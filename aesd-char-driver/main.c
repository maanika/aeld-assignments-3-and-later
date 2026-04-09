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
int aesd_major = 0; // use dynamic major
int aesd_minor = 0;

MODULE_AUTHOR("Maanika Kenneth Koththigoda");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

static int aesd_open(struct inode *inode, struct file *filp)
{
	PDEBUG("open");
	// handle open
	struct aesd_dev *dev;
	dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
	filp->private_data = dev; /* for other methods */
	return 0;
}

static int aesd_release(struct inode *inode, struct file *filp)
{
	PDEBUG("release");
	// handle release
	return 0;
}

static ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	PDEBUG("read %zu bytes with offset %lld", count, *f_pos);

	ssize_t retval = -ENOMEM;
	struct aesd_dev *dev = (struct aesd_dev *)filp->private_data;

    if (dev->read_info.complete) {
        dev->read_info.complete = false;
        return 0;
    }

	// handle read
	size_t offset_rtn = 0;

    // should this be at the top (before private_data gets assigned) ????? TBC
	if (mutex_lock_interruptible(&dev->lock) != 0) {
		return -ERESTARTSYS;
	}

    dev->read_info.read_len = 0;
    size_t read_offset = 0;

	while (dev->read_info.read_len < count) {
		struct aesd_buffer_entry *read_entry =
			aesd_circular_buffer_find_entry_offset_for_fpos(
				&(dev->circular_buffer), *f_pos + read_offset, // something buggy here
				&offset_rtn);
		if (read_entry == NULL) {
            // end of circular buffer
			goto exit;
		}

		const size_t len = min(count - dev->read_info.read_len, read_entry->size);
		if (len == 0) {
			goto exit;
		}

		PDEBUG("Found entry %s, attempting to copy %zu bytes to user buffer",
		       read_entry->buffptr, len);
		if (copy_to_user(buf + dev->read_info.read_len, read_entry->buffptr, len)) {
			goto exit;
		}

		PDEBUG("Saved entry %s", read_entry->buffptr);

		dev->read_info.read_len += len; // offset starts from 0
        read_offset += (len - 1);

		PDEBUG("len total %zu", dev->read_info.read_len);
	}

exit:
    retval = dev->read_info.read_len;
    dev->read_info.complete = true;

	mutex_unlock(&dev->lock);

	return retval;
}

static void write_circular_buffer_packet(struct aesd_circular_buffer *buffer, const char *writestr,
					 const size_t count)
{
	struct aesd_buffer_entry entry = {
		.buffptr = writestr,
		.size = count,
	};

	PDEBUG("Adding %s (len %zu) to circular buffer", entry.buffptr, entry.size);

	const char *free_bufferptr = aesd_circular_buffer_add_entry(buffer, &entry);
	if (free_bufferptr != NULL) {
		kfree(free_bufferptr);
	}
}

static void print_circular_buffer(struct aesd_circular_buffer *buffer)
{
	PDEBUG("Priniting current circular buffer");
	uint8_t index = 0;
	struct aesd_buffer_entry *entry;
	AESD_CIRCULAR_BUFFER_FOREACH(entry, buffer, index)
	{
		if (entry->buffptr == NULL) {
			break;
		}
		PDEBUG("index: %d, value: %s, size: %zu", index, entry->buffptr, entry->size);
	}
}

static ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	// f_pos is ignored, we will be writing to the circular buffer's implentation of the next
	// free slot.

	struct aesd_dev *dev = (struct aesd_dev *)filp->private_data;

	if (mutex_lock_interruptible(&dev->lock) != 0) {
		return -ERESTARTSYS;
	}

	PDEBUG("write %zu bytes with offset %lld (ignored)", count, *f_pos);

	ssize_t retval = 0;
	const size_t str_len = count + 1; // +1 for NULL terminating

	const bool new_write = (dev->write_info.str == NULL) & (!dev->write_info.partial_flag) &
			       (dev->write_info.str == 0);

	if (new_write) {
		PDEBUG("New write");
		dev->write_info.str = kmalloc(str_len, GFP_KERNEL);
	} else {
		PDEBUG("Conituning a partial write");
		// we already allocated a byte for NULL terminator.
		dev->write_info.str =
			krealloc(dev->write_info.str, dev->write_info.len + count, GFP_KERNEL);
	}

	if (dev->write_info.str == NULL) {
		retval = -ENOMEM;
		goto exit;
	}
	memset(dev->write_info.str + dev->write_info.len, 0, str_len);

	if (copy_from_user(dev->write_info.str + dev->write_info.len, buf, count)) {
		kfree(dev->write_info.str);
		retval = -EFAULT;
		goto exit;
	}

	// update len after copying from user buffer (new offset)
	dev->write_info.len += count;

	// -1 because arrary indexing starts from 0.
	if (dev->write_info.str[dev->write_info.len - 1] == '\n') {
		PDEBUG("Write string complete");
		write_circular_buffer_packet(&(dev->circular_buffer), dev->write_info.str, str_len);

		// reset write info
		dev->write_info.str = NULL;
		dev->write_info.len = 0;
		dev->write_info.partial_flag = false;

	} else {
		// set write info for next write
		dev->write_info.partial_flag = true;
	}

	retval = count;

exit:
	print_circular_buffer(&(dev->circular_buffer));
	mutex_unlock(&dev->lock);
	return retval;
}

struct file_operations aesd_fops = {.owner = THIS_MODULE,
				    .read = aesd_read,
				    .write = aesd_write,
				    .open = aesd_open,
				    .release = aesd_release};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
	int err, devno = MKDEV(aesd_major, aesd_minor);

	cdev_init(&dev->cdev, &aesd_fops);
	dev->cdev.owner = THIS_MODULE;
	dev->cdev.ops = &aesd_fops;
	err = cdev_add(&dev->cdev, devno, 1);
	if (err) {
		printk(KERN_ERR "Error %d adding aesd cdev", err);
	}
	return err;
}

static int aesd_init_module(void)
{
	dev_t dev = 0;
	int result;
	result = alloc_chrdev_region(&dev, aesd_minor, 1, "aesdchar");
	aesd_major = MAJOR(dev);
	if (result < 0) {
		printk(KERN_WARNING "Can't get major %d\n", aesd_major);
		return result;
	}
	memset(&aesd_device, 0, sizeof(struct aesd_dev));

	// initialize the AESD specific portion of the device
	PDEBUG("Initializing AESD device...");

	// initialise read info
	aesd_device.read_info.read_len = 0;
    aesd_device.read_info.complete = false;

	// initialise write info
	aesd_device.write_info.str = NULL;
	aesd_device.write_info.len = 0;
	aesd_device.write_info.partial_flag = false;

	aesd_circular_buffer_init(&aesd_device.circular_buffer);
	mutex_init(&aesd_device.lock);

	result = aesd_setup_cdev(&aesd_device);

	if (result) {
		unregister_chrdev_region(dev, 1);
	}
	return result;
}

static void free_circular_buffer(struct aesd_circular_buffer *buffer)
{
	uint8_t index = 0;
	struct aesd_buffer_entry *entry;
	AESD_CIRCULAR_BUFFER_FOREACH(entry, buffer, index)
	{
		kfree(entry->buffptr);
	}
}

static void aesd_cleanup_module(void)
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
