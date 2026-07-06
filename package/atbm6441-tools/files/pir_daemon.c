/*
 * pir_daemon: decode ATBM6441 uartmsg frames on /dev/ttyS0 and surface
 * PIR trigger events on the video OSD via raptor's rod daemon.
 *
 * Frame: 0x7B(start) LEN TYPE PAYLOAD[LEN-4] CHECKSUM
 *   LEN counts the whole frame (start..checksum), valid range 4..132.
 *   CHECKSUM = (~(start ^ LEN ^ TYPE ^ payload bytes)) & 0xFF
 *
 * Reverse-engineered from contractMCU's uartmsg_recv_task and confirmed
 * against live hardware capture: a PIR wave produces `7B 04 03 83`
 * (TYPE=3, zero-length payload), repeated for the duration of motion.
 *
 * On each PIR trigger the daemon sets rod template variable %pir% to
 * "MOTION"; after PIR_HOLD_SEC without triggers it resets it to "--".
 * The OSD text element is (re-)registered before every update so a rod
 * restart can't orphan the display (re-add of an existing element is a
 * cheap no-op error that we ignore).
 *
 * rod control socket wire protocol (rss_ctrl.c): 2-byte big-endian
 * length prefix + JSON body, one request/response per connection.
 *
 * NOTE: deliberately does NOT talk to rmd — enabling rmd/IVS hard-locks
 * this board (see session log 2026-07-07); OSD-only until that is fixed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <time.h>
#include <stdint.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>

#define MAX_FRAME 132
#define ROD_SOCK_PATH "/var/run/rss/rod.sock"
#define PIR_TYPE 0x03
#define PIR_HOLD_SEC 5

static int open_uart(const char *path)
{
	int fd = open(path, O_RDONLY | O_NOCTTY);
	if (fd < 0) {
		perror("open");
		return -1;
	}

	struct termios tio;
	if (tcgetattr(fd, &tio) != 0) {
		perror("tcgetattr");
		close(fd);
		return -1;
	}

	cfmakeraw(&tio);
	cfsetispeed(&tio, B115200);
	cfsetospeed(&tio, B115200);
	tio.c_cflag |= (CLOCAL | CREAD);
	tio.c_cflag &= ~PARENB;
	tio.c_cflag &= ~CSTOPB;
	tio.c_cflag &= ~CSIZE;
	tio.c_cflag |= CS8;
	tio.c_cc[VMIN] = 1;
	tio.c_cc[VTIME] = 0;

	if (tcsetattr(fd, TCSANOW, &tio) != 0) {
		perror("tcsetattr");
		close(fd);
		return -1;
	}

	return fd;
}

static void timestamp(char *buf, size_t len)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	struct tm tm;
	localtime_r(&ts.tv_sec, &tm);
	snprintf(buf, len, "%02d:%02d:%02d.%03ld",
		 tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000000);
}

/* Send one JSON command to rod's control socket and drain the response.
 * Best-effort: if rod isn't up yet, fail silently and let the caller
 * retry on the next event. Returns 0 on success. */
static int rod_send(const char *json)
{
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;

	struct sockaddr_un addr = {0};
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, ROD_SOCK_PATH, sizeof(addr.sun_path) - 1);

	struct timeval tv = {.tv_sec = 1, .tv_usec = 0};
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		close(fd);
		return -1;
	}

	uint16_t len = (uint16_t)strlen(json);
	unsigned char hdr[2] = {(unsigned char)(len >> 8), (unsigned char)(len & 0xFF)};

	if (write(fd, hdr, 2) != 2 || write(fd, json, len) != (ssize_t)len) {
		close(fd);
		return -1;
	}

	/* Drain the response so rod's accept/handle loop doesn't stall. */
	unsigned char resp_hdr[2];
	if (read(fd, resp_hdr, 2) == 2) {
		uint16_t resp_len = ((uint16_t)resp_hdr[0] << 8) | resp_hdr[1];
		char discard[256];
		while (resp_len > 0) {
			size_t chunk = resp_len > sizeof(discard) ? sizeof(discard) : resp_len;
			ssize_t n = read(fd, discard, chunk);
			if (n <= 0)
				break;
			resp_len -= (uint16_t)n;
		}
	}

	close(fd);
	return 0;
}

