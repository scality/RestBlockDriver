#include <linux/module.h>    // included for all kernel modules
#include <linux/kernel.h>    // included for KERN_INFO
#include <linux/device.h>
#include <linux/blkdev.h>

#include "dewb.h"

/********************************************************************
 * /sys/block/dewb?/
 *                   dewb_debug	 Sets verbosity
 *                   dewb_urls	 Gets device CDMI url
 *                   dewb_name   Gets device's on-storage filename
 *		     dewb_size   Gets device size
 *******************************************************************/
static ssize_t attr_debug_store(struct device *dv, 
				struct device_attribute *attr, 
				const char *buff, size_t count)
{
	struct gendisk *disk	  = dev_to_disk(dv);
	struct dewb_device_s *dev = disk->private_data;
        char *end;
        long new;
	int val;

	/* XXX: simple_strtol is an obsolete function
	 * TODO: replace it with kstrtol wich can returns:
	 *       0 on success, -ERANGE on overflow and -EINVAL on parsing error
	 */
	new = simple_strtol(buff, &end, 0);
	if (end == buff || new > INT_MAX || new < INT_MIN) {
		DEWB_LOG(KERN_WARNING, "attr_debug_store: Invalid debug value");
		return -EINVAL;
	}
	val = (int) new;
	if (val >= 0 && val <= 7) {
		dev->debug.level = val;
		if (dev->debug.level == DEWB_LOG_DEBUG)
			DEWB_LOG(KERN_DEBUG, "attr_debug_store: Setting Log level to %d for device %s", 
				val, dev->name);
	}
	else
		DEWB_LOG(KERN_WARNING, "attr_debug_store: Invalid debug value (%d) for device %s in sysfs", 
			val, dev->name);

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
	
	//snprintf(buff, PAGE_SIZE, "%s\n", dev->thread_cdmi_desc[0].url);
	snprintf(buff, PAGE_SIZE, "%s\n", dev->thread_cdmi_desc[0]->url);

	return strlen(buff);
}

static ssize_t attr_disk_name_show(struct device *dv,
				   struct device_attribute *attr, char *buff)
{
	struct gendisk *disk	  = dev_to_disk(dv);
	struct dewb_device_s *dev = disk->private_data;

	//snprintf(buff, PAGE_SIZE, "%s\n", kbasename(dev->thread_cdmi_desc[0].url));
	snprintf(buff, PAGE_SIZE, "%s\n", kbasename(dev->thread_cdmi_desc[0]->url));

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
static DEVICE_ATTR(dewb_name, S_IRUGO, &attr_disk_name_show, NULL);
static DEVICE_ATTR(dewb_size, S_IRUGO, &attr_disk_size_show, NULL);


/************************************************************************
 * /sys/class/dewp/
 *                   create     Create the volume's file on the storage
 *                   destroy    Removes the volume's file on the storage
 *                   attach	Attach a volume as a new dewp device
 *                   detach	Detaches (remove from the system)
 *				the requested volume (or device)
 ***********************************************************************/

static struct class *class_dewb;		/* /sys/class/dewp */


static void class_dewb_release(struct class *cls)
{
	kfree(cls);
}

static ssize_t class_dewb_create_show(struct class *c, struct class_attribute *attr,
				      char *buf)
{
	(void)c;
	(void)attr;

	snprintf(buf, PAGE_SIZE, "# Usage: echo 'VolumeName size(bytes)' > create\n");

	return strlen(buf);
}

static ssize_t class_dewb_create_store(struct class *c,
				struct class_attribute *attr,
				const char *buf, size_t count)
{
	ssize_t ret = 0;
	char filename[DEWB_URL_SIZE + 1];
	const char *tmp = buf;
	unsigned long long size = 0;
	size_t len = 0;

	(void)c;
	(void)attr;

	DEWB_INFO("Creating volume with params: %s    (%lu)", buf, count);

	/* Ensure we have two space-separated args + only 1 space */
	tmp = strrchr(buf, ' ');
	if (tmp == NULL || tmp != strchr(buf, ' '))
	{
		DEWB_ERROR("More than one space in arguments: tmp=%p,"
                           "strchr=%p", tmp, strchr(buf, ' '));
		ret = -EINVAL;
		goto out;
	}

	len = (size_t)(tmp - buf);
	if ((len == 0) || (len >= DEWB_URL_SIZE)) {
		DEWB_ERROR("len=%lu", len);
		ret = -EINVAL;
		goto out;
	}

	memcpy(filename, buf, len);
	if (filename[len - 1] == '\n')
		filename[len - 1] = 0;
	else
		filename[len] = 0;

	DEWB_INFO("Trying to create device '%s' ...", filename);

	while (*tmp != 0 && *tmp == ' ')
		tmp++;

	/* Check that the second arg is numeric-only */
	ret = kstrtoull(tmp, 10, &size);
	if (ret != 0)
		goto out;

	DEWB_INFO("... of %llu bytes", size);

	ret = dewb_device_create(filename, size);
	if (ret != 0)
	{
		goto out;
	}

	ret = count;

out:
	return ret;
}

static ssize_t class_dewb_extend_show(struct class *c, struct class_attribute *attr,
				      char *buf)
{
	(void)c;
	(void)attr;

	snprintf(buf, PAGE_SIZE,
		 "The new size must be greater than the current size.\n"
		 "# Usage: echo 'VolumeName size(bytes)' > extend\n");

	return strlen(buf);
}

static ssize_t class_dewb_extend_store(struct class *c,
				       struct class_attribute *attr,
				       const char *buf, size_t count)
{
	ssize_t ret = 0;
	char filename[DEWB_URL_SIZE + 1];
	const char *tmp = buf;
	unsigned long long size = 0;
	size_t len = 0;

	(void)c;
	(void)attr;

	DEWB_INFO("Extending volume with params: %s    (%lu)", buf, count);

	/* Ensure we have two space-separated args + only 1 space */
	tmp = strrchr(buf, ' ');
	if (tmp == NULL || tmp != strchr(buf, ' '))
	{
		DEWB_ERROR("More than one space in arguments: tmp=%p,"
                           "strchr=%p", tmp, strchr(buf, ' '));
		ret = -EINVAL;
		goto out;
	}

	len = (size_t)(tmp - buf);
	if ((len == 0) || (len >= DEWB_URL_SIZE)) {
		DEWB_ERROR("len=%lu", len);
		ret = -EINVAL;
		goto out;
	}

	memcpy(filename, buf, len);
	if (filename[len - 1] == '\n')
		filename[len - 1] = 0;
	else
		filename[len] = 0;

	DEWB_INFO("Trying to extend device '%s' ...", filename);

	while (*tmp != 0 && *tmp == ' ')
		tmp++;

	/* Check that the second arg is numeric-only */
	ret = kstrtoull(tmp, 10, &size);
	if (ret != 0)
		goto out;

	DEWB_INFO("... of %llu bytes", size);

	ret = dewb_device_extend(filename, size);
	if (ret != 0)
	{
		goto out;
	}

	ret = count;

out:
	return ret;
}

static ssize_t class_dewb_destroy_show(struct class *c, struct class_attribute *attr,
				       char *buf)
{
	(void)c;
	(void)attr;

	snprintf(buf, PAGE_SIZE, "# Usage: echo VolumeName > destroy\n");

	return strlen(buf);
}

static ssize_t class_dewb_destroy_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	ssize_t ret = 0;
	char filename[DEWB_URL_SIZE + 1];

