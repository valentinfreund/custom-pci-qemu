/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Register map of custom-pci-device: the contract between the QEMU device
 * model (qemu/devices/custom-pci-device.c) and its Linux driver
 * (drivers/pci-dma/drv_pci.c). Macros only, so it builds in both projects.
 */
#ifndef DRIVERLAB_CUSTOM_PCI_REGS_H
#define DRIVERLAB_CUSTOM_PCI_REGS_H

/* PCI identity */
#define CUSTOM_PCI_VENDOR_ID        0x1234
#define CUSTOM_PCI_DEVICE_ID        0x5678
#define CUSTOM_PCI_REVISION         0x01

/* Sizes */
#define CUSTOM_PCI_BAR0_SIZE        4096    /* register block */
#define CUSTOM_PCI_MEM_SIZE         4096    /* device memory, reachable via DMA */

/* BAR0 registers (32-bit accesses) */
#define CUSTOM_PCI_REG_IRQ_TRIGGER  0x00    /* W: 1 raises an MSI */
#define CUSTOM_PCI_REG_DMA_ADDR_LO  0x04    /* R/W: DMA bus address, bits 31:0 */
#define CUSTOM_PCI_REG_DMA_ADDR_HI  0x08    /* R/W: DMA bus address, bits 63:32 */
#define CUSTOM_PCI_REG_DMA_SIZE     0x0C    /* R/W: transfer size in bytes */
#define CUSTOM_PCI_REG_DMA_CMD      0x10    /* W: starts a transfer */
#define CUSTOM_PCI_REG_DEV_ADDR     0x14    /* R/W: offset in device memory */
#define CUSTOM_PCI_REG_STATUS       0x18    /* R: status bits */

/* DMA_CMD values */
#define CUSTOM_PCI_CMD_DMA_READ     0x01    /* guest memory -> device memory */
#define CUSTOM_PCI_CMD_DMA_WRITE    0x02    /* device memory -> guest memory */

/* STATUS bits */
#define CUSTOM_PCI_STATUS_ERROR     (1u << 0)   /* last DMA command was rejected */

#endif /* DRIVERLAB_CUSTOM_PCI_REGS_H */
