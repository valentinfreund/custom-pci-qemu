#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "hw/qdev-properties.h"
#include "hw/pci/msi.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "sysemu/dma.h"

#define TYPE_CUSTOM_PCI_DEVICE "custom-pci-device"
OBJECT_DECLARE_SIMPLE_TYPE(CustomPCIState, CUSTOM_PCI_DEVICE)

//pci device information
#define CUSTOM_PCI_VENDOR_ID    0x1234
#define CUSTOM_PCI_DEVICE_ID    0x5678
#define CUSTOM_PCI_REVISION     0x01
#define BAR0_SIZE               4096
//bar0 register map
#define REG_IRQ_TRIGGER         0x00
#define REG_DMA_ADDR_LOW        0x04
#define REG_DMA_ADDR_HIGH       0x08
#define REG_DMA_SIZE            0x0C
#define REG_DMA_CMD             0x10
#define REG_DEV_ADDR            0x14
#define REG_STATUS              0x18
//dma commands
#define CMD_DMA_READ            0x01    //guest memory -> device memory
#define CMD_DMA_WRITE           0x02    //device memory -> guest memory
//status bits
#define STATUS_ERROR            (1u << 0)   //last DMA command was rejected
//internal device memory
#define DEV_MEM_SIZE            4096


//############################
// state container			##
//############################
typedef struct CustomPCIState {
    PCIDevice parent_obj;

    MemoryRegion bar0;

    uint32_t dma_addr_low;
    uint32_t dma_addr_high;
    uint32_t dma_size;
    uint32_t dev_addr;
    uint32_t status;

    uint8_t mem[DEV_MEM_SIZE];
} CustomPCIState;

//##################################################
// @name 		custom_pci_raise_irq
// @param		device
// @function	signals the guest via MSI (if enabled)
//-------------------------------------------------
static void custom_pci_raise_irq(CustomPCIState *d)
{
    PCIDevice *pci_dev = PCI_DEVICE(d);

    if (msi_enabled(pci_dev)) {
        msi_notify(pci_dev, 0);
    }
}

//##################################################
// @name 		do_custom_dma
// @param		device, command type
// @function	copies dma_size bytes between guest memory
//				at dma_addr and device memory at dev_addr,
//				then raises an interrupt
//-------------------------------------------------
static void do_custom_dma(CustomPCIState *d, uint32_t cmd)
{
    PCIDevice *pci_dev = PCI_DEVICE(d);
    dma_addr_t dma_addr = ((dma_addr_t)d->dma_addr_high << 32) | d->dma_addr_low;
    uint32_t size = d->dma_size;

    d->status = 0;

    if (size == 0 || d->dev_addr >= DEV_MEM_SIZE || size > DEV_MEM_SIZE - d->dev_addr) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "custom-pci-device: invalid DMA: %u bytes at device offset %u\n",
                      size, d->dev_addr);
        d->status = STATUS_ERROR;
    } else if (cmd == CMD_DMA_READ) {
        pci_dma_read(pci_dev, dma_addr, &d->mem[d->dev_addr], size);
    } else if (cmd == CMD_DMA_WRITE) {
        pci_dma_write(pci_dev, dma_addr, &d->mem[d->dev_addr], size);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "custom-pci-device: unknown DMA command %u\n", cmd);
        d->status = STATUS_ERROR;
    }

    custom_pci_raise_irq(d);
}

//##################################################
// @name 		custom_pci_bar0_read
// @param		device state, hwaddr, size
// @return		register value
// @function	called when the guest reads from BAR0
//-------------------------------------------------
static uint64_t custom_pci_bar0_read(void *opaque, hwaddr addr, unsigned size)
{
    CustomPCIState *d = opaque;

    switch (addr) {
    case REG_DMA_ADDR_LOW:
        return d->dma_addr_low;
    case REG_DMA_ADDR_HIGH:
        return d->dma_addr_high;
    case REG_DMA_SIZE:
        return d->dma_size;
    case REG_DEV_ADDR:
        return d->dev_addr;
    case REG_STATUS:
        return d->status;
    default:
        return 0;
    }
}

