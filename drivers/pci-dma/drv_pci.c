#include <linux/module.h>
#include <linux/pci.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/idr.h>

#include <driverlab/custom-pci-regs.h>   /* register map shared with the QEMU device */
#include <linux/drv_pci.h>               /* UAPI: ioctl interface, shared with userspace */

//device information
#define DRV_NAME "drv_pci"
#define DMA_BUF_SIZE      CUSTOM_PCI_MEM_SIZE
#define DMA_TIMEOUT_MS    1000


//-------------------------------------------------
//-------------------------------------------------
MODULE_DESCRIPTION("PCI driver for custom-pci-device: MMIO, DMA and MSI");
MODULE_AUTHOR("Valentin Freundorfer");
MODULE_LICENSE("GPL");
//-------------------------------------------------
//-------------------------------------------------


//############################
// state container			##
//############################
struct dev_state {
    struct pci_dev* pdev;
    void __iomem* bar0;

    void* dma_buf; 			//virtual address
    dma_addr_t dma_handle; 	//physical bus address

    int irq;

    struct mutex lock;      //one DMA transfer at a time (shared buffer + completion)
    struct completion dma_comp;
    struct miscdevice miscdev;
    int id;                 //instance number: /dev/drv_pci<id>
    char name[16];
};

//hands out the instance numbers for /dev/drv_pci<N>
static DEFINE_IDA(drv_pci_ida);

//##################################################
// @name 	    custompci_irq_handler
// @function	signals the end of a DMA transfer
//-------------------------------------------------
static irqreturn_t custompci_irq_handler(int irq, void *dev_id)
{
    struct dev_state *st = dev_id;

    dev_dbg(&st->pdev->dev, "interrupt\n");
    complete(&st->dma_comp);

    return IRQ_HANDLED;
}

//##################################################
// @name 	    run_dma
// @param		device state, command, buffer offset,
//              device memory offset, length
// @return		0 or error code
// @function	programs one DMA transfer of len bytes between
//              the DMA buffer at buf_off and the device memory
//              at dev_off and waits for the completion interrupt.
//              Caller must hold st->lock.
//-------------------------------------------------
static int run_dma(struct dev_state *st, u32 cmd, u32 buf_off, u32 dev_off, size_t len)
{
    dma_addr_t addr = st->dma_handle + buf_off;
    long ret;

    reinit_completion(&st->dma_comp);

    iowrite32(lower_32_bits(addr), st->bar0 + CUSTOM_PCI_REG_DMA_ADDR_LO);
    iowrite32(upper_32_bits(addr), st->bar0 + CUSTOM_PCI_REG_DMA_ADDR_HI);
    iowrite32((u32)len, st->bar0 + CUSTOM_PCI_REG_DMA_SIZE);
    iowrite32(dev_off, st->bar0 + CUSTOM_PCI_REG_DEV_ADDR);
    iowrite32(cmd, st->bar0 + CUSTOM_PCI_REG_DMA_CMD);

    ret = wait_for_completion_interruptible_timeout(&st->dma_comp,
                                                    msecs_to_jiffies(DMA_TIMEOUT_MS));
    if (ret == 0) {
        dev_err(&st->pdev->dev, "DMA timeout (cmd %u, %zu bytes)\n", cmd, len);
        return -ETIMEDOUT;
    }
    if (ret < 0)
        return ret;     //interrupted by a signal

    if (ioread32(st->bar0 + CUSTOM_PCI_REG_STATUS) & CUSTOM_PCI_STATUS_ERROR) {
        dev_err(&st->pdev->dev, "device rejected DMA (cmd %u, %zu bytes at device offset %u)\n",
                cmd, len, dev_off);
        return -EIO;
    }

    dev_dbg(&st->pdev->dev, "DMA done (cmd %u, %zu bytes, buffer offset %u, device offset %u)\n",
            cmd, len, buf_off, dev_off);
    return 0;
}

//##################################################
// @name 	    miscpci_dma_read
// @param		file, char, length, offset
// @return		length of transferred data
// @function	lets the device write into the DMA buffer
//              and copies the result to user space
//-------------------------------------------------
static ssize_t miscpci_dma_read(struct file *file, char __user *ubuf, size_t len, loff_t *off)
{
    struct dev_state *st = container_of(file->private_data, struct dev_state, miscdev);
    ssize_t ret;

    if (*off >= DMA_BUF_SIZE)
        return 0;

    if (len > DMA_BUF_SIZE - *off)
        len = DMA_BUF_SIZE - *off;

    if (mutex_lock_interruptible(&st->lock))
        return -ERESTARTSYS;

    ret = run_dma(st, CUSTOM_PCI_CMD_DMA_WRITE, *off, *off, len);
    if (ret)
        goto out;

    if (copy_to_user(ubuf, st->dma_buf + *off, len)) {
        ret = -EFAULT;
        goto out;
    }

    *off += len;
    ret = len;
out:
    mutex_unlock(&st->lock);
    return ret;
}