	(void)c;
	(void)attr;

	/* Sanity check URL size */
	if ((count == 0) || (count >= DEWB_URL_SIZE)) {
		DEWB_ERROR("Url too long");
		ret =-ENOMEM;
		goto out;
	}
	
	memcpy(filename, buf, count);
	if (filename[count - 1] == '\n')
		filename[count - 1] = 0;
	else
		filename[count] = 0;

	DEWB_INFO("Trying to destroy device '%s'", filename);
	ret = dewb_device_destroy(filename);
	if (ret != 0)
	{
		goto out;
	}

	ret = count;

out:
	return ret;
}

static ssize_t class_dewb_attach_show(struct class *c, struct class_attribute *attr,
				      char *buf)
{
	(void)c;
	(void)attr;

	snprintf(buf, PAGE_SIZE, "# Usage: echo VolumeName > attach\n");

	return strlen(buf);
}

static ssize_t class_dewb_attach_store(struct class *c,
			struct class_attribute *attr,
			const char *buf, size_t count)
{
	int ret;
	char filename[DEWB_URL_SIZE + 1];

	/* Sanity check URL size */
	if ((count == 0) || (count > DEWB_URL_SIZE)) {
		DEWB_ERROR("Url too long");
		ret =-ENOMEM;
		goto out;
	}
	
	memcpy(filename, buf, count);
	if (filename[count - 1] == '\n')
		filename[count - 1] = 0;
	else
		filename[count] = 0;

	ret = dewb_device_attach(filename);
	if (ret == 0)
		return count;
out:
	return ret;
}

static ssize_t class_dewb_detach_show(struct class *c, struct class_attribute *attr,
				      char *buf)
{
	(void)c;
	(void)attr;

	snprintf(buf, PAGE_SIZE, "# Usage: echo VolumeName > detach\n");

	return strlen(buf);
}

static ssize_t class_dewb_detach_store(struct class *c,
				struct class_attribute *attr,
				const char *buf,
				size_t count)
{
	int ret;
	char filename[DEWB_URL_SIZE + 1];

	/* Sanity check URL size */
	if ((count == 0) || (count > DEWB_URL_SIZE)) {
		DEWB_ERROR("Url too long");
		return -ENOMEM;
	}
	
	memcpy(filename, buf, count);
	if (filename[count - 1] == '\n')
		filename[count - 1] = 0;
	else
		filename[count] = 0;

	ret = dewb_device_detach_by_name(filename);
	if (ret == 0)
		return count;

	return ret;
}

