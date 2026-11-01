################################################################################
#
# drv-pci
#
################################################################################

DRV_PCI_VERSION = 1.0
DRV_PCI_SITE = $(BR2_EXTERNAL_DRIVERLAB_PATH)/../drivers/pci-dma
DRV_PCI_SITE_METHOD = local
DRV_PCI_LICENSE = GPL-2.0

$(eval $(kernel-module))
$(eval $(generic-package))