//##################################################
// @name 	    miscpci_dma_write
// @param		file, char, length, offset
// @return		length of copied data
// @function	copies user data into the DMA buffer
//              and lets the device read it
//-------------------------------------------------
static ssize_t miscpci_dma_write(struct file *file, const char __user *ubuf, size_t len, loff_t *off)
{
    struct dev_state *st = container_of(file->private_data, struct dev_state, miscdev);
    ssize_t ret;

    if (*off >= DMA_BUF_SIZE)
        return -ENOSPC;

    if (len > DMA_BUF_SIZE - *off)
        len = DMA_BUF_SIZE - *off;

    if (mutex_lock_interruptible(&st->lock))
        return -ERESTARTSYS;

    if (copy_from_user(st->dma_buf + *off, ubuf, len)) {
        ret = -EFAULT;
        goto out;
    }

    ret = run_dma(st, CUSTOM_PCI_CMD_DMA_READ, *off, *off, len);
    if (ret)
        goto out;

    *off += len;
    ret = len;
out:
    mutex_unlock(&st->lock);
    return ret;
}

//##################################################
// @name 	    ioctl_dma
// @param		device state, user pointer to struct drv_pci_dma
// @return		0 or error code
// @function	DRV_PCI_IOC_DMA: validates the request and runs
//              one DMA transfer on the (mmap'ed) DMA buffer
//-------------------------------------------------
static long ioctl_dma(struct dev_state *st, void __user *uarg)
{
    struct drv_pci_dma req;
    u32 cmd;
    long ret;

    if (copy_from_user(&req, uarg, sizeof(req)))
        return -EFAULT;

    //never trust userspace: check every field before touching the hardware
    if (req.flags || req.reserved)
        return -EINVAL;
    if (req.direction == DRV_PCI_DMA_TO_DEVICE)
        cmd = CUSTOM_PCI_CMD_DMA_READ;          //device reads the buffer
    else if (req.direction == DRV_PCI_DMA_FROM_DEVICE)
        cmd = CUSTOM_PCI_CMD_DMA_WRITE;         //device writes the buffer
    else
        return -EINVAL;
    //written as "offset > size - len" so that large values cannot overflow
    if (req.size == 0 || req.size > DMA_BUF_SIZE ||
        req.buf_offset > DMA_BUF_SIZE - req.size ||
        req.dev_offset > CUSTOM_PCI_MEM_SIZE - req.size)
        return -EINVAL;

    if (mutex_lock_interruptible(&st->lock))
        return -ERESTARTSYS;
    ret = run_dma(st, cmd, req.buf_offset, req.dev_offset, req.size);
    mutex_unlock(&st->lock);

    return ret;
}

//##################################################
// @name 	    miscpci_ioctl
// @param		file, ioctl command, argument
// @return		0 or error code
// @function	dispatches the ioctls of include/uapi/linux/drv_pci.h
//-------------------------------------------------
static long miscpci_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct dev_state *st = container_of(file->private_data, struct dev_state, miscdev);
    void __user *uarg = (void __user *)arg;

    switch (cmd) {
    case DRV_PCI_IOC_INFO: {
        //initializer zeroes all other fields: no kernel stack data leaks to userspace
        struct drv_pci_info info = {
            .buf_size = DMA_BUF_SIZE,
            .mem_size = CUSTOM_PCI_MEM_SIZE,
            .instance = st->id,
        };

        return copy_to_user(uarg, &info, sizeof(info)) ? -EFAULT : 0;
    }
    case DRV_PCI_IOC_DMA:
        return ioctl_dma(st, uarg);
    default:
        return -ENOTTY;     //convention for "unknown ioctl"
    }
}

//############################
// file operations    		##
//############################
static const struct file_operations miscpci_fops = {
    .owner = THIS_MODULE,
    .read  = miscpci_dma_read,
    .write = miscpci_dma_write,
    .unlocked_ioctl = miscpci_ioctl,
    .compat_ioctl   = compat_ptr_ioctl,     //32-bit programs: same struct layout
};