static ssize_t class_dewb_addmirror_show(struct class *c, struct class_attribute *attr,
					 char *buf)
{
	(void)c;
	(void)attr;

	snprintf(buf, PAGE_SIZE, "# Usage: echo mirror_url1,...,mirror_urlN > add_mirrors\n");

	return strlen(buf);
}

static ssize_t class_dewb_addmirror_store(struct class *c,
					  struct class_attribute *attr,
					  const char *buf,
					  size_t count)
{
	ssize_t		ret = 0;
	char		url[DEWB_URL_SIZE+1];
	const char	*tmp = buf;
	const char	*tmpend = tmp;
	int		errcount = 0;

	while (tmp != NULL)
	{
		while (*tmp != 0 && *tmp == ',')
			++tmp;

		tmpend = strchr(tmp, ',');
		if (tmpend != NULL)
		{
			memcpy(url, tmp, (tmpend - tmp));
			url[(tmpend - tmp)] = 0;
		}
		else
		{
			// Strip the ending newline
			tmpend = tmp;
			while (*tmpend && *tmpend != '\n')
				tmpend++;

			if ((tmpend - tmp) > DEWB_URL_SIZE)
			{
				DEWB_ERROR("Url too big: '%s'", tmp);
				ret = -EINVAL;
				goto end;
			}
			memcpy(url, tmp, (tmpend - tmp));
			url[(tmpend - tmp)] = 0;
			tmpend = NULL;
		}
		url[DEWB_URL_SIZE] = 0;

		ret = dewb_mirror_add(url);
		if (ret < 0)
			errcount += 1;

		tmp = tmpend;
	}

	ret = count;
	if (errcount > 0)
	{
		DEWB_ERROR("Could not add every mirror to driver.");
		ret = -EINVAL;
	}

end:
	return ret;
}

static ssize_t class_dewb_removemirror_show(struct class *c, struct class_attribute *attr,
					    char *buf)
{
	(void)c;
	(void)attr;

	snprintf(buf, PAGE_SIZE, "# Usage: echo mirror_url1,...,mirror_urlN > remove_mirrors\n");

	return strlen(buf);
}

static ssize_t class_dewb_removemirror_store(struct class *c,
					     struct class_attribute *attr,
					     const char *buf,
					     size_t count)
{
	ssize_t		ret = 0;
	char		url[DEWB_URL_SIZE];
	const char	*tmp = buf;
	const char	*tmpend = tmp;

	while (tmp != NULL)
	{
		while (*tmp != 0 && *tmp == ',')
			++tmp;

		tmpend = strchr(tmp, ',');
		if (tmpend != NULL)
		{
			memcpy(url, tmp, (tmpend - tmp));
			url[(tmpend - tmp)] = 0;
		}
		else
		{
			// Strip the ending newline
			tmpend = tmp;
			while (*tmpend && *tmpend != '\n')
				tmpend++;

			if ((tmpend - tmp) > DEWB_URL_SIZE)
			{
				DEWB_ERROR("Url too big: '%s'", tmp);
				ret = -EINVAL;
				goto end;
			}
			memcpy(url, tmp, (tmpend - tmp));
			url[(tmpend - tmp)] = 0;
			tmpend = NULL;
		}

		ret = dewb_mirror_remove(url);
		if (ret < 0)
		{
			goto end;
		}

		tmp = tmpend;

	}

	ret = count;

end:
	return ret;
}

static ssize_t class_dewb_mirrors_show(struct class *c, struct class_attribute *attr,
				       char *buf)
{
	ssize_t	ret = 0;

	(void)c;
	(void)attr;

	ret = dewb_mirrors_dump(buf, PAGE_SIZE);

	return ret;
}


void dewb_sysfs_device_init(dewb_device_t *dev)
{
	device_create_file(disk_to_dev(dev->disk), &dev_attr_dewb_debug);
	device_create_file(disk_to_dev(dev->disk), &dev_attr_dewb_urls);
	device_create_file(disk_to_dev(dev->disk), &dev_attr_dewb_name);
	device_create_file(disk_to_dev(dev->disk), &dev_attr_dewb_size);
}

static struct class_attribute class_dewb_attrs[] = {
	__ATTR(attach,		0600, class_dewb_attach_show, class_dewb_attach_store),
	__ATTR(detach,		0600, class_dewb_detach_show, class_dewb_detach_store),
	__ATTR(create,		0600, class_dewb_create_show, class_dewb_create_store),
	__ATTR(extend,		0600, class_dewb_extend_show, class_dewb_extend_store),
	__ATTR(destroy,		0600, class_dewb_destroy_show, class_dewb_destroy_store),
	__ATTR(add_mirrors,	0600, class_dewb_addmirror_show, class_dewb_addmirror_store),
	__ATTR(remove_mirrors,	0600, class_dewb_removemirror_show, class_dewb_removemirror_store),
	__ATTR(mirrors,		0400, class_dewb_mirrors_show, NULL),
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
	class_dewb->owner	  = THIS_MODULE;
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
