
// Defining __KERNEL__ and MODULE allows us to access kernel-level
// code not usually available to userspace programs.

#undef __KERNEL__
#define __KERNEL__
 
#undef MODULE
#define MODULE
 
// Linux Kernel/LKM headers: module.h is needed by all modules and
// kernel.h is needed for KERN_INFO.

#include <linux/module.h>    // included for all kernel modules
#include <linux/kernel.h>    // included for KERN_INFO
#include <linux/init.h>        // included for __init and __exit macros
#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/kthread.h>

#include "dewb.h"

MODULE_LICENSE("GPL");

typedef struct dewb_device_s {

	/* Device subsystem related data */
	int			id;		/* device ID */
	int			major;		/* blkdev assigned major */
	char			name[32];	/* blkdev name, e.g. dewba */
	struct gendisk		*disk;
	uint64_t		disk_size;

	struct request_queue	*q;
	spinlock_t		rq_lock;	/* request queue lock */

	struct task_struct	*thread; 
	/* 
	** List of requests received by the drivers, but still to be
	** processed. This due to network latency.
	*/
	spinlock_t		waiting_lock;	/* request queue lock */
	wait_queue_head_t	waiting_wq;
	struct list_head	waiting_queue; /* Requests to be sent */
	
	/* Dewpoint specific data */
	struct dewb_cdmi_desc_s cdmi_desc;
	
} dewb_device_t;

static dewb_device_t	devtab[DEV_MAX];
static int		allocated_devices = 0;
static DEFINE_SPINLOCK(devtab_lock);

static const struct block_device_operations dewb_fops =
{
	.owner =	THIS_MODULE,
};

/*
 * Handle an I/O request.
 */
static void dewb_xmit_range(struct dewb_device_s *dev, char *buf, 
			unsigned long range_start, unsigned long size, int write)

{
	int i;

	if ((range_start + size) > dev->disk_size) {
		DEWB_INFO("Beyond-end write (%lu %lu)", range_start, size);
		return;
	}

	if (write) {
//		for (i = 0; i < size / 512UL; i++)
			dewb_cdmi_putrange(&dev->cdmi_desc, 
					buf,
					range_start +  4096UL, 
					4096UL);
	}
	else {
//		for (i = 0; i < size / 512UL; i++)
			dewb_cdmi_getrange(&dev->cdmi_desc,
					buf,
					range_start + ( i * 4096UL), 
					4096UL);
	}
}

static int dewb_xfer_bio(struct dewb_device_s *dev, struct bio *bio)
{
	int i;
	struct bio_vec *bvec;
	sector_t sector = bio->bi_sector;

	/* 
	** Takes care of REQ_FLUSH flag, if set we have to flush
	** before sending actual bio to device
	*/
	if (bio->bi_rw & REQ_FLUSH) {
		DEWB_DEBUG("[Flush(REG_FLUSH)]");
		dewb_cdmi_flush(&dev->cdmi_desc, dev->disk_size);
	}

	bio_for_each_segment(bvec, bio, i) {
		char *buffer = kmap(bvec->bv_page);
		unsigned int nbsect = bvec->bv_len / 512UL;
		
		DEWB_DEBUG("[Transfering sect=%lu, nb=%d w=%d]",
			(unsigned long) sector, nbsect, 
			bio_data_dir(bio) == WRITE);
		
		dewb_xmit_range(dev, buffer + bvec->bv_offset, 
				sector * 512UL, nbsect * 512UL,
				bio_data_dir(bio) == WRITE);

		sector += nbsect;
		kunmap(bvec->bv_page);
	}

	/* 
	** Takes care care of REQ_FUA flag, need to flush here
	** after the bio have been sent
	*/
	if (bio->bi_rw & REQ_FUA) {
		DEWB_DEBUG("[Flush(REG_FUA)]");
		dewb_cdmi_flush(&dev->cdmi_desc, dev->disk_size);
	}

	return 0;
}

