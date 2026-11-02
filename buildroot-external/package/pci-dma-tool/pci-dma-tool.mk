################################################################################
#
# pci-dma-tool
#
################################################################################

PCI_DMA_TOOL_VERSION = 1.0
PCI_DMA_TOOL_SITE = $(BR2_EXTERNAL_DRIVERLAB_PATH)/../userspace/pci-dma-tool
PCI_DMA_TOOL_SITE_METHOD = local
PCI_DMA_TOOL_LICENSE = GPL-2.0

define PCI_DMA_TOOL_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) CC="$(TARGET_CC)" CFLAGS="$(TARGET_CFLAGS)" \
		DRIVERLAB_INCLUDE=$(BR2_EXTERNAL_DRIVERLAB_PATH)/../include
endef

define PCI_DMA_TOOL_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/pci-dma-tool $(TARGET_DIR)/usr/bin/pci-dma-tool
endef

$(eval $(generic-package))
