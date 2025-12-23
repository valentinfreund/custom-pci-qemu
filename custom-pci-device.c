#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "hw/qdev-properties.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"

#define TYPE_CUSTOM_PCI_DEVICE "custom-pci-device"
OBJECT_DECLARE_SIMPLE_TYPE(PCIDeviceState, CUSTOM_PCI_DEVICE)

#define PCI_VENDOR_ID    0x1234
#define PCI_DEVICE_ID    0x5678
#define PCI_REVISION     0x01
#define BAR0_SIZE           4096


//##############################
// state container						##
//##############################
typedef struct PCIDeviceState {
    PCIDevice parent_obj;
    
    MemoryRegion bar0;
    uint8_t bar0_data[BAR0_SIZE];
} MyPCIDeviceState;

//##################################################
// @name 			custom_pci_bar0_read
// @param			device state, hwaddr, size
// @return		0 or error code
// @function	read function is called 
//						if something is written into the bar
//-------------------------------------------------
static uint64_t custom_pci_bar0_read(void *opaque, hwaddr addr, unsigned size)
{
    MyPCIDeviceState *d = opaque;
    uint64_t val = 0;
    
    if (addr + size <= BAR0_SIZE) {
        memcpy(&val, &d->bar0_data[addr], size);
        qemu_log_mask(LOG_GUEST_ERROR, "custom-pci-device: BAR0 read at 0x%lx, size %d, value 0x%lx\n", addr, size, val);
    }
    
    return val;
}

//##################################################
// @name 			custom_pci_bar0_write
// @param			device state, hwaddr, val, size
// @return		void
// @function	write function is called 
//						if something needs to be written into the bar
//-------------------------------------------------
static void custom_pci_bar0_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    MyPCIDeviceState *d = opaque;
    
    if (addr + size <= BAR0_SIZE) {
        memcpy(&d->bar0_data[addr], &val, size);
        qemu_log_mask(LOG_GUEST_ERROR, "custom-pci-device: BAR0 write at 0x%lx, size %d, value 0x%lx\n", addr, size, val);
    }
}


//##############################
// memory operations					##
//##############################
static const MemoryRegionOps custom_pci_bar0_ops = {
    .read = custom_pci_bar0_read,
    .write = custom_pci_bar0_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

//##################################################
// @name 			custom_pci_device_realize
// @param			device, error
// @return		void
// @function	initializes the pci device state
//-------------------------------------------------
static void custom_pci_device_realize(PCIDevice *pci_dev, Error **errp)
{
    PCIDeviceState *d = CUSTOM_PCI_DEVICE(pci_dev);
    
    /* Initialize BAR0 as memory-mapped I/O */
    memory_region_init_io(&d->bar0, OBJECT(d), &custom_pci_bar0_ops, d, "custom-pci-bar0", BAR0_SIZE);
    pci_register_bar(pci_dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &d->bar0);
    memset(d->bar0_data, 0, BAR0_SIZE);
}


//##################################################
// @name 			custom_pci_device_exit
// @param			device
// @return		void
// @function	gets called at shut down
//-------------------------------------------------
static void custom_pci_device_exit(PCIDevice *pci_dev)
{}


//##################################################
// @name 			custom_pci_device_class_init
// @param			object class, data
// @return		void
// @function	gets called at start up
//-------------------------------------------------
static void custom_pci_device_class_init(ObjectClass *oclass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oclass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(oclass);
    
    k->realize = custom_pci_device_realize;
    k->exit = custom_pci_device_exit;
    k->vendor_id = PCI_VENDOR_ID;
    k->device_id = PCI_DEVICE_ID;
    k->revision = PCI_REVISION;
    k->class_id = PCI_CLASS_OTHERS;
    
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->desc = "Custom PCI Device";
}


//##############################
// type info									##
//##############################
static const TypeInfo custom_pci_device_info = {
    .name          = TYPE_CUSTOM_PCI_DEVICE,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIDeviceState),
    .class_init    = custom_pci_device_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

//##################################################
// @name 			custom_pci_device_register_types
// @param			void
// @return		void
// @function	gets called at start up
//-------------------------------------------------
static void custom_pci_device_register_types(void)
{
    type_register_static(&custom_pci_device_info);
}

type_init(custom_pci_device_register_types)