static int dewb_thread(void *data)
{
	struct dewb_device_s *dev = data;
	struct request *req;
	unsigned long flags;
	struct bio *bio;

	set_user_nice(current, -20);
	while (!kthread_should_stop() || !list_empty(&dev->waiting_queue)) {
		/* wait for something to do */
		wait_event_interruptible(dev->waiting_wq,
					kthread_should_stop() ||
					!list_empty(&dev->waiting_queue));

		/* extract request */
		if (list_empty(&dev->waiting_queue))
			continue;

		spin_lock_irqsave(&dev->waiting_lock, flags);
		req = list_entry(dev->waiting_queue.next, struct request,
				queuelist);
		list_del_init(&req->queuelist);
		spin_unlock_irqrestore(&dev->waiting_lock, flags);

		__rq_for_each_bio(bio, req) {
			dewb_xfer_bio(dev, bio);
		}
		
		/* No IO error testing for the moment */
		__blk_end_request_all(req, 0);
	
	}
	return 0;
}


static void dewb_rq_fn(struct request_queue *q)
{
	struct dewb_device_s *dev = q->queuedata;	
	struct request *req;
	unsigned long flags;

       	while ((req = blk_fetch_request(q)) != NULL) {
		if (req->cmd_type != REQ_TYPE_FS) {
			DEWB_DEBUG("Skip non-CMD request");
			__blk_end_request_all(req, -EIO);
			continue;
		}

		spin_lock_irqsave(&dev->waiting_lock, flags);
		list_add_tail(&req->queuelist, &dev->waiting_queue);
		spin_unlock_irqrestore(&dev->waiting_lock, flags);

		wake_up(&dev->waiting_wq);

	}
}

static int dewb_init_disk(struct dewb_device_s *dev)
{
	struct gendisk *disk;
	struct request_queue *q;
	int ret;

	/* create gendisk info */
	disk = alloc_disk(DEV_MINORS);
	if (!disk)
		return -ENOMEM;

	strcpy(disk->disk_name, dev->name);
	disk->major	   = dev->major;
	disk->first_minor  = 0;
	disk->fops	   = &dewb_fops;
	disk->private_data = dev;

	/* init rq */
	q = blk_init_queue(dewb_rq_fn, &dev->rq_lock);
	if (!q) {
		put_disk(disk);
		return -ENOMEM;
	}
	q->queuedata	= dev;

	dev->disk	= disk;
	dev->q		= disk->queue = q;

	blk_queue_flush(q, REQ_FLUSH | REQ_FUA);
	
	if ((ret = dewb_cdmi_connect(&dev->cdmi_desc))) {
		DEWB_ERROR("Unable to connect to CDMI endpoint : %d", ret);
		put_disk(disk);
		return -EIO;
	}

	ret = dewb_cdmi_getsize(&dev->cdmi_desc, &dev->disk_size);
	set_capacity(disk, dev->disk_size / 512ULL);

	dev->thread = kthread_create(dewb_thread, dev, "%s", 
				dev->disk->disk_name);

	wake_up_process(dev->thread);
	
	if (IS_ERR(dev->thread)) {
		DEWB_ERROR("Unable to create worker thread");
		put_disk(disk);
		return -EIO;
	}

	add_disk(disk);

	DEWB_INFO("%s: Added of size 0x%llx",
		disk->disk_name, (unsigned long long)dev->disk_size);

	return 0;
}

/********************************************************************
 * /sys/class/dewp/
 *                   add	Create a new dewp device
 *                   remove	Remove the last created device
 *******************************************************************/

static struct class *class_dewp;		/* /sys/class/dewp */

static void class_dewp_release(struct class *cls)
{
	kfree(cls);
}