//##################################################
// @name 	    custompci_probe
// @param		pci_dev and pci_device_id
// @return		0 or error code
// @function	gets called for each list entry,
//				allocates and initializes
//-------------------------------------------------
static int custompci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    int ret;
    struct dev_state *st;

    pr_info(DRV_NAME ": probe() called for device %04x:%04x\n",
            pdev->vendor, pdev->device);

    /* Enable PCI device */
    ret = pci_enable_device(pdev);
    if (ret)
        return ret;
        
    pci_set_master(pdev);

    /* Allocate state container */
    st = devm_kzalloc(&pdev->dev, sizeof(*st), GFP_KERNEL);
    if (!st) {
        ret = -ENOMEM;
        goto err_disable;
    }

    pci_set_drvdata(pdev, st);
    st->pdev = pdev;
    mutex_init(&st->lock);
    init_completion(&st->dma_comp);
	
    /* Request and map BAR0 */
    ret = pci_request_region(pdev, 0, DRV_NAME);
    if (ret)
        goto err_disable;

    st->bar0 = pci_iomap(pdev, 0, 0);
    if (!st->bar0) {
        ret = -ENOMEM;
        goto err_release_region;
    }
    
    /* Setup DMA mask */
    ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
    if (ret) {
        goto err_iounmap;
    }

    /* Allocate DMA memory */
    st->dma_buf = dma_alloc_coherent(&pdev->dev, DMA_BUF_SIZE, &st->dma_handle, GFP_KERNEL);
    if (!st->dma_buf) {
        ret = -ENOMEM;
        goto err_iounmap;
    }

    /* Acitivate MSI Interrupts */
    ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSI);
    if (ret < 0) {
        dev_err(&pdev->dev, "cannot allocate MSI vector: %d\n", ret);
        goto err_free_dma;
    }
    
    st->irq = pci_irq_vector(pdev, 0);

    /* Register IRQ handler */
    ret = request_irq(st->irq, custompci_irq_handler, 0, DRV_NAME, st);
    if (ret) {
        dev_err(&pdev->dev, "cannot request IRQ %d: %d\n", st->irq, ret);
        goto err_free_irq_vectors;
    }
    
	/* Setup misc device */
    st->miscdev.minor = MISC_DYNAMIC_MINOR;
    st->id = ida_alloc(&drv_pci_ida, GFP_KERNEL);
    if (st->id < 0) {
        ret = st->id;
        goto err_free_irq;
    }
    snprintf(st->name, sizeof(st->name), DRV_NAME "%d", st->id);
    st->miscdev.name = st->name;
    st->miscdev.fops = &miscpci_fops;
    st->miscdev.parent = &pdev->dev;

    ret = misc_register(&st->miscdev);
    if (ret) {
        dev_err(&pdev->dev, "cannot register misc device: %d\n", ret);
        goto err_free_id;
    }

    dev_info(&pdev->dev, "probed, DMA buffer, MSI and /dev/%s ready\n", st->name);
    return 0;

err_free_id:
    ida_free(&drv_pci_ida, st->id);
err_free_irq:
    free_irq(st->irq, st);
err_free_irq_vectors:
    pci_free_irq_vectors(pdev);
err_free_dma:
    dma_free_coherent(&pdev->dev, DMA_BUF_SIZE, st->dma_buf, st->dma_handle);
err_iounmap:
    pci_iounmap(pdev, st->bar0);
err_release_region:
    pci_release_region(pdev, 0);
err_disable:
    pci_disable_device(pdev);
    return ret;
}

//##################################################
// @name 		custompci_remove
// @param		pci_dev
// @return		void
// @function	gets called for each list entry when rmmod,
//				deallocates and clean up
//-------------------------------------------------
static void custompci_remove(struct pci_dev *pdev)
{
    struct dev_state *st = pci_get_drvdata(pdev);

    misc_deregister(&st->miscdev);
    ida_free(&drv_pci_ida, st->id);
    
    free_irq(st->irq, st);
    pci_free_irq_vectors(pdev);
    dma_free_coherent(&pdev->dev, DMA_BUF_SIZE, st->dma_buf, st->dma_handle);
    
    if (st->bar0)
        pci_iounmap(pdev, st->bar0);

    pci_release_region(pdev, 0);
    pci_clear_master(pdev);
    pci_disable_device(pdev);

    dev_info(&pdev->dev, "removed /dev/%s\n", st->name);
}

//############################
// pci device table			##
//############################
static const struct pci_device_id qemupci_ids[] = {
    { PCI_DEVICE(CUSTOM_PCI_VENDOR_ID, CUSTOM_PCI_DEVICE_ID) },
    { 0 }
};
MODULE_DEVICE_TABLE(pci, qemupci_ids);

//############################
// function mapping			##
//############################
static struct pci_driver custompci_driver = {
    .name = DRV_NAME,
    .id_table = qemupci_ids,
    .probe = custompci_probe,
    .remove = custompci_remove,
};

//##################################################
// @name 		custompci_init
// @param		void
// @return		void
// @function	gets called when module is added
//-------------------------------------------------
static int __init custompci_init(void)
{
    pr_info(DRV_NAME ": loading driver\n");
    return pci_register_driver(&custompci_driver);
}

//##################################################
// @name 		custompci_exit
// @param		void
// @return		void
// @function	gets called when rmmod
//-------------------------------------------------
static void __exit custompci_exit(void)
{
    pr_info(DRV_NAME ": unloading driver\n");
    pci_unregister_driver(&custompci_driver);
}

module_init(custompci_init);
module_exit(custompci_exit);
