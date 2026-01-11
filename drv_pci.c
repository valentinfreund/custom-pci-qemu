#include <linux/module.h>
#include <linux/pci.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>

#define DRV_NAME "drv_pci"
#define BUF_SIZE 256
#define VENDOR_ID 0x1234
#define DEVICE_ID 0x5678


//-------------------------------------------------
//-------------------------------------------------
MODULE_DESCRIPTION("PCI driver: init, probe, remove and exit should be shown in kernel msg");
MODULE_AUTHOR("Valentin Freundorfer");
MODULE_LICENSE("GPL");
//-------------------------------------------------
//-------------------------------------------------


//############################
// state container			##
//############################
struct dev_state {
    struct pci_dev *pdev;
    void __iomem *bar0;

    char buffer[BUF_SIZE];
    size_t buffer_len;

    struct miscdevice miscdev;
};

//##################################################
// @name 	    miscpci_read
// @param		file, char, length, offset
// @return		length of transferred data
// @function	reads data from kernel space to 
//              user space
//-------------------------------------------------
static ssize_t miscpci_read(struct file *file, char __user *ubuf, size_t len, loff_t *off)
{
    struct dev_state *st = container_of(file->private_data,
                                          struct dev_state, miscdev);

    if (*off >= st->buffer_len)
        return 0; // EOF

    if (len > st->buffer_len - *off)
        len = st->buffer_len - *off;

    if (copy_to_user(ubuf, st->buffer + *off, len))
        return -EFAULT;

    *off += len; //don't forget the offset
    return len;
}

//##################################################
// @name 	    miscpci_write
// @param		file, char, length, offset
// @return		length of copied data
// @function	reads data from user space to 
//              kernel space into buffer
//-------------------------------------------------
static ssize_t miscpci_write(struct file *file, const char __user *ubuf, size_t len, loff_t *off)
{
    struct dev_state *st = container_of(file->private_data,
                                          struct dev_state, miscdev);

    if (len > BUF_SIZE)
        return -EINVAL;

    if (copy_from_user(st->buffer, ubuf, len))
        return -EFAULT;

    st->buffer_len = len;
    return len;
}

//############################
// file operations    		##
//############################
static const struct file_operations miscpci_fops = {
    .owner = THIS_MODULE,
    .read  = miscpci_read,
    .write = miscpci_write,
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

    /* Allocate state container */
    st = devm_kzalloc(&pdev->dev, sizeof(*st), GFP_KERNEL);
    if (!st) {
        ret = -ENOMEM;
        goto err_disable;
    }

    pci_set_drvdata(pdev, st);
    st->pdev = pdev;

    /* Request and map BAR0 */
    ret = pci_request_region(pdev, 0, DRV_NAME);
    if (ret)
        goto err_disable;

    st->bar0 = pci_iomap(pdev, 0, 0);
    if (!st->bar0) {
        ret = -ENOMEM;
        goto err_release_region;
    }
/* Setup misc device */
    st->miscdev.minor = MISC_DYNAMIC_MINOR;
    st->miscdev.name = DRV_NAME;
    st->miscdev.fops = &miscpci_fops;

    ret = misc_register(&st->miscdev);
    if (ret)
        goto err_iounmap;

    pr_info(DRV_NAME ": PCI device probed and miscdev registered\n");
    return 0;

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
    
    if (st->bar0)
        pci_iounmap(pdev, st->bar0);

    pci_release_region(pdev, 0);
    pci_disable_device(pdev);

    pr_info(DRV_NAME ": device removed\n");
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