static ssize_t class_dewp_add(struct class *c,
			struct class_attribute *attr,
			const char *buf, size_t count)
{
	ssize_t rc;
	int irc;
	int dev_id;
	dewb_device_t *dev;
	char url[DEWB_URL_SIZE + 1];

	/* Get module reference */
	if (!try_module_get(THIS_MODULE))
		return -ENODEV;

	/* Sanity check URL size */
	if ((count == 0) || (count > DEWB_URL_SIZE)) {
		rc=  -ENOMEM;
		goto err_out_mod;
	}
	
	memcpy(url, buf, count);
	if (url[count - 1] == '\n')
		url[count - 1] = 0;
	else
		url[count] = 0;
	
	/* Allocate dev structure */
	spin_lock(&devtab_lock);
	if (allocated_devices == DEV_MAX) {
		rc=  -ENOMEM;
		goto err_out_mod;
	}

	dev_id   = allocated_devices;
	dev	 = &devtab[allocated_devices];
	allocated_devices++;
	dev->id  = dev_id;
	init_waitqueue_head(&dev->waiting_wq);
	INIT_LIST_HEAD(&dev->waiting_queue);
	spin_lock_init(&dev->waiting_lock);

	spin_unlock(&devtab_lock);

	/* Parse add command */
	if (dewb_cdmi_init(&dev->cdmi_desc, url)) {
		DEWB_ERROR("Invalid URL : %s", url);
		rc = -EINVAL;
		goto err_out_dev;
	}
		
	/* initialize rest of new object */
	sprintf(dev->name, DEV_NAME "%c", (char)(dev_id + 'a'));
	
	irc = register_blkdev(0, dev->name);
	if (irc < 0) {
		rc = irc;
		goto err_out_dev;
	}

	dev->major = irc;

	rc = dewb_init_disk(dev);
	if (rc < 0)
		goto err_out_unregister;
	DEWB_DEBUG("Added device %s (major:%d)", dev->name, dev->major);

	return count;
err_out_unregister:
	unregister_blkdev(dev->major, dev->name);
err_out_dev:
	allocated_devices--;
err_out_mod:
	DEWB_DEBUG("Error adding device %s", buf);
	module_put(THIS_MODULE);
	return rc;
}

static ssize_t class_dewp_remove(struct class *c,
				struct class_attribute *attr,
				const char *buf,
				size_t count)
{
	dewb_device_t *dev;
	int dev_id = allocated_devices - 1;

	spin_lock(&devtab_lock);
	if (allocated_devices == 0)
		goto out;

	allocated_devices--;

	dev = &devtab[dev_id];

	if (!dev->disk)
		return count;
	
	kthread_stop(dev->thread);

	if (dev->disk->flags & GENHD_FL_UP)
		del_gendisk(dev->disk);

	if (dev->disk->queue)
		blk_cleanup_queue(dev->disk->queue);

	DEWB_INFO("%s: Removing",
		dev->disk->disk_name);

	put_disk(dev->disk);
	
	/* Remove device */
	unregister_blkdev(dev->major, dev->name);

	/* release module ref */
	module_put(THIS_MODULE);

out:
	spin_unlock(&devtab_lock);
	return count;
}

static struct class_attribute class_dewp_attrs[] = {
	__ATTR(add,	0200, NULL, class_dewp_add),
	__ATTR(remove,	0200, NULL, class_dewp_remove),
	__ATTR_NULL
};

static int dewp_sysfs_init(void)
{
	int ret = 0;

	/*
	 * create control files in sysfs
	 * /sys/class/dewp/...
	 */
	class_dewp = kzalloc(sizeof(*class_dewp), GFP_KERNEL);
	if (!class_dewp)
		return -ENOMEM;

	class_dewp->name	  = DEV_NAME;
	class_dewp->owner         = THIS_MODULE;
	class_dewp->class_release = class_dewp_release;
	class_dewp->class_attrs   = class_dewp_attrs;

	ret = class_register(class_dewp);
	if (ret) {
		kfree(class_dewp);
		class_dewp = NULL;
		DEWB_ERROR("failed to create class dewp");
		return ret;
	}

	return 0;
}

static int __init dewblock_init(void)
{
	int rc;
	
	DEWB_INFO("Installing dewblock module");
	rc = dewp_sysfs_init();
	if (rc)
		return rc;
	
	return 0;
}
 
static void __exit dewblock_cleanup(void)
{
	DEWB_INFO("Cleaning up module");

	if (class_dewp)
		class_destroy(class_dewp);
	class_dewp = NULL;
}

module_init(dewblock_init);
module_exit(dewblock_cleanup);
