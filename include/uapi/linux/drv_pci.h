/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Userspace API of the drv_pci driver (/dev/drv_pci<N>), shared by the
 * driver and userspace programs.
 *
 * Besides read()/write(), the driver offers:
 *   - mmap():  maps the DMA buffer (DRV_PCI_IOC_INFO.buf_size bytes) into
 *              the process, so data does not have to be copied.
 *   - ioctl(): DRV_PCI_IOC_INFO and DRV_PCI_IOC_DMA below.
 *
 * Rules for this file: fixed-size types only (__u32, __u64), no implicit
 * padding, reserved fields must be 0 so they can be used later.
 */
#ifndef _UAPI_LINUX_DRV_PCI_H
#define _UAPI_LINUX_DRV_PCI_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define DRV_PCI_IOC_MAGIC       0xD7

struct drv_pci_info {
	__u32 buf_size;         /* size of the DMA buffer, mmap() up to this size */
	__u32 mem_size;         /* size of the device memory */
	__u32 instance;         /* N in /dev/drv_pci<N> */
	__u32 reserved;
};

/* drv_pci_dma.direction */
#define DRV_PCI_DMA_TO_DEVICE   1       /* DMA buffer -> device memory */
#define DRV_PCI_DMA_FROM_DEVICE 2       /* device memory -> DMA buffer */

struct drv_pci_dma {
	__u32 direction;        /* DRV_PCI_DMA_* */
	__u32 size;             /* bytes, 1 .. buf_size */
	__u32 buf_offset;       /* offset in the DMA buffer */
	__u32 dev_offset;       /* offset in the device memory */
	__u32 flags;            /* must be 0 */
	__u32 reserved;         /* must be 0 */
};

/* Query buffer and device sizes. */
#define DRV_PCI_IOC_INFO        _IOR(DRV_PCI_IOC_MAGIC, 0x01, struct drv_pci_info)
/* Run one DMA transfer and wait for it to complete. */
#define DRV_PCI_IOC_DMA         _IOW(DRV_PCI_IOC_MAGIC, 0x02, struct drv_pci_dma)

#endif /* _UAPI_LINUX_DRV_PCI_H */
