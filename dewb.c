
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
#include <linux/init.h>      // included for __init and __exit macros
#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/blkdev.h>

#include "dewb.h"

MODULE_LICENSE("GPL");

static dewb_device_t	devtab[DEV_MAX];
static int		nb_threads = 0;
static DEFINE_SPINLOCK(devtab_lock);

/*
 * Handle an I/O request.
 */
static void dewb_xmit_range(struct dewb_device_s *dev, 
			struct dewb_cdmi_desc_s *desc,
			char *buf, 
			unsigned long range_start, unsigned long size, int write)

{
	if ((range_start + size) > dev->disk_size) {
		DEWB_INFO("Beyond-end write (%lu %lu)", range_start, size);
		return;
	}

	if (size != 4096)
		DEWB_DEBUG("Wrote size of %lu", size);

	if (write) {
		dewb_cdmi_putrange(dev,
				desc,
				buf,
				range_start, 
				size);
	}
	else {
		dewb_cdmi_getrange(dev,
				desc,
				buf,
				range_start, 
				size);
	}
}

static int dewb_xfer_bio(struct dewb_device_s *dev, 
			struct dewb_cdmi_desc_s *desc,
			struct bio *bio)
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
		dewb_cdmi_flush(dev, desc, dev->disk_size);
	}

	
	bio_for_each_segment(bvec, bio, i) {
		char *buffer = kmap(bvec->bv_page);
		unsigned int nbsect = bvec->bv_len / 512UL;
		
		DEWB_DEBUG("[Transfering sect=%lu, nb=%d w=%d]",
			(unsigned long) sector, nbsect, 
			bio_data_dir(bio) == WRITE);
		
		dewb_xmit_range(dev, desc, buffer + bvec->bv_offset,
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
		dewb_cdmi_flush(dev, desc, dev->disk_size);
	}

	return 0;
}

static int dewb_thread(void *data)
{
	struct dewb_device_s *dev = data;
	struct request *req;
	unsigned long flags;
	struct bio *bio;
	int th_id;
	
	/* Init thread specific values */
	spin_lock(&devtab_lock);
	th_id = nb_threads;
	nb_threads++;
	spin_unlock(&devtab_lock);

	set_user_nice(current, -20);
	while (!kthread_should_stop() || !list_empty(&dev->waiting_queue)) {
		/* wait for something to do */
		wait_event_interruptible(dev->waiting_wq,
					kthread_should_stop() ||
					!list_empty(&dev->waiting_queue));

		spin_lock_irqsave(&dev->waiting_lock, flags);
		/* extract request */
		if (list_empty(&dev->waiting_queue)) {
			spin_unlock_irqrestore(&dev->waiting_lock, flags);
			continue;
		}
		req = list_entry(dev->waiting_queue.next, struct request,
				queuelist);
		list_del_init(&req->queuelist);
		spin_unlock_irqrestore(&dev->waiting_lock, flags);
		
		DEWB_DEBUG("NEW REQUEST [tid:%d]", th_id);
		__rq_for_each_bio(bio, req) {
			DEWB_DEBUG("New bio sector:%lu", bio->bi_sector);
			dewb_xfer_bio(dev, &dev->thread_cdmi_desc[th_id], bio);
			DEWB_DEBUG("End bio sector:%lu", bio->bi_sector);

		}
		DEWB_DEBUG("END REQUEST [tid:%d]", th_id);
		
		/* No IO error testing for the moment */
		blk_end_request_all(req, 0);
	
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

		wake_up_nr(&dev->waiting_wq, 1);

	}
}

static int dewb_open(struct block_device *bdev, fmode_t mode)
{
	dewb_device_t *dev = bdev->bd_disk->private_data;

	spin_lock(&devtab_lock);

	dev->users++;

	spin_unlock(&devtab_lock);

	return 0;
}

static int dewb_release(struct gendisk *disk, fmode_t mode)
{
	dewb_device_t *dev = disk->private_data;

	spin_lock(&devtab_lock);

	dev->users--;

	spin_unlock(&devtab_lock);

	return 0;
}

static const struct block_device_operations dewb_fops =
{
	.owner   =	THIS_MODULE,
	.open    =	dewb_open,
	.release =	dewb_release,
};

static int dewb_init_disk(struct dewb_device_s *dev)
{
	struct gendisk *disk;
	struct request_queue *q;
	int i;
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

	//blk_queue_max_hw_sectors(q, DEV_SECTORSIZE / 512ULL);
	q->queuedata	= dev;
	
	dev->disk	= disk;
	dev->q		= disk->queue = q;

	blk_queue_flush(q, REQ_FLUSH | REQ_FUA);
	blk_queue_max_segments(q, 1);

	for (i = 0; i < DEWB_THREAD_POOL_SIZE; i++) {

		if ((ret = dewb_cdmi_connect(dev, &dev->thread_cdmi_desc[i]))) {
			DEWB_ERROR("Unable to connect to CDMI endpoint : %d",
				ret);
			put_disk(disk);
			return -EIO;
		}

	}
	/* Caution: be sure to call this before spawning threads */
	ret = dewb_cdmi_getsize(dev, &dev->disk_size);

	set_capacity(disk, dev->disk_size / 512ULL);

	for (i = 0; i < DEWB_THREAD_POOL_SIZE; i++) {
		
		dev->thread[i] = kthread_create(dewb_thread, dev, "%s", 
						dev->disk->disk_name);
		
		if (IS_ERR(dev->thread[i])) {
			DEWB_ERROR("Unable to create worker thread");
			put_disk(disk);
			return -EIO;
		}
		wake_up_process(dev->thread[i]);

	}
	add_disk(disk);

	DEWB_INFO("%s: Added of size 0x%llx",
		disk->disk_name, (unsigned long long)dev->disk_size);

	return 0;
}
#define device_free_slot(X) ((X)->name[0] == 0)


