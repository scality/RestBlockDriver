/*
 * Copyright (C) 2014 SCALITY SA. All rights reserved.
 * http://www.scality.com
 * Copyright (c) 2010 Serge A. Zaitsev
 *
 * This file is part of RestBlockDriver.
 *
 * RestBlockDriver is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * RestBlockDriver is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Foobar.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <linux/module.h>    // included for all kernel modules
#include <linux/kernel.h>    // included for KERN_INFO
#include <linux/device.h>
#include <linux/blkdev.h>
#include <linux/string.h>

#include "dewb.h"


/* Function for parsing params and reading humand readable size format
 * 
 */
int parse_params(char *params, const char *delim, char **param_tbl, int param_nb, int max)
{
	int i;
	int j;
	char *tmp;

	j = 0;
	for (i = 0; i < max && j < param_nb; i++) {
		tmp = strsep(&params, delim);
		if (NULL != tmp && *tmp != '\0') {
			param_tbl[j] = tmp;
			j++;
		}
	}

	return 0;
}
int human_to_bytes(char *size_str, unsigned long long *size)
{
	char h;
	unsigned long long coef;
	int ret;

	coef = 1;
	h = size_str[strlen(size_str) - 1];
	/* get human format if any and set coeff */
	switch (h) {
		case 'G':
			coef = GB;
			size_str[strlen(size_str) - 1] = '\0';
			break;
		case 'M':
			coef = MB;
			size_str[strlen(size_str) - 1] = '\0';
			break;
		case 'k':
			coef = kB;
			size_str[strlen(size_str) - 1] = '\0';
			break;
		default:
			coef = 1;
	}
	/* calculate size */
	ret = kstrtoull(size_str, 10, size);
	if (ret != 0) {
		DEWB_LOG_ERR(dewb_log, "Invalid volume size %s (%llu) (ret: %d)", size_str, *size, ret);
		return -EINVAL;
	}
	*size = *size * coef;

	return 0; 
}

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
		DEWB_LOG_WARN(dev->debug.level, "attr_debug_store: Invalid debug value");
		return -EINVAL;
	}
	val = (int) new;
	if (val >= 0 && val <= 7) {
		dev->debug.level = val;
		DEWB_LOG_DEBUG(dev->debug.level, "attr_debug_store: Setting Log level to %d for device %s", 
			val, dev->name);
	}
	else
		DEWB_LOG_WARN(dev->debug.level, "attr_debug_store: Invalid debug value (%d) for device %s in sysfs", 
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
	//char filename[DEWB_URL_SIZE + 1];
	//const char *tmp = buf;
	unsigned long long size = 0;
	size_t len = 0;
	char *size_str = NULL;
	char *params[2];
	char delim = ' ';
	char *tmp_buf;
	(void)c;
	(void)attr;

	DEWB_LOG_INFO(dewb_log, "Creating volume with params: %s (%lu)", buf, count);

	/* TODO: split the buff into two string array with a thread-safe function strtok_r
	 *       - use a temporary buffer
	 *       - properly end string
	 */
	tmp_buf = NULL;
	if (count >= 256) {
		DEWB_LOG_ERR(dewb_log, "Invalid parameter (too long: %lu)", count);
		ret = -EINVAL;
		goto out;
	}
	
	tmp_buf = kmalloc(count, GFP_KERNEL);
	if (NULL == tmp_buf) {
		DEWB_LOG_ERR(dewb_log, "Unable to allocate memory for parameters");
		ret = -ENOMEM;
		goto out;
	}
	memcpy(tmp_buf, buf, count);
	tmp_buf[count - 1] = 0;
	parse_params(tmp_buf, &delim, params, 2, count);
	/* sanity check */
	len = strlen(params[0]);
	if (len >= DEWB_URL_SIZE) {
		DEWB_LOG_ERR(dewb_log, "Invalid volume name (too long: %lu)", len);
		ret = -EINVAL;
		goto out;
	}
	human_to_bytes(params[1], &size);

	DEWB_LOG_INFO(dewb_log, "Creating volume %s of size %llu (bytes)", params[0], size);

	ret = dewb_device_create(params[0], size);
	if (ret != 0) {
		DEWB_LOG_ERR(dewb_log, "Failed to create device: %lu", ret);
		goto out;
	}

	ret = count;

out:
	if (size_str != NULL)
		kfree(size_str);
	if (tmp_buf != NULL)
		kfree(tmp_buf);

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
	//char filename[DEWB_URL_SIZE + 1];
	//const char *tmp = buf;
	unsigned long long size = 0;
	size_t len = 0;
	char *size_str = NULL;
	char *params[2];
	char delim = ' ';
	char *tmp_buf;
	(void)c;
	(void)attr;

	DEWB_LOG_INFO(dewb_log, "Extending volume with params: %s (%lu)", buf, count);

	tmp_buf = NULL;
	if (count >= 256) {
		DEWB_LOG_ERR(dewb_log, "Invalid parameter (too long: %lu)", count);
		ret = -EINVAL;
		goto out;
	}
	
	tmp_buf = kmalloc(count, GFP_KERNEL);
	if (NULL == tmp_buf) {
		DEWB_LOG_ERR(dewb_log, "Unable to allocate memory for parameters");
		ret = -ENOMEM;
		goto out;
	}
	memcpy(tmp_buf, buf, count);
	tmp_buf[count - 1] = 0;
	parse_params(tmp_buf, &delim, params, 2, count);
	/* sanity check */
	len = strlen(params[0]);
	if (len >= DEWB_URL_SIZE) {
		DEWB_LOG_ERR(dewb_log, "Invalid volume name (too long: %lu)", len);
		ret = -EINVAL;
		goto out;
	}
	human_to_bytes(params[1], &size);

	DEWB_LOG_INFO(dewb_log, "Creating volume %s of size %llu (bytes)", params[0], size);

	ret = dewb_device_extend(params[0], size);
	if (ret != 0) {
		goto out;
	}

	ret = count;

out:
	if (size_str != NULL)
		kfree(size_str);
	if (tmp_buf != NULL)
		kfree(tmp_buf);

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
		DEWB_LOG_ERR(dewb_log, "Invalid parameter (too long: %lu)", count);
		ret = -ENOMEM;
		goto out;
	}
	
	memcpy(filename, buf, count);
	if (filename[count - 1] == '\n')
		filename[count - 1] = 0;
	else
		filename[count] = 0;

	DEWB_LOG_INFO(dewb_log, "Destroying device '%s'", filename);
	ret = dewb_device_destroy(filename);
	if (ret != 0) {
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
	struct dewb_cdmi_desc_s *cdmi_desc;

	cdmi_desc = NULL;

	/* Sanity check URL size */
	if ((count == 0) || (count > DEWB_URL_SIZE)) {
		DEWB_LOG_ERR(dewb_log, "Invalid parameter (too long: %lu)", count);
		ret = -EINVAL;
		goto out;
	}
	
	memcpy(filename, buf, count);
	if (filename[count - 1] == '\n')
		filename[count - 1] = 0;
	else
		filename[count] = 0;

	cdmi_desc = kmalloc(sizeof(struct dewb_cdmi_desc_s *), GFP_KERNEL);
	if (cdmi_desc == NULL) {
		DEWB_LOG_ERR(dewb_log, "Failed to allocate memory for CDMI struct");
		ret = -ENOMEM;
                goto out;
	}

	/* ret = _dewb_mirror_pick(filename, cdmi_desc);
	if (ret != 0) {
		DEWB_LOG_ERR(dewb_log, "Failed to get mirror from filename: %s", filename);
		ret = -EINVAL;
		goto out;
	} */

	DEWB_LOG_INFO(dewb_log, "Attaching device %s", filename);
	ret = dewb_device_attach(cdmi_desc, filename);
	if (ret == 0) {
		kfree(cdmi_desc);
		return count;
	}
out:
	if (NULL != cdmi_desc)
		kfree(cdmi_desc);

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
		DEWB_LOG_ERR(dewb_log, "Invalid parameter (too long: %lu)", count);
		return -EINVAL;
	}
	
	memcpy(filename, buf, count);
	if (filename[count - 1] == '\n')
		filename[count - 1] = 0;
	else
		filename[count] = 0;

	DEWB_LOG_INFO(dewb_log, "Detaching device %s", filename);
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
				DEWB_LOG_ERR(dewb_log, "Url too big: '%s'", tmp);
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
		DEWB_LOG_ERR(dewb_log, "Could not add every mirror to driver.");
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
				DEWB_LOG_ERR(dewb_log, "Url too big: '%s'", tmp);
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
	/* TODO: check for class_create() from device.h
	 */
	class_dewb = kzalloc(sizeof(*class_dewb), GFP_KERNEL);
	if (!class_dewb) {
		DEWB_LOG_CRIT(dewb_log, "Failed to allocate memory for sysfs class registration");
		return -ENOMEM;
	}
	class_dewb->name	  = DEV_NAME;
	class_dewb->owner	  = THIS_MODULE;
	class_dewb->class_release = class_dewb_release;
	class_dewb->class_attrs   = class_dewb_attrs;

	ret = class_register(class_dewb);
	if (ret) {
		kfree(class_dewb);
		class_dewb = NULL;
		DEWB_LOG_CRIT(dewb_log, "Failed to create class dewb");
		return ret;
	}

	return 0;
}

void dewb_sysfs_cleanup(void)
{
	/* TODO: check for class_unregister from device.h
	 */
	if (class_dewb)
		class_destroy(class_dewb);
	class_dewb = NULL;
}
