#include <linux/module.h>
#include <linux/pci.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/idr.h>

//device information
#define DRV_NAME "drv_pci"
#define VENDOR_ID 0x1234
#define DEVICE_ID 0x5678
//register address information
#define REG_IRQ_TRIGGER   0x00
#define REG_DMA_ADDR_LOW  0x04
#define REG_DMA_ADDR_HIGH 0x08
#define REG_DMA_SIZE      0x0C
#define REG_DMA_CMD       0x10
//dma information
#define CMD_DMA_READ      0x01
#define CMD_DMA_WRITE     0x02
#define DMA_BUF_SIZE      4096
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
// @param		device state, command, buffer offset, length
// @return		0 or error code
// @function	programs one DMA transfer of the region
//              [off, off + len) of the DMA buffer and
//              waits for the completion interrupt.
//              Caller must hold st->lock.
//-------------------------------------------------
static int run_dma(struct dev_state *st, u32 cmd, loff_t off, size_t len)
{
    dma_addr_t addr = st->dma_handle + off;
    long ret;

    reinit_completion(&st->dma_comp);

    iowrite32(lower_32_bits(addr), st->bar0 + REG_DMA_ADDR_LOW);
    iowrite32(upper_32_bits(addr), st->bar0 + REG_DMA_ADDR_HIGH);
    iowrite32((u32)len, st->bar0 + REG_DMA_SIZE);
    iowrite32(cmd, st->bar0 + REG_DMA_CMD);

    ret = wait_for_completion_interruptible_timeout(&st->dma_comp,
                                                    msecs_to_jiffies(DMA_TIMEOUT_MS));
    if (ret == 0) {
        dev_err(&st->pdev->dev, "DMA timeout (cmd %u, %zu bytes)\n", cmd, len);
        return -ETIMEDOUT;
    }
    if (ret < 0)
        return ret;     //interrupted by a signal

    dev_dbg(&st->pdev->dev, "DMA done (cmd %u, %zu bytes at offset %lld)\n", cmd, len, off);
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

    ret = run_dma(st, CMD_DMA_WRITE, *off, len);
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

    ret = run_dma(st, CMD_DMA_READ, *off, len);
    if (ret)
        goto out;

    *off += len;
    ret = len;
out:
    mutex_unlock(&st->lock);
    return ret;
}

//############################
// file operations    		##
//############################
static const struct file_operations miscpci_fops = {
    .owner = THIS_MODULE,
    .read  = miscpci_dma_read,
    .write = miscpci_dma_write,
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
    { PCI_DEVICE(VENDOR_ID, DEVICE_ID) },
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