/* This function gets the next free slot in device tab (devtab)
** and sets its name and id.
** Note : all the remaining fields states are undefived, it is
** the caller responsability to set them.
*/
static dewb_device_t *dewb_device_new(void)
{
	dewb_device_t *dev = NULL;
	int i;

	/* Lock table to protect against concurrent devices
	 * creation */
	spin_lock(&devtab_lock);

	/* find next empty slot in tab */
	for (i = 0; i < DEV_MAX; i++)
		if (device_free_slot(&devtab[i])) {
			dev = &devtab[i];
			break;
		}
	
	/* If no room left, return NULL*/
	if (!dev)
		goto out;

	dev->id = i;
	dev->debug = DEWB_DEBUG_LEVEL;
	dev->users = 0;
	sprintf(dev->name, DEV_NAME "%c", (char)(i + 'a'));

out:
	spin_unlock(&devtab_lock);
	return dev;
}

/* This helper marks the given device slot as empty 
** CAUTION: the devab lock must be held 
*/
static void __dewb_device_free(dewb_device_t *dev)
{
	dev->name[0] = 0;
}

static void dewb_device_free(dewb_device_t *dev)
{
	spin_lock(&devtab_lock);
	__dewb_device_free(dev);
	spin_unlock(&devtab_lock);
}

static int __dewb_device_remove(dewb_device_t *dev)
{
	int i;

	if (dev->users) {
		DEWB_ERROR("%s: Unable to remove, device still opened", dev->name);
		return -EBUSY;
	}
		
	if (device_free_slot(dev)) {
		DEWB_ERROR("Unable to remove: aldready freed");
		return -EINVAL;
	}

	if (!dev->disk)
		return -EINVAL;
	
	DEWB_INFO("%s: Removing", dev->disk->disk_name);
	
	for (i = 0; i < DEWB_THREAD_POOL_SIZE; i++)
		kthread_stop(dev->thread[i]);

	if (dev->disk->flags & GENHD_FL_UP)
		del_gendisk(dev->disk);

	if (dev->disk->queue)
		blk_cleanup_queue(dev->disk->queue);

	put_disk(dev->disk);
	
	/* Remove device */
	unregister_blkdev(dev->major, dev->name);

	/* Mark slot as empty */
	__dewb_device_free(dev);

	return 0;
}

int dewb_device_remove(dewb_device_t *dev)
{
	int ret;

	spin_lock(&devtab_lock);	

	ret = __dewb_device_remove(dev);

	spin_unlock(&devtab_lock);

	return ret;	
}

int dewb_device_remove_by_id(int dev_id)
{
	dewb_device_t *dev;
	int ret;

	spin_lock(&devtab_lock);

	dev = &devtab[dev_id];
	ret = __dewb_device_remove(dev);

	spin_unlock(&devtab_lock);

	return ret;
}

int dewb_device_add(char *url)
{
	dewb_device_t *dev;
	ssize_t rc;
	int irc;
	int i;

	/* Allocate dev structure */
	dev = dewb_device_new();
	if (!dev) {
		rc=  -ENOMEM;
		goto err_out_mod;
	}

	init_waitqueue_head(&dev->waiting_wq);
	INIT_LIST_HEAD(&dev->waiting_queue);
	spin_lock_init(&dev->waiting_lock);

	/* Parse add command */
	for (i = 0; i < DEWB_THREAD_POOL_SIZE; i++) {
		if (dewb_cdmi_init(dev, &dev->thread_cdmi_desc[i], url)) {
			DEWB_ERROR("Invalid URL : %s", url);
			rc = -EINVAL;
			goto err_out_dev;
		}
	}
	irc = register_blkdev(0, dev->name);
	if (irc < 0) {
		rc = irc;
		goto err_out_dev;
	}

	dev->major = irc;

	rc = dewb_init_disk(dev);
	if (rc < 0)
		goto err_out_unregister;

	dewb_sysfs_device_init(dev);
	
	DEWB_DEBUG("Added device (major:%d)", dev->major);

	return rc;
err_out_unregister:
	unregister_blkdev(dev->major, dev->name);
err_out_dev:
	dewb_device_free(dev);
err_out_mod:
	DEWB_ERROR("Error adding device %s", url);
	return rc;
}

static int __init dewblock_init(void)
{
	int rc;
	
	DEWB_INFO("Installing dewblock module");

	/* Zeroing device tab */
	memset(devtab, 0, sizeof(devtab));

	rc = dewb_sysfs_init();
	if (rc)
		return rc;
	
	return 0;
}
 
static void __exit dewblock_cleanup(void)
{
	DEWB_INFO("Cleaning up module");	
	dewb_sysfs_cleanup();

	return ;
}

module_init(dewblock_init);
module_exit(dewblock_cleanup);
