MOTORS_SITE_METHOD = git
MOTORS_SITE = https://github.com/gtxaspec/ingenic-motor.git
MOTORS_VERSION = $(shell git ls-remote $(MOTORS_SITE) HEAD | head -1 | cut -f1)

MOTORS_LICENSE = MIT
MOTORS_LICENSE_FILES = LICENSE

define MOTORS_BUILD_CMDS
	$(TARGET_CC) -Os -s $(@D)/motor.c -o $(@D)/motors
endef

define MOTORS_INSTALL_TARGET_CMDS
	$(INSTALL) -m 755 -d $(TARGET_DIR)/etc/init.d
	$(INSTALL) -m 755 -t $(TARGET_DIR)/etc/init.d/ $(MOTORS_PKGDIR)/files/S09motor

	$(INSTALL) -m 0755 -D $(@D)/motors $(TARGET_DIR)/usr/bin/motors
endef

$(eval $(generic-package))
