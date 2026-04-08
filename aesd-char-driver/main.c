/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/string.h>
#include <linux/fs.h> // file_operations
#include "aesdchar.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Maanika Kenneth Koththigoda");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    // handle open
	struct aesd_dev *dev;
	dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
	filp->private_data = dev; /* for other methods */
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    // handle release
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;

    PDEBUG("read %zu bytes with offset %lld", count, *f_pos);

    // handle read
    PDEBUG("Reading ...");
    
    retval = count;

    return retval;
}

static void write_circular_buffer_packet(struct aesd_circular_buffer *buffer,
                                         const char *writestr, const size_t count)
{
    struct aesd_buffer_entry entry = {
        .buffptr = writestr,
        .size = count,
    };

    PDEBUG("Adding %s (len %d) to circular buffer", entry.buffptr, (int)entry.size);

    const struct aesd_buffer_entry* free_entry = aesd_circular_buffer_add_entry(buffer, &entry);
    if (free_entry != NULL) {
        kfree(free_entry);
    }
}

static void print_circular_buffer(struct aesd_circular_buffer *buffer)
{
    PDEBUG("Priniting current circular buffer");
    uint8_t index = 0;
    struct aesd_buffer_entry *entry;
    AESD_CIRCULAR_BUFFER_FOREACH(entry, buffer, index) {
        if (entry->buffptr == NULL) {
            break;
        }
        PDEBUG("index: %d, value: %s, size: %d", index, entry->buffptr, (int)entry->size);
    }
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    struct aesd_dev *dev = (struct aesd_dev*) filp->private_data;

    if (mutex_lock_interruptible(&dev->lock) != 0) {
        return -ERESTARTSYS;
    }

    PDEBUG("write %zu bytes with offset %lld", count, *f_pos);

    ssize_t retval = 0;
    const size_t str_len = count + 1; // +1 for NULL terminating

    char *writestr = kmalloc(str_len, GFP_KERNEL);
    if (!writestr) {
        retval = -ENOMEM;
        goto exit;
    }
    memset(writestr, 0, str_len);

    if (copy_from_user(writestr, buf, count)) { // copying one less than str_len to keep NULL terminating
        retval = -EFAULT;
        goto exit;
    }

    write_circular_buffer_packet(&(dev->circular_buffer), writestr, str_len);
    retval = count;

exit:
    print_circular_buffer(&(dev->circular_buffer));
    mutex_unlock(&dev->lock);
    return retval;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}

int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    // initialize the AESD specific portion of the device
    PDEBUG("Initializing AESD device...");
    aesd_circular_buffer_init(&aesd_device.circular_buffer);
    mutex_init(&aesd_device.lock);

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

static void free_circular_buffer(struct aesd_circular_buffer *buffer)
{
    uint8_t index = 0;
    struct aesd_buffer_entry *entry;
    AESD_CIRCULAR_BUFFER_FOREACH(entry, buffer, index) {
        kfree(entry->buffptr);
    }
}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    // cleanup AESD specific poritions here as necessary
    PDEBUG("Cleaning up AESD device...");
    free_circular_buffer(&aesd_device.circular_buffer);

    unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