/* Ensure the OSD element exists, then set %pir%. add-element on an
 * existing element is rejected by rod without logging -- harmless.
 * Returns 0 if rod accepted the update. */
static int osd_set_pir(const char *value)
{
	rod_send("{\"cmd\":\"add-element\",\"name\":\"pir\",\"type\":\"text\","
		 "\"template\":\"PIR %pir%\",\"position\":\"top_right\",\"max_chars\":12}");

	char cmd[128];
	snprintf(cmd, sizeof(cmd), "{\"cmd\":\"set-var\",\"name\":\"pir\",\"value\":\"%s\"}",
		 value);
	if (rod_send(cmd) != 0) {
		char ts[32];
		timestamp(ts, sizeof(ts));
		fprintf(stderr, "[%s] rod not reachable at %s (will retry)\n", ts, ROD_SOCK_PATH);
		return -1;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	const char *dev = (argc > 1) ? argv[1] : "/dev/ttyS0";
	int fd = open_uart(dev);
	if (fd < 0)
		return 1;

	printf("pir_daemon: reading %s, PIR status -> rod OSD var %%pir%% (hold %ds)\n",
	       dev, PIR_HOLD_SEC);
	fflush(stdout);

	unsigned char buf[MAX_FRAME];
	int have = 0;
	time_t last_trigger = 0;
	int motion_shown = 0;
	/* rod may still be initializing when we start (S32 vs S31raptor);
	 * retry the idle display on the 500ms tick until it lands. */
	int synced = (osd_set_pir("--") == 0);

	for (;;) {
		fd_set rfds;
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		struct timeval tv = {.tv_sec = 0, .tv_usec = 500000};

		int sr = select(fd + 1, &rfds, NULL, NULL, &tv);
		if (sr < 0) {
			perror("select");
			break;
		}

		if (sr == 0) {
			/* idle tick: expire the MOTION display */
			if (motion_shown && time(NULL) - last_trigger >= PIR_HOLD_SEC) {
				synced = (osd_set_pir("--") == 0);
				motion_shown = 0;
			} else if (!synced) {
				synced = (osd_set_pir(motion_shown ? "MOTION" : "--") == 0);
			}
			continue;
		}

		unsigned char b;
		ssize_t n = read(fd, &b, 1);
		if (n <= 0) {
			if (n < 0)
				perror("read");
			break;
		}

		if (have == 0) {
			if (b != 0x7B)
				continue;
			buf[have++] = b;
			continue;
		}

		buf[have++] = b;

		if (have == 2) {
			unsigned char len = buf[1];
			if (len < 4 || len > MAX_FRAME) {
				if (buf[1] == 0x7B) {
					buf[0] = buf[1];
					have = 1;
				} else {
					have = 0;
				}
			}
			continue;
		}

		unsigned char len = buf[1];
		if (have < len)
			continue;

		unsigned char chk = 0;
		int i;
		for (i = 0; i < len - 1; i++)
			chk ^= buf[i];
		chk = (~chk) & 0xFF;

		if (chk == buf[len - 1]) {
			unsigned char type = buf[2];
			int paylen = len - 4;

			if (type == PIR_TYPE && paylen == 0) {
				char ts[32];
				timestamp(ts, sizeof(ts));
				printf("[%s] PIR_TRIGGER -> OSD\n", ts);
				fflush(stdout);
				last_trigger = time(NULL);
				if (!motion_shown) {
					synced = (osd_set_pir("MOTION") == 0);
					motion_shown = 1;
				}
			}
		}
		/* Unknown types / checksum errors: ignore, keep listening. */

		have = 0;
	}

	close(fd);
	return 0;
}
