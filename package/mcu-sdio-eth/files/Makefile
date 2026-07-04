CONFIG_BT_ENABLED := n
CONFIG_TEST_RAW_TP := n
CONFIG_INFO_LOGS=y
CONFIG_DEBUG_LOGS=n
CONFIG_VERBOSE_LOGS=n

CONFIG_ESP_HOSTED_USE_WORKQUEUE=n

# Target slave chip (ATBM6441 = ESP32-compatible)
ESP_SLAVE := "CONFIG_TARGET_ESP32"

CROSS_COMPILE :=
KERNEL := /lib/modules/$(shell uname -r)/build
ARCH := arm64

PWD := $(shell pwd)

MODULE_NAME=esp32_sdio

ifeq ($(target), sdio)
	MODULE_NAME=esp32_sdio
endif

ifeq ($(target), spi)
	MODULE_NAME=esp32_spi
endif

ifeq ($(CONFIG_TEST_RAW_TP), y)
	EXTRA_CFLAGS += -DCONFIG_TEST_RAW_TP
endif

ifeq ($(CONFIG_DEBUG_STATS), y)
	EXTRA_CFLAGS += -DESP_DEBUG_STATS
endif

ifeq ($(CONFIG_BT_ENABLED), y)
	EXTRA_CFLAGS += -DCONFIG_BT_ENABLED
	module_objects += esp_bt.o
endif

# Patched include paths for Thingino package layout
EXTRA_CFLAGS += -I$(src)/common -I$(src)

ifeq ($(MODULE_NAME), esp32_sdio)
	EXTRA_CFLAGS += -I$(src)/sdio
	module_objects += sdio/esp_sdio.o sdio/esp_sdio_api.o
endif

ifeq ($(MODULE_NAME), esp32_spi)
	EXTRA_CFLAGS += -I$(src)/spi
	module_objects += spi/esp_spi.o
endif

ifneq ($(ESP_SLAVE), "")
EXTRA_CFLAGS += -D$(ESP_SLAVE)
endif

ifeq ($(CONFIG_INFO_LOGS), y)
	EXTRA_CFLAGS += -DCONFIG_INFO_LOGS
endif

ifeq ($(CONFIG_DEBUG_LOGS), y)
	EXTRA_CFLAGS += -DCONFIG_INFO_LOGS
	EXTRA_CFLAGS += -DCONFIG_DEBUG_LOGS
endif

ifeq ($(CONFIG_VERBOSE_LOGS), y)
	EXTRA_CFLAGS += -DCONFIG_INFO_LOGS
	EXTRA_CFLAGS += -DCONFIG_DEBUG_LOGS
	EXTRA_CFLAGS += -DCONFIG_VERBOSE_LOGS
endif

ifeq ($(CONFIG_ESP_HOSTED_USE_WORKQUEUE), y)
	EXTRA_CFLAGS += -DCONFIG_ESP_HOSTED_USE_WORKQUEUE=1
else
	EXTRA_CFLAGS += -DCONFIG_ESP_HOSTED_USE_THREAD=1
endif

obj-m := $(MODULE_NAME).o
$(MODULE_NAME)-y := main.o esp_stats.o $(module_objects)
$(MODULE_NAME)-y += esp_serial.o esp_rb.o esp_fw_verify.o

all:
	make ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KERNEL) M=$(PWD) modules

clean:
	rm -rf *.o sdio/*.o spi/*.o *.ko
	rm -rf .*.cmd .tmp_versions *.mod.c *.mod *.order *.symvers *.markers
	rm -rf sdio/.*.cmd spi/.*.cmd
	-@make ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KERNEL) M=$(PWD) clean 2>/dev/null || true
