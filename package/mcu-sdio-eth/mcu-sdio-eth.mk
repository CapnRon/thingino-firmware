MCU_SDIO_ETH_SITE_METHOD = local
MCU_SDIO_ETH_SITE = $(BR2_EXTERNAL_THINGINO_PATH)/package/mcu-sdio-eth/files
MCU_SDIO_ETH_VERSION = local

MCU_SDIO_ETH_LICENSE = GPL-2.0

MCU_SDIO_ETH_DEPENDENCIES = linux

define MCU_SDIO_ETH_BUILD_CMDS
	$(MAKE) -C $(@D) \
		KERNEL=$(LINUX_DIR) \
		ARCH=mips \
		CROSS_COMPILE=$(TARGET_CROSS) \
		all
endef

define MCU_SDIO_ETH_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/esp32_sdio.ko \
		$(TARGET_DIR)/usr/lib/modules/3.10.14__isvp_pike_1.0__/extra/mcu_sdio_eth.ko
endef

$(eval $(generic-package))