//##################################################
// @name 		custom_pci_bar0_write
// @param		device state, hwaddr, val, size
// @return		void
// @function	called when the guest writes to BAR0
//-------------------------------------------------
static void custom_pci_bar0_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    CustomPCIState *d = opaque;

    switch (addr) {
    case REG_IRQ_TRIGGER:
        if (val == 1) {
            custom_pci_raise_irq(d);
        }
        break;
    case REG_DMA_ADDR_LOW:
        d->dma_addr_low = (uint32_t)val;
        break;
    case REG_DMA_ADDR_HIGH:
        d->dma_addr_high = (uint32_t)val;
        break;
    case REG_DMA_SIZE:
        d->dma_size = (uint32_t)val;
        break;
    case REG_DMA_CMD:
        do_custom_dma(d, (uint32_t)val);
        break;
    case REG_DEV_ADDR:
        d->dev_addr = (uint32_t)val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "custom-pci-device: write to unknown register 0x%" HWADDR_PRIx "\n",
                      addr);
        break;
    }
}


//############################
// memory operations		##
//############################
static const MemoryRegionOps custom_pci_bar0_ops = {
    .read = custom_pci_bar0_read,
    .write = custom_pci_bar0_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {          //what the guest may do
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl = {           //what the callbacks above implement
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

//##################################################
// @name 	    custom_pci_device_realize
// @param		device, error
// @return		void
// @function	initializes the pci device state
//-------------------------------------------------
static void custom_pci_device_realize(PCIDevice *pci_dev, Error **errp)
{
    CustomPCIState *d = CUSTOM_PCI_DEVICE(pci_dev);

    /* Initialize BAR0 as memory-mapped I/O */
    memory_region_init_io(&d->bar0, OBJECT(d), &custom_pci_bar0_ops, d, "custom-pci-bar0", BAR0_SIZE);
    pci_register_bar(pci_dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &d->bar0);

    /* Initialize MSI (device, config space offset, #vectors, 64bit, per vector mask, error pointer) */
    if (msi_init(pci_dev, 0, 1, true, false, errp)) {
        return;
    }
}

//##################################################
// @name 		custom_pci_device_reset
// @param		device
// @return		void
// @function	gets called on system reset
//-------------------------------------------------
static void custom_pci_device_reset(DeviceState *dev)
{
    CustomPCIState *d = CUSTOM_PCI_DEVICE(dev);

    d->dma_addr_low = 0;
    d->dma_addr_high = 0;
    d->dma_size = 0;
    d->dev_addr = 0;
    d->status = 0;
    memset(d->mem, 0, sizeof(d->mem));
}

//##################################################
// @name 		custom_pci_device_exit
// @param		pcidevice
// @return		void
// @function	gets called at shut down
//-------------------------------------------------
static void custom_pci_device_exit(PCIDevice *pci_dev)
{
    msi_uninit(pci_dev);
}


//##################################################
// @name 		custom_pci_device_class_init
// @param		object class, data
// @return		void
// @function	gets called at start up
//-------------------------------------------------
static void custom_pci_device_class_init(ObjectClass *oclass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oclass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(oclass);

    k->realize = custom_pci_device_realize;
    k->exit = custom_pci_device_exit;
    k->vendor_id = CUSTOM_PCI_VENDOR_ID;
    k->device_id = CUSTOM_PCI_DEVICE_ID;
    k->revision = CUSTOM_PCI_REVISION;
    k->class_id = PCI_CLASS_OTHERS;

    dc->reset = custom_pci_device_reset;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->desc = "Custom PCI Device";
}


//############################
// type info				##
//############################
static const TypeInfo custom_pci_device_info = {
    .name          = TYPE_CUSTOM_PCI_DEVICE,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(CustomPCIState),
    .class_init    = custom_pci_device_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

//##################################################
// @name 		custom_pci_device_register_types
// @param		void
// @return		void
// @function	gets called at start up
//-------------------------------------------------
static void custom_pci_device_register_types(void)
{
    type_register_static(&custom_pci_device_info);
}

type_init(custom_pci_device_register_types)
