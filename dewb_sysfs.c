#include <linux/module.h>    // included for all kernel modules
#include <linux/kernel.h>    // included for KERN_INFO
#include <linux/device.h>
#include <linux/blkdev.h>

#include "dewb.h"

/********************************************************************
 * /sys/block/dewb?/
 *                   dewb_debug	 Sets verbosity
 *                   dewb_urls	 Gets device CDMI url
 *		     dewb_size   Gets device size
 *		     dewb_remove Removes the device
 *******************************************************************/
static ssize_t attr_debug_store(struct device *dv, 
				struct device_attribute *attr, 
				const char *buff, size_t count)
{
	struct gendisk *disk	  = dev_to_disk(dv);
	struct dewb_device_s *dev = disk->private_data;

	if (count == 0)
		return count;

	disk = dev_to_disk(dv);
	if (*buff == '0')
		dev->debug.level = 0;
	else
		dev->debug.level = 1;
	
	DEWB_INFO("Setting the debug level to %d", dev->debug.level);
	return count;
}

static ssize_t attr_debug_show(struct device *dv, 
			struct device_attribute *attr, char *buff)
{
	struct gendisk *disk	  = dev_to_disk(dv);
	struct dewb_device_s *dev = disk->private_data;
	
	snprintf(buff, PAGE_SIZE, "%d\n", dev->debug.level);

	return strlen(buff);	
}


static ssize_t attr_urls_show(struct device *dv, 
			struct device_attribute *attr, char *buff)
{
	struct gendisk *disk	  = dev_to_disk(dv);
	struct dewb_device_s *dev = disk->private_data;
	
	snprintf(buff, PAGE_SIZE, "%s\n", dev->thread_cdmi_desc[0].url);

	return strlen(buff);
}

static ssize_t attr_disk_size_show(struct device *dv, 
				struct device_attribute *attr, char *buff)
{
	struct gendisk *disk	  = dev_to_disk(dv);
	struct dewb_device_s *dev = disk->private_data;
	
	snprintf(buff, PAGE_SIZE, "%llu\n", dev->disk_size);

	return strlen(buff);
}

static DEVICE_ATTR(dewb_debug, S_IWUSR | S_IRUGO, &attr_debug_show, &attr_debug_store);
static DEVICE_ATTR(dewb_urls, S_IRUGO, &attr_urls_show, NULL);
static DEVICE_ATTR(dewb_size, S_IRUGO, &attr_disk_size_show, NULL);


/********************************************************************
 * /sys/class/dewp/
 *                   add	Create a new dewp device
 *                   remove	Remove the last created device
 *******************************************************************/

static struct class *class_dewb;		/* /sys/class/dewp */


static void class_dewb_release(struct class *cls)
{
	kfree(cls);
}

static ssize_t class_dewb_add_show(struct class *c, struct class_attribute *attr,
                                   char *buf)
{
	(void)c;
	(void)attr;

	/* Get module reference */
	if (!try_module_get(THIS_MODULE))
		return -ENODEV;

	snprintf(buf, PAGE_SIZE, "# Usage: echo URL > add\n");

	module_put(THIS_MODULE);
	return strlen(buf);
}

static ssize_t class_dewb_add_store(struct class *c,
			struct class_attribute *attr,
			const char *buf, size_t count)
{
	int ret;
	char url[DEWB_URL_SIZE + 1];

	/* Get module reference */
	if (!try_module_get(THIS_MODULE))
		return -ENODEV;

	/* Sanity check URL size */
	if ((count == 0) || (count > DEWB_URL_SIZE)) {
		DEWB_ERROR("Url too long");
		ret =-ENOMEM;
		goto out;
	}
	
	memcpy(url, buf, count);
	if (url[count - 1] == '\n')
		url[count - 1] = 0;
	else
		url[count] = 0;

	ret = dewb_device_add(url);
	if (ret == 0)
		return count;
out:
	module_put(THIS_MODULE);
	return ret;
}

static ssize_t class_dewb_remove_show(struct class *c, struct class_attribute *attr,
                                      char *buf)
{
	(void)c;
	(void)attr;

	/* Get module reference */
	if (!try_module_get(THIS_MODULE))
		return -ENODEV;

	snprintf(buf, PAGE_SIZE, "# Usage: echo device_name > remove\n");

	module_put(THIS_MODULE);
	return strlen(buf);
}

static ssize_t class_dewb_remove_store(struct class *c,
				struct class_attribute *attr,
				const char *buf,
				size_t count)
{
	int dev_id;
	int ret;

	/* Sanity check */
	
	/* 123456 7*/
	/* dewba\0\n */
	if (count < 6) {
		DEWB_ERROR("Unable to remove :unknown device");
		return -EINVAL;
	}

	/* Determine device ID to remove */
	dev_id = buf[4] - 'a';
	if ((dev_id < 0) || (dev_id >= DEV_MAX)) {

		DEWB_ERROR("Unable to remove: unknown device dewb%c ", buf[4]);
		return -EINVAL;
	}	

	ret = dewb_device_remove_by_id(dev_id);
	if (ret == 0)
		module_put(THIS_MODULE);

	return (ret < 0) ? ret : count;
}

void dewb_sysfs_device_init(dewb_device_t *dev)
{
	device_create_file(disk_to_dev(dev->disk), &dev_attr_dewb_debug);
	device_create_file(disk_to_dev(dev->disk), &dev_attr_dewb_urls);
	device_create_file(disk_to_dev(dev->disk), &dev_attr_dewb_size);
}

static struct class_attribute class_dewb_attrs[] = {
	__ATTR(add,	0600, class_dewb_add_show, class_dewb_add_store),
	__ATTR(remove,	0600, class_dewb_remove_show, class_dewb_remove_store),
	__ATTR_NULL
};

int dewb_sysfs_init(void)
{
	int ret = 0;

	/*
	 * create control files in sysfs
	 * /sys/class/dewb/...
	 */
	class_dewb = kzalloc(sizeof(*class_dewb), GFP_KERNEL);
	if (!class_dewb)
		return -ENOMEM;

	class_dewb->name	  = DEV_NAME;
	class_dewb->owner         = THIS_MODULE;
	class_dewb->class_release = class_dewb_release;
	class_dewb->class_attrs   = class_dewb_attrs;

	ret = class_register(class_dewb);
	if (ret) {
		kfree(class_dewb);
		class_dewb = NULL;
		DEWB_ERROR("failed to create class dewb");
		return ret;
	}

	return 0;
}

void dewb_sysfs_cleanup(void)
{
	if (class_dewb)
		class_destroy(class_dewb);
	class_dewb = NULL;

}
