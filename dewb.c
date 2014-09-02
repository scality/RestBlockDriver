/*
   This file is part of RestBlockDriver.

   RestBlockDriver is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   RestBlockDriver is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Foobar.  If not, see <http://www.gnu.org/licenses/>.

 */

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
#include <linux/moduleparam.h>	// included for LKM parameters
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


/* TODO: Testing REQ_FLUSH with old version (Issue #21)
 */
//#define DEWB_BIO_ENABLED	1


// LKM information
MODULE_AUTHOR("Laurent Meyer <laurent.meyer@digitam.net>");
MODULE_AUTHOR("David Pineau <david.pineau@scality.com>");
MODULE_DESCRIPTION("Block Device Driver for REST-based storage");
MODULE_LICENSE("GPL");
MODULE_VERSION(DEV_REL_VERSION);

static dewb_device_t	devtab[DEV_MAX];
static dewb_mirror_t	*mirrors = NULL;
static DEFINE_SPINLOCK(devtab_lock);

/* Module parameters (LKM parameters)
 */
unsigned short dewb_log = DEWB_LOG_LEVEL_DFLT;
unsigned short req_timeout = DEWB_REQ_TIMEOUT_DFLT;
unsigned short nb_req_retries = DEWB_NB_REQ_RETRIES_DFLT;
unsigned short mirror_conn_timeout = DEWB_CONN_TIMEOUT_DFLT;
unsigned int thread_pool_size = DEWB_THREAD_POOL_SIZE_DFLT;
MODULE_PARM_DESC(debug, "Global log level for Dewblock LKM");
module_param_named(debug, dewb_log, ushort, 0644);

MODULE_PARM_DESC(req_timeout, "Global timeout for request");
module_param(req_timeout, ushort, 0644);

MODULE_PARM_DESC(nb_req_retries, "Global number of retries for request");
module_param(nb_req_retries, ushort, 0644);

MODULE_PARM_DESC(mirror_conn_timeout, "Global timeout for connection to mirror(s)");
module_param(mirror_conn_timeout, ushort, 0644);

MODULE_PARM_DESC(thread_pool_size, "Size of the thread pool");
module_param(thread_pool_size, uint, 0444);

/* XXX: Request mapping
 */
char *req_code_to_str(int code)
{
	switch (code) {
		case READ: return "READ"; break;
		case WRITE: return "WRITE"; break;
		case WRITE_FLUSH: return "WRITE_FLUSH"; break;
		case WRITE_FUA: return "WRITE_FUA"; break;
		case WRITE_FLUSH_FUA: return "WRITE_FLUSH_FUA"; break;
		default: return "UNKNOWN";
	}
}

int req_flags_to_str(int flags, char *buff)
{
	//char buff[128];
	int size = 0;
	buff[0] = '\0';

	// detect common flags
	if (flags == REQ_COMMON_MASK) {
		strncpy(buff, "REQ_COMMON_MASK", 15);
		buff[16] = '\0';
		return 16;
	}
	if (flags == REQ_FAILFAST_MASK) {
		strncpy(buff, "REQ_FAILFAST_MASK", 17);
		buff[18] = '\0';
		return 18;
	}
	if (flags == REQ_NOMERGE_FLAGS) {
		strncpy(buff, "REQ_NOMERGE_FLAGS", 17);
		buff[18] = '\0';
		return 18;
	}

	if (flags & REQ_WRITE) {
		strncpy(&buff[size], "REQ_WRITE|", 10);
		size += 10;
	}
	if (flags & REQ_FAILFAST_DEV) {
		strncpy(&buff[size], "REQ_FAILFAST_DEV|", 17);
		size += 17;
	}
	if (flags & REQ_FAILFAST_TRANSPORT) {
		strncpy(&buff[size], "REQ_FAILFAST_TRANSPORT|", 23);
		size += 23;
	}
	if (flags & REQ_FAILFAST_DRIVER) {
		strncpy(&buff[size], "REQ_FAILFAST_DRIVER|", 20);
		size += 20;
	}
	if (flags & REQ_SYNC) {
		strncpy(&buff[size], "REQ_SYNC|", 9);
		size += 9;
 	}	
	if (flags & REQ_META) {
		strncpy(&buff[size], "REQ_META|", 9);
		size += 9;
	}
	if (flags & REQ_PRIO) {
		strncpy(&buff[size], "REQ_PRIO|", 9);
		size += 9;
	}
	if (flags & REQ_DISCARD) {
		strncpy(&buff[size], "REQ_DISCARD|", 13);
		size += 13;
	}
	if (flags & REQ_WRITE_SAME) {
		strncpy(&buff[size], "REQ_WRITE_SAME|", 16);
		size += 16;
	}
	if (flags & REQ_NOIDLE) {
		strncpy(&buff[size], "REQ_NOIDLE|", 11);
		size += 11;
	}
	if (flags & REQ_RAHEAD) {
		strncpy(&buff[size], "REQ_RAHEAD|", 11);
		size += 11;
	}
	if (flags & REQ_THROTTLED) {
		strncpy(&buff[size], "REQ_THROTTLED|", 14);
		size += 14;
	}
	if (flags & REQ_SORTED) {
		strncpy(&buff[size], "REQ_SORTED|", 11);
		size += 11;
	}
	if (flags & REQ_SOFTBARRIER) {
		strncpy(&buff[size], "REQ_SOFTBARRIER|", 16);
		size += 16;
	}
	if (flags & REQ_FUA) {
		strncpy(&buff[size], "REQ_FUA|", 8);
		size += 8;
	}
	if (flags & REQ_NOMERGE) {
		strncpy(&buff[size], "REQ_NOMERGE|", 12);
		size += 12;
	}
	if (flags & REQ_STARTED) {
		strncpy(&buff[size], "REQ_STARTED|", 12);
		size += 12;
	}
	if (flags & REQ_DONTPREP) {
		strncpy(&buff[size], "REQ_DONTPREP|", 13);
		size += 13;
	}
	if (flags & REQ_QUEUED) {
		strncpy(&buff[size], "REQ_QUEUED|", 11);
		size += 11;
	}
	if (flags & REQ_ELVPRIV) {
		strncpy(&buff[size], "REQ_ELVPRIV|", 12);
		size += 12;
	}
	if (flags & REQ_FAILED) {
		strncpy(&buff[size], "REQ_FAILED|", 11);
		size += 11;
	}
	if (flags & REQ_QUIET) {
		strncpy(&buff[size], "REQ_QUIET|", 10);
		size += 10;
	}
	if (flags & REQ_PREEMPT) {
		strncpy(&buff[size], "REQ_PREEMPT|", 12);
		size += 12;
	}
	if (flags & REQ_ALLOCED) {
		strncpy(&buff[size], "REQ_ALLOCED|", 12);
		size += 12;
	}
	if (flags & REQ_COPY_USER) {
		strncpy(&buff[size], "REQ_COPY_USER|", 14);
		size += 14;
	}
	if (flags & REQ_FLUSH) {
		strncpy(&buff[size], "REQ_FLUSH|", 10);
		size += 10;
	}
	if (flags & REQ_FLUSH_SEQ) {
		strncpy(&buff[size], "REQ_FLUSH_SEQ|", 14);
		size += 14;
	}
	if (flags & REQ_IO_STAT) {
		strncpy(&buff[size], "REQ_IO_STAT|", 12);
		size += 12;
	}
	if (flags & REQ_MIXED_MERGE) {
		strncpy(&buff[size], "REQ_MIXED_MERGE|", 16);
		size += 16;
	}
	if (flags & REQ_SECURE) {
		strncpy(&buff[size], "REQ_SECURE|", 11);
		size += 11;
	}
	if (flags & REQ_KERNEL) {
		strncpy(&buff[size], "REQ_KERNEL|", 11);
		size += 11;
	}
	if (flags & REQ_PM) {
		strncpy(&buff[size], "REQ_PM|", 7);
		size += 7;
	}
#ifdef REQ_END	/* appears in 3.6.10 in linux/blk_types.h */
	if (flags & REQ_END) {
		strncpy(&buff[size], "REQ_END|", 8);
		size += 8;
	}
#endif
	if (size != 0)
		buff[size-1] = '\0';

	return size;
}


/*
 * Handle an I/O request.
 */
#ifdef DEWB_BIO_ENABLED
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
		DEWB_DEV_DEBUG("Wrote size of %lu", size);

	if (write) {
		dewb_cdmi_putrange(&dev->debug,
				desc,
				range_start, 
				size);
	}
	else {
		dewb_cdmi_getrange(&dev->debug,
				desc,
				//buf,
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
		DEWB_DEV_DEBUG("[Flush(REG_FLUSH)]");
		dewb_cdmi_flush(&dev->debug, desc, dev->disk_size);
	}

	bio_for_each_segment(bvec, bio, i) {
		char *buffer = kmap(bvec->bv_page);
		unsigned int nbsect = bvec->bv_len / 512UL;
		
		DEWB_DEV_DEBUG("[Transfering sect=%lu, nb=%d w=%d]",
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
		DEWB_DEV_DEBUG("[Flush(REG_FUA)]");
		dewb_cdmi_flush(&dev->debug, desc, dev->disk_size);
	}

	return 0;
}
#endif	/* _DEWB_BIO_ENABLED_ */ 


/* TODO: Handle CDMI request timeout and retry (Issue #22) 
 * For error handling use a returned code
 */
//void dewb_xfer_scl(struct dewb_device_s *dev, 
int dewb_xfer_scl(struct dewb_device_s *dev, 
		struct dewb_cdmi_desc_s *desc, 
		struct request *req)
{
	int ret;
	int i;

	DEWB_LOG_DEBUG(dev->debug.level, "dewb_xfer_scl: CDMI request %p (%s) for device %p with cdmi %p", 
		req, req_code_to_str(rq_data_dir(req)), dev, desc);

	ret = 0;
	/* TODO: Handle CDMI request retry (Issue #22)
	 */
	for (i = 0; i < nb_req_retries; i++) {
		if (rq_data_dir(req) == WRITE) {
			ret = dewb_cdmi_putrange(&dev->debug,
						desc,
						blk_rq_pos(req) * 512ULL, 
						blk_rq_sectors(req) * 512ULL);
		}
		else {
			ret = dewb_cdmi_getrange(&dev->debug,
						desc,
						blk_rq_pos(req) * 512ULL, 
						blk_rq_sectors(req) * 512ULL);
		}
		if (0 == ret)
			break;
		else if (i < nb_req_retries - 1)
			DEWB_LOG_NOTICE(dev->debug.level, "Retrying CDMI request... %d", (i + 1));
	}

	if (ret) {
		DEWB_LOG_ERR(dev->debug.level, "CDMI Request using scatterlist failed with IO error: %d", ret);
		return -EIO;
	}

	return ret;
}

static int dewb_thread(void *data)
{
	struct dewb_device_s *dev;
	struct request *req;
	unsigned long flags;
	int th_id;
	int th_ret = 0;
	char buff[256];
#ifdef DEWB_BIO_ENABLED
	struct bio *bio;
#endif
	struct req_iterator iter;
	struct bio_vec *bvec;
	struct dewb_cdmi_desc_s *cdmi_desc;

	DEWB_LOG_DEBUG(((struct dewb_device_s *)data)->debug.level, "dewb_thread: thread function with data %p", data);

	dev = data;

	/* Init thread specific values */
	spin_lock(&devtab_lock);
	th_id = dev->nb_threads;
	dev->nb_threads++;
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
		
		if (blk_rq_sectors(req) == 0)
			continue;

		req_flags_to_str(req->cmd_flags, buff);
		DEWB_LOG_DEBUG(dev->debug.level, "dewb_thread: thread %d: New REQ of type %s (%d) flags: %s (%llu)", 
			th_id, req_code_to_str(rq_data_dir(req)), rq_data_dir(req), buff, req->cmd_flags);
		if (req->cmd_flags & REQ_FLUSH) {
			DEWB_LOG_DEBUG(dev->debug.level, "DEBUG CMD REQ_FLUSH\n");
		}
		/* XXX: Use iterator instead of internal function (cf linux/blkdev.h)
		 *  __rq_for_each_bio(bio, req) {
		 */
		rq_for_each_segment(bvec, req, iter) {
			if (iter.bio->bi_rw & REQ_FLUSH) {
				DEWB_LOG_DEBUG(dev->debug.level, "DEBUG VR BIO REQ_FLUSH\n");
			}
		}

#ifdef DEWB_BIO_ENABLED
		__rq_for_each_bio(bio, req) {
			//dewb_xfer_bio(dev, &dev->thread_cdmi_desc[th_id], bio);
			dewb_xfer_bio(dev, dev->thread_cdmi_desc[th_id], bio);
		}
#else
		/* Create scatterlist */
		/* sg_init_table(dev->thread_cdmi_desc[th_id].sgl, DEV_NB_PHYS_SEGS);
		dev->thread_cdmi_desc[th_id].sgl_size = blk_rq_map_sg(dev->q, req, dev->thread_cdmi_desc[th_id].sgl); */
		cdmi_desc = dev->thread_cdmi_desc[th_id];
		sg_init_table(dev->thread_cdmi_desc[th_id]->sgl, DEV_NB_PHYS_SEGS);
		dev->thread_cdmi_desc[th_id]->sgl_size = blk_rq_map_sg(dev->q, req, dev->thread_cdmi_desc[th_id]->sgl);

		DEWB_LOG_DEBUG(dev->debug.level, "dewb_thread: scatter_list size %d [nb_seg = %d, sector = %lu, nr_sectors=%u w=%d]", 
			DEV_NB_PHYS_SEGS, dev->thread_cdmi_desc[th_id]->sgl_size, blk_rq_pos(req), blk_rq_sectors(req), rq_data_dir(req) == WRITE);

		/* Call scatter function */
		//th_ret = dewb_xfer_scl(dev, &dev->thread_cdmi_desc[th_id], req);
		th_ret = dewb_xfer_scl(dev, dev->thread_cdmi_desc[th_id], req);
#endif	/* _DEWB_BIO_ENABLED_ */

		//DEWB_DEV_DEBUG("END REQUEST [tid:%d]", th_id);
		DEWB_LOG_DEBUG(dev->debug.level, "dewb_thread: thread %d: REQ done with returned code %d", 
			th_id, th_ret);;
	
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
			DEWB_LOG_DEBUG(dev->debug.level, "dewb_rq_fn: Skip non-CMD request");

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
	//dewb_device_t *dev = bdev->bd_disk->private_data;
	dewb_device_t *dev;

	DEWB_LOG_INFO(dewb_log, "dewb_open: opening block device %p", bdev);

	dev = bdev->bd_disk->private_data;

	spin_lock(&devtab_lock);
	dev->users++;
	spin_unlock(&devtab_lock);

	return 0;
}

/*
 * After linux kernel v3.10, this function stops returning anything
 * (becomes void). To avoid supporting too many things, just keep it int
 * and ignore th associated warning.
 */
/* XXX: No return value expected
 *      Fix compilation warning: initialization from incompatible pointer type
 */
//static int dewb_release(struct gendisk *disk, fmode_t mode)
static void dewb_release(struct gendisk *disk, fmode_t mode)
{
	dewb_device_t *dev;

	DEWB_LOG_INFO(dewb_log, "dewb_release: releasing disk %p", disk);

	dev = disk->private_data;

	spin_lock(&devtab_lock);
	dev->users--;
	spin_unlock(&devtab_lock);

	//return 0;
}

static const struct block_device_operations dewb_fops =
{
	.owner   =	THIS_MODULE,
	.open	 =	dewb_open,
	.release =	dewb_release,
};

static int dewb_init_disk(struct dewb_device_s *dev)
{
	struct gendisk *disk;
	struct request_queue *q;
	int i;
	int ret;

	DEWB_LOG_INFO(dewb_log, "dewb_init_disk: initializing disk for device: %p", dev);

	/* create gendisk info */
	disk = alloc_disk(DEV_MINORS);
	if (!disk) {
		DEWB_LOG_WARN(dewb_log, "dewb_init_disk: unable to allocate memory for disk for device: %p", 
			dev);
		return -ENOMEM;
	}
	DEWB_LOG_INFO(dewb_log, "Creating new disk: %p", disk);

	strcpy(disk->disk_name, dev->name);
	disk->major	   = dev->major;
	disk->first_minor  = 0;
	disk->fops	   = &dewb_fops;
	disk->private_data = dev;

	/* init rq */
	q = blk_init_queue(dewb_rq_fn, &dev->rq_lock);
	if (!q) {
		DEWB_LOG_WARN(dewb_log, "dewb_init_disk: unable to init block queue for device: %p, disk: %p", 
			dev, disk);
		put_disk(disk);
		return -ENOMEM;
	}

	blk_queue_max_hw_sectors(q, DEV_NB_PHYS_SEGS);
	q->queuedata	= dev;

	dev->disk	= disk;
	dev->q		= disk->queue = q;
	dev->nb_threads = 0;
	//blk_queue_flush(q, REQ_FLUSH | REQ_FUA);
	//blk_queue_max_phys_segments(q, DEV_NB_PHYS_SEGS);

	//TODO: Enable flush and bio (Issue #21)
	//blk_queue_flush(q, REQ_FLUSH);

	//TODO: Make thread pool variable (Issue #33)
	//for (i = 0; i < DEWB_THREAD_POOL_SIZE; i++) {
	for (i = 0; i < thread_pool_size; i++) {
		//if ((ret = dewb_cdmi_connect(&dev->debug, &dev->thread_cdmi_desc[i]))) {
		if ((ret = dewb_cdmi_connect(&dev->debug, dev->thread_cdmi_desc[i]))) {
			DEWB_LOG_ERR(dewb_log, "Unable to connect to CDMI endpoint: %d",
				ret);
			put_disk(disk);
			return -EIO;
		}
	}
	/* Caution: be sure to call this before spawning threads */
	//ret = dewb_cdmi_getsize(&dev->debug, &dev->thread_cdmi_desc[0],
	ret = dewb_cdmi_getsize(&dev->debug, dev->thread_cdmi_desc[0], &dev->disk_size);
	if (ret != 0) {
		DEWB_LOG_ERR(dewb_log, "Could not retrieve volume size.");
		put_disk(disk);
		return ret;
	}

	set_capacity(disk, dev->disk_size / 512ULL);

	//TODO: Make thread pool variable (Issue #33)
	//for (i = 0; i < DEWB_THREAD_POOL_SIZE; i++) {
	for (i = 0; i < thread_pool_size; i++) {
		dev->thread[i] = kthread_create(dewb_thread, dev, "%s", 
						dev->disk->disk_name);
		if (IS_ERR(dev->thread[i])) {
			DEWB_LOG_ERR(dewb_log, "Unable to create worker thread (id %d)", i);
			put_disk(disk);
			return -EIO;
		}
		wake_up_process(dev->thread[i]);
	}
	add_disk(disk);

	DEWB_LOG_INFO(dewb_log, "%s: Added of size 0x%llx",
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

	DEWB_LOG_INFO(dewb_log, "dewb_device_new: creating new device with %d threads", 
		thread_pool_size);

	/* Lock table to protect against concurrent devices
	 * creation 
	 */
	spin_lock(&devtab_lock);

	/* find next empty slot in tab */
	for (i = 0; i < DEV_MAX; i++) {
		if (device_free_slot(&devtab[i])) {
			dev = &devtab[i];
			break;
		}
	}

	/* If no room left, return NULL */
	if (!dev)
		goto out;

	dev->id = i;
	dev->debug.name = &dev->name[0];
	/* TODO: Inherit log level from dewblock LKM (Issue #28)
	 */
	dev->debug.level = dewb_log;
	dev->users = 0;
	sprintf(dev->name, DEV_NAME "%c", (char)(i + 'a'));

	/* Table can be unlocked because device is reserved (name not empty) */
	spin_unlock(&devtab_lock);

	/* XXX: dynamic allocation of thread pool and cdmi connection pool
	 * NB: The memory allocation for the thread is an array of pointer 
	 *     whereas the allocation for the cdmi connection pool is an array
	 *     of cmdi connection structure
	 */
	//dev->thread = kmalloc(sizeof(struct task_struct *) * thread_pool_size, GFP_KERNEL);
	dev->thread_cdmi_desc = kmalloc(sizeof(struct dewb_cdmi_desc_s *) * thread_pool_size, GFP_KERNEL); 
	if (dev->thread_cdmi_desc == NULL) {
		DEWB_LOG_CRIT(dewb_log, "dewb_device_new: Unable to allocate memory for CDMI struct pointer");
		/* should return -ENOMEM */
		dev->name[0] = 0;
		dev = NULL;	
		goto err_mem;
	}
	for (i = 0; i < thread_pool_size; i++) {
		dev->thread_cdmi_desc[i] = kmalloc(sizeof(struct dewb_cdmi_desc_s), GFP_KERNEL); 
		if (dev->thread_cdmi_desc[i] == NULL) {
			DEWB_LOG_CRIT(dewb_log, "dewb_device_new: Unable to allocate memory for CDMI struct, step %d", i);
			goto err_mem;
		}
		/* TODO: add a socket timeout
		 * NB: this is erased as cdmi_desc is reallocatd and rewritted
		 */
		/* set request timeout */
		/* DEWB_LOG_INFO(dewb_log, "dewb_device_new: Setting CDMI request timeout: %d", req_timeout);
		dev->thread_cdmi_desc[i]->timeout.tv_sec = req_timeout; 
		dev->thread_cdmi_desc[i]->timeout.tv_usec = 0; */
	}
	dev->thread = kmalloc(sizeof(struct task_struct *) * thread_pool_size, GFP_KERNEL);
	if (dev->thread == NULL) {
		DEWB_LOG_CRIT(dewb_log, "dewb_device_new: Unable to allocate memory for kernel thread struct");
	}

	return dev;

err_mem:
	if (NULL != dev->thread_cdmi_desc) {
		for (i = 0; i < thread_pool_size; i++) {
			kfree(dev->thread_cdmi_desc[i]);
		}
		kfree(dev->thread_cdmi_desc);
	}
out:
	spin_unlock(&devtab_lock);

	return NULL;
}

/* This helper marks the given device slot as empty 
** CAUTION: the devab lock must be held 
*/
static void __dewb_device_free(dewb_device_t *dev)
{
	DEWB_LOG_INFO(dewb_log, "__dewb_device_free: freeing device: %p", dev);

	dev->name[0] = 0;
}

static void dewb_device_free(dewb_device_t *dev)
{
	int i;

	DEWB_LOG_INFO(dewb_log, "dewb_device_free: freeing device: %p", dev);

	/* XXX: lock has been aquire at a higher level
	 */	
	//spin_lock(&devtab_lock);

	__dewb_device_free(dev);

	/* TODO: free kernel memory for CDMI struct (Issue #33)
	 */
	for (i = 0; i < thread_pool_size; i++) {
		kfree(dev->thread_cdmi_desc[i]);
	}
	kfree(dev->thread_cdmi_desc);
	kfree(dev->thread);

	//spin_unlock(&devtab_lock);
}

static int _dewb_reconstruct_url(char *url, char *name,
				 const char *baseurl, const char *basepath,
				 const char *filename)
{
	int urllen = 0;
	int namelen = 0;
	int seplen = 0;

	DEWB_LOG_DEBUG(dewb_log, "_dewb_reconstruct_url: construction of URL with url: %s, name: %s, baseurl: %s, basepath: %p, filename: %s", 
		url, name, baseurl, basepath, filename);

	urllen = strlen(baseurl);
	if (baseurl[urllen - 1] != '/')
		seplen = 1;

	if (seplen)
	{
		urllen = snprintf(url, DEWB_URL_SIZE, "%s/%s", baseurl, filename);
		namelen = snprintf(name, DEWB_URL_SIZE, "%s/%s", basepath, filename);
	}
	else
	{
		urllen = snprintf(url, DEWB_URL_SIZE, "%s%s", baseurl, filename);
		namelen = snprintf(name, DEWB_URL_SIZE, "%s%s", basepath, filename);
	}

	if (urllen >= DEWB_URL_SIZE || namelen >= DEWB_URL_SIZE)
		return -EINVAL;

	return 0;
}

static int __dewb_device_detach(dewb_device_t *dev)
{
	int i;

	DEWB_LOG_DEBUG(dewb_log, "__dewb_device_detach: detaching device %s (%p)", dev->name, dev);

	if (dev->users) {
		DEWB_LOG_ERR(dewb_log, "%s: Unable to remove, device still opened", dev->name);
		return -EBUSY;
	}

/*
	if (device_free_slot(dev)) {
		DEWB_ERROR("Unable to remove: aldready freed");
		return -EINVAL;
	}
*/

	if (!dev->disk) {
		DEWB_LOG_ERR(dewb_log, "%s: Disk is no more available", dev->name);
		return -EINVAL;
	}

	DEWB_LOG_INFO(dewb_log, "%s: Removing disk", dev->disk->disk_name);

	/* TODO: Make thread pool variable (Issue #33)
	 */
	//for (i = 0; i < DEWB_THREAD_POOL_SIZE; i++)
	for (i = 0; i < thread_pool_size; i++)
		kthread_stop(dev->thread[i]);

	if (dev->disk->flags & GENHD_FL_UP)
		del_gendisk(dev->disk);

	if (dev->disk->queue)
		blk_cleanup_queue(dev->disk->queue);

	put_disk(dev->disk);

	/* Remove device */
	unregister_blkdev(dev->major, dev->name);

	/* Mark slot as empty */
	//__dewb_device_free(dev);
	if (NULL != dev)
		dewb_device_free(dev);

	return 0;
}

static int _dewb_detach_devices(void)
{
	int ret;
	int i = 0;
	int errcount = 0;

	DEWB_LOG_INFO(dewb_log, "_dewb_detach_devices: detaching devices");

	spin_lock(&devtab_lock);
	for (i = 0; i < DEV_MAX; ++i) {
		if (!device_free_slot(&devtab[i])) {
			ret = __dewb_device_detach(&devtab[i]);
			if (ret != 0) {
				/* DEWB_ERROR("Could not remove device %s for volume at unload %s",
					   devtab[i].name, devtab[i].thread_cdmi_desc ? devtab[i].thread_cdmi_desc[0].filename : "NULL"); */
				DEWB_LOG_ERR(dewb_log, "Could not remove device %s for volume at unload %s",
					   devtab[i].name, devtab[i].thread_cdmi_desc ? devtab[i].thread_cdmi_desc[0]->filename : "NULL");
			}
		}
	}
	spin_unlock(&devtab_lock);

	return errcount;
}

static void _dewb_mirror_free(dewb_mirror_t *mirror)
{
	/* XXX: should have a dewb_debug_t params
	 */
	DEWB_LOG_DEBUG(dewb_log, "_dewb_mirror_free: deleting mirror %p", mirror);

	if (mirror)
		kfree(mirror);
}

static int _dewb_mirror_new(dewb_debug_t *dbg, const char *url, dewb_mirror_t **mirror)
{
	dewb_mirror_t	*new = NULL;
	int		ret = 0;

	DEWB_LOG_DEBUG(dbg->level, "_dewb_mirror_new: creating mirror with url: %s, mirrors: %p", url, *mirror);

	new = kcalloc(1, sizeof(*new), GFP_KERNEL);
	if (new == NULL) {
		DEWB_LOG_ERR(dbg->level, "Cannot allocate memory to add a new mirror.");
		ret = -ENOMEM;
		goto end;
	}

	ret = dewb_cdmi_init(dbg, &new->cdmi_desc, url);
	if (ret != 0) {
		DEWB_LOG_ERR(dbg->level, "Could not initialize mirror descriptor (parse URL).");
		goto end;
	}

	if (mirror) {
		*mirror = new;
		new = NULL;
	}

	ret = 0;
end:
	if (new)
		_dewb_mirror_free(new);

	return ret;
}


/*
 * XXX NOTE XXX: #13 This function picks only one mirror that has enough free
 * space in the URL buffer to append the filename.
 */
static int _dewb_mirror_pick(const char *filename, struct dewb_cdmi_desc_s *pick)
//int _dewb_mirror_pick(const char *filename, struct dewb_cdmi_desc_s *pick)
{
	char url[DEWB_URL_SIZE];
	char name[DEWB_URL_SIZE];
	int ret;
	int found = 0;
	dewb_mirror_t *mirror = NULL;

	/* XXX: should have a dewb_debug_t param
	 */
	DEWB_LOG_DEBUG(dewb_log, "_dewb_mirror_pick: picking mirror with filename: %s, with CDMI pick %p", filename, pick);

	spin_lock(&devtab_lock);
	mirror = mirrors;
	while (mirror != NULL)
	{
		DEWB_LOG_INFO(dewb_log, "Browsing mirror: %s", mirror->cdmi_desc.url);
		ret = _dewb_reconstruct_url(url, name,
					    mirror->cdmi_desc.url,
					    mirror->cdmi_desc.filename,
					    filename);
		DEWB_LOG_INFO(dewb_log, "Dewb reconstruct url yielded %s, %i", url, ret);
		if (ret == 0)
		{
			memcpy(pick, &mirror->cdmi_desc,
			       sizeof(mirror->cdmi_desc));
			strncpy(pick->url, url, DEWB_URL_SIZE);
			strncpy(pick->filename, name, DEWB_URL_SIZE);
			DEWB_LOG_INFO(dewb_log, "Copied into pick: url=%s, name=%s", pick->url, pick->filename);
			found = 1;
			break ;
		}
		mirror = mirror->next;
	}
	spin_unlock(&devtab_lock);

	DEWB_LOG_INFO(dewb_log, "Browsed all mirrors");

	if (!found) {
		DEWB_LOG_ERR(dewb_log, "Could not match any mirror for filename %s", filename);
		// No such device or adress seems to match 'missing mirror'
		ret = -ENXIO;
		goto end;
	}

	ret = 0;
end:
	return ret;
}

/* XXX: Respect ISO C90
 *      Fix compilation warning: ISO C90 forbids mixed declarations and code
 */
int dewb_mirror_add(const char *url)
{
	int		ret = 0;
	int		found = 0;
	int		was_first = 0;
	dewb_mirror_t	*cur = NULL;
	dewb_mirror_t	*last = NULL;
	dewb_mirror_t	*new = NULL;
	dewb_debug_t debug;
	struct dewb_cdmi_desc_s *cdmi_desc = NULL;

	DEWB_LOG_INFO(dewb_log, "dewb_mirror_add: adding mirror %s", url);

	//dewb_debug_t debug;
	//struct dewb_cdmi_desc_s *cdmi_desc = NULL;

	debug.name = "<Mirror-Adder>";
	/* TODO: Inherit log level from dewblock (Issue #28)
	 */
	//debug.level = DEWB_DEBUG_LEVEL;
	debug.level = dewb_log;

	if (strlen(url) >= DEWB_URL_SIZE) {
		DEWB_LOG_ERR(dewb_log, "Url too big: '%s'", url);
		ret = -EINVAL;
		goto err_out_dev;
	}

	ret = _dewb_mirror_new(&debug, url, &new);
	if (ret != 0)
		goto err_out_dev;

	cdmi_desc = kcalloc(1, sizeof(*cdmi_desc), GFP_KERNEL);
	if (cdmi_desc == NULL) {
		ret = -ENOMEM;
		goto err_out_mirror_alloc;
	}
	memcpy(cdmi_desc, &new->cdmi_desc, sizeof(new->cdmi_desc));

	spin_lock(&devtab_lock);
	cur = mirrors;
	while (cur != NULL) {
		if (strcmp(url, cur->cdmi_desc.url) == 0)
		{
			found = 1;
			break ;
		}
		last = cur;
		cur = cur->next;
	}
	if (found == 0) {
		if (mirrors == NULL)
			was_first = 1;
		if (last != NULL)
			last->next = new;
		else
			mirrors = new;
		new = NULL;
	}
	spin_unlock(&devtab_lock);

	if (was_first) {
		ret = dewb_cdmi_connect(&debug, cdmi_desc);
		if (ret != 0)
			goto err_out_cdmi_alloc;

		ret = dewb_cdmi_list(&debug, cdmi_desc, &dewb_device_attach);
		if (ret != 0)
			goto err_out_cdmi;

		dewb_cdmi_disconnect(&debug, cdmi_desc);
	}
	kfree(cdmi_desc);

	return 0;

err_out_cdmi:
	dewb_cdmi_disconnect(&debug, cdmi_desc);
err_out_cdmi_alloc:
	kfree(cdmi_desc);
err_out_mirror_alloc:
	_dewb_mirror_free(new);
err_out_dev:

	return ret;
}

int dewb_mirror_remove(const char *url)
{
	int		ret = 0;
	int		found = 0;
	dewb_mirror_t	*cur = NULL;
	dewb_mirror_t	*prev = NULL;

	DEWB_LOG_INFO(dewb_log, "dewb_mirror_remove: removing mirror %s", url);

	if (strlen(url) >= DEWB_URL_SIZE) {
		DEWB_LOG_ERR(dewb_log, "Url too big: '%s'", url);
		ret = -EINVAL;
		goto end;
	}

	// Check if it's the last mirror. If yes, remove all devices.
	/* XXX: Only lock while detaching device
	 */
	//spin_lock(&devtab_lock);
	ret = 0;
	if (mirrors != NULL && mirrors->next == NULL) {
		ret = _dewb_detach_devices();
	}
	//spin_unlock(&devtab_lock);

	if (ret != 0) {
		DEWB_LOG_ERR(dewb_log, "Could not remove all devices; not removing mirror.");
		ret = -EBUSY;
		goto end;
	}

	spin_lock(&devtab_lock);
	cur = mirrors;
	while (cur != NULL) {
		if (strcmp(url, cur->cdmi_desc.url) == 0) {
			found = 1;
			break;
		}
		prev = cur;
		cur = cur->next;
	}
	if (found == 0) {
		DEWB_LOG_ERR(dewb_log, "Cannot remove mirror: Url is not part of mirrors");
		ret = -ENOENT;
	}
	else {
		if (prev)
			prev->next = cur->next;
		else
			mirrors = cur->next;
		_dewb_mirror_free(cur);
	}
	spin_unlock(&devtab_lock);

	ret = 0;
end:
	return ret;
}

ssize_t dewb_mirrors_dump(char *buf, ssize_t max_size)
{
	dewb_mirror_t	*cur = NULL;
	ssize_t		printed = 0;
	ssize_t		len = 0;
	ssize_t		ret = 0;

	DEWB_LOG_INFO(dewb_log, "dewb_mirrors_dump: dumping mirrors: buf: %p, max_size: %ld", buf, max_size);

	spin_lock(&devtab_lock);
	cur = mirrors;
	while (cur) {
		if (printed != 0) {
			len = snprintf(buf + printed, max_size - printed, ",");
			if (len == -1 || len != 1) {
				DEWB_LOG_ERR(dewb_log, "Not enough space to print mirrors list in buffer.");
				ret = -ENOMEM;
				break;
			}
			printed += len;
		}

		len = snprintf(buf + printed, max_size - printed, "%s", cur->cdmi_desc.url);
		if (len == -1 || len > (max_size - printed)) {
			DEWB_LOG_ERR(dewb_log, "Not enough space to print mirrors list in buffer.");
			ret = -ENOMEM;
			break;
		}
		printed += len;

		cur = cur->next;
	}
	spin_unlock(&devtab_lock);

	len = snprintf(buf + printed, max_size - printed, "\n");
	if (len == -1 || len != 1) {
		DEWB_LOG_ERR(dewb_log, "Not enough space to print mirrors list in buffer.");
		ret = -ENOMEM;
	}
	printed += len;

	return ret < 0 ? ret : printed;
}

int dewb_device_detach_by_name(const char *filename)
{
	int ret;
	int i;

	DEWB_LOG_INFO(dewb_log, "dewb_device_detach_by_name: detaching device name %s", filename);

	spin_lock(&devtab_lock);
	for (i = 0; i < DEV_MAX; ++i) {
		if (!device_free_slot(&devtab[i])) {
			/* const char *fname
			    = kbasename(devtab[i].thread_cdmi_desc[0].filename); */
			const char *fname = kbasename(devtab[i].thread_cdmi_desc[0]->filename);
			if (strcmp(filename, fname) == 0) {
				ret = __dewb_device_detach(&devtab[i]);
				if (ret != 0) {
					DEWB_LOG_ERR(dewb_log, "Cannot detach volume automatically.");
					break;
				}
			}
		}
	}
	spin_unlock(&devtab_lock);

	return ret;	
}

int dewb_device_detach_by_id(int dev_id)
{
	dewb_device_t *dev;
	int ret;

	DEWB_LOG_INFO(dewb_log, "dewb_device_detach_by_id: detaching device id %d", dev_id);

	spin_lock(&devtab_lock);

	dev = &devtab[dev_id];
	ret = __dewb_device_detach(dev);

	spin_unlock(&devtab_lock);

	return ret;
}

/* TODO: Remove useless memory allocation
 */
//int dewb_device_attach(const char *filename)
int dewb_device_attach(struct dewb_cdmi_desc_s *cdmi_desc, const char *filename)
{
	dewb_device_t *dev;
	ssize_t rc;
	int irc;
	int i;
	//struct dewb_cdmi_desc_s *cdmi_desc;

	DEWB_LOG_INFO(dewb_log, "dewb_device_attach: attaching filename %s", filename);

	/* cdmi_desc = kmalloc(sizeof(*cdmi_desc), GFP_KERNEL);
	if (cdmi_desc == NULL) {
		rc = -ENOMEM;
		goto err_out_mod;
	} */

	/* Allocate dev structure */
	dev = dewb_device_new();
	if (!dev) {
		rc = -ENOMEM;
		goto err_out_dev;
	}

	init_waitqueue_head(&dev->waiting_wq);
	INIT_LIST_HEAD(&dev->waiting_queue);
	spin_lock_init(&dev->waiting_lock);

	/* set first CDMI struct */
	//cdmi_desc = &dev->thread_cdmi_desc[0];
	//cdmi_desc = dev->thread_cdmi_desc[0];

	/* Pick a convenient mirror to get dewb_cdmi_desc
	 * TODO: #13 We need to manage failover by using every mirror
	 * NB: _dewb_mirror_pick rewrite the cdmi_desc sruct
	 */
	/* NB: do it outside this function
	 */
	rc = _dewb_mirror_pick(filename, cdmi_desc);
	if (rc != 0) { 
		goto err_out_dev;
	}
	DEWB_LOG_INFO(dewb_log, "Adding Device: Picked mirror [ip=%s port=%d fullpath=%s]",
		  cdmi_desc->ip_addr, cdmi_desc->port, cdmi_desc->filename);

	/* set timeout value */
	cdmi_desc->timeout.tv_sec = req_timeout;
	cdmi_desc->timeout.tv_usec = 0;

	/* Parse add command */
	/* TODO: copy CDMI struct to for each thread
	 */
	//for (i = 0; i < DEWB_THREAD_POOL_SIZE; i++) {
	for (i = 0; i < thread_pool_size; i++) {	
		memcpy(dev->thread_cdmi_desc[i], cdmi_desc, sizeof(*cdmi_desc));
		DEWB_LOG_INFO(dewb_log, "thread CDMI timeout: %lu", dev->thread_cdmi_desc[i]->timeout.tv_sec);
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

	DEWB_LOG_INFO(dewb_log, "Added device %s (major:%d) for mirror [ip=%s port=%d fullpath=%s]", 
		dev->name, dev->major, cdmi_desc->ip_addr, cdmi_desc->port, cdmi_desc->filename);

	return rc;

err_out_unregister:
	unregister_blkdev(dev->major, dev->name);
err_out_dev:
	if (NULL != dev)
		dewb_device_free(dev);
	/*if (NULL != cdmi_desc)
		kfree(cdmi_desc); */
//err_out_mod:
	DEWB_LOG_ERR(dewb_log, "Error adding device %s", filename);

	return rc;
}

int dewb_device_create(const char *filename, unsigned long long size)
{
	dewb_debug_t debug;
	struct dewb_cdmi_desc_s *cdmi_desc;
	int rc;

	DEWB_LOG_INFO(dewb_log, "dewb_device_create: creating filename %s of size %llu", filename, size);

	/* TODO: Inherit log level from dewblock LKM (Issue #28)
	 */
	debug.name = NULL;
	//debug.level = 0;
	debug.level = dewb_log;

	cdmi_desc = kmalloc(sizeof(*cdmi_desc), GFP_KERNEL);
	if (cdmi_desc == NULL) {
		rc = -ENOMEM;
		goto err_out_mod;
	}

	/* Now, setup a cdmi connection then Truncate(create) the file. */
	rc = _dewb_mirror_pick(filename, cdmi_desc);
	if (rc != 0)
		goto err_out_alloc;

	rc = dewb_cdmi_connect(&debug, cdmi_desc);
	if (rc != 0)
		goto err_out_alloc;

	rc = dewb_cdmi_create(&debug, cdmi_desc, size);
	if (rc != 0)
		goto err_out_cdmi;

	dewb_cdmi_disconnect(&debug, cdmi_desc);

	//rc = dewb_device_attach(filename);
	rc = dewb_device_attach(cdmi_desc, filename);
	if (rc != 0) {
		DEWB_LOG_ERR(dewb_log, "Cannot add created volume automatically.");
		goto err_out_cdmi;
	}

	kfree(cdmi_desc);

	return rc;

err_out_cdmi:
	dewb_cdmi_disconnect(&debug, cdmi_desc);
err_out_alloc:
	kfree(cdmi_desc);
err_out_mod:
	DEWB_LOG_ERR(dewb_log, "Error creating device %s", filename);

	return rc;
}

int dewb_device_extend(const char *filename, unsigned long long size)
{
	dewb_debug_t debug;
	struct dewb_cdmi_desc_s *cdmi_desc = NULL;
	int i;
	int rc;

	DEWB_LOG_INFO(dewb_log, "dewb_device_extend: extending filename %s to %llu size", filename, size);

	/* TODO: Inherit log level from dewblock LKM (Issue #28)
	 */
	debug.name = NULL;
	//debug.level = 0;
	debug.level = dewb_log;

	cdmi_desc = kmalloc(sizeof(*cdmi_desc), GFP_KERNEL);
	if (cdmi_desc == NULL) {
		rc = -ENOMEM;
		goto err_out_mod;
	}

	/* Now, setup a cdmi connection then Truncate(create) the file. */
	rc = _dewb_mirror_pick(filename, cdmi_desc);
	if (rc != 0)
		goto err_out_alloc;

	rc = dewb_cdmi_connect(&debug, cdmi_desc);
	if (rc != 0)
		goto err_out_alloc;

	rc = dewb_cdmi_extend(&debug, cdmi_desc, size);
	if (rc != 0)
		goto err_out_cdmi;

	dewb_cdmi_disconnect(&debug, cdmi_desc);

	kfree(cdmi_desc);

	// Find device (normally only 1) associated to filename and update their size
	spin_lock(&devtab_lock);
	for (i = 0; i < DEV_MAX; ++i)
	{
		if (!device_free_slot(&devtab[i]))
		{
			/* const char *fname
			    = kbasename(devtab[i].thread_cdmi_desc[0].filename); */
			const char *fname
			    = kbasename(devtab[i].thread_cdmi_desc[0]->filename);
			if (strcmp(filename, fname) == 0)
			{
				devtab[i].disk_size = size;
				set_capacity(devtab[i].disk, devtab[i].disk_size / 512ULL);
				break ;
			}
		}
	}
	spin_unlock(&devtab_lock);

	return rc;

err_out_cdmi:
	dewb_cdmi_disconnect(&debug, cdmi_desc);
err_out_alloc:
	kfree(cdmi_desc);
err_out_mod:
	DEWB_LOG_ERR(dewb_log, "Error creating device %s", filename);

	return rc;
}

int dewb_device_destroy(const char *filename)
{
	dewb_debug_t debug;
	struct dewb_cdmi_desc_s *cdmi_desc = NULL;
	int rc;
	int remove_err = 0;
	int i;

	DEWB_LOG_INFO(dewb_log, "dewb_device_destroy: destroying filename: %s", filename);

	/* TODO: Inherit log level from dewblock LKM (Issue #8)
	 */
	debug.name = NULL;
	//debug.level = 0;
	debug.level = dewb_log;

	cdmi_desc = kmalloc(sizeof(struct dewb_cdmi_desc_s), GFP_KERNEL);
	if (cdmi_desc == NULL) {
		DEWB_LOG_ERR(dewb_log, "unable to allocate memory for temporary CDMI");
		rc = -ENOMEM;
		goto err_out_mod;
	}

	// Remove every device (normally only 1) associated to filename
	spin_lock(&devtab_lock);
	for (i = 0; i < DEV_MAX; ++i) {
		if (!device_free_slot(&devtab[i])) {
			/* const char *fname
			    = kbasename(devtab[i].thread_cdmi_desc[0].filename); */
			const char *fname = kbasename(devtab[i].thread_cdmi_desc[0]->filename);
			if (strcmp(filename, fname) == 0) {
				rc = __dewb_device_detach(&devtab[i]);
				if (rc != 0) {
					DEWB_LOG_ERR(dewb_log, "Cannot remove created"
						   " volume automatically: %s", fname);
					remove_err = 1;
					break;
				}
			}
		}
	}
	spin_unlock(&devtab_lock);

	if (remove_err) {
		DEWB_LOG_ERR(dewb_log, "Could not remove every device associated to "
			   "volume %s", filename);
		goto err_out_alloc;
	}


	/* First, setup a cdmi connection then Delete the file. */
	rc = _dewb_mirror_pick(filename, cdmi_desc);
	if (rc != 0)
		goto err_out_alloc;

	rc = dewb_cdmi_connect(&debug, cdmi_desc);
	if (rc != 0)
		goto err_out_alloc;

	rc = dewb_cdmi_delete(&debug, cdmi_desc);
	if (rc != 0)
		goto err_out_cdmi;

	dewb_cdmi_disconnect(&debug, cdmi_desc);
	kfree(cdmi_desc);

	return rc;

err_out_cdmi:
	dewb_cdmi_disconnect(&debug, cdmi_desc);
err_out_alloc:
	kfree(cdmi_desc);
err_out_mod:
	DEWB_LOG_ERR(dewb_log, "Error destroying volume %s", filename);

	return rc;
}

static int __init dewblock_init(void)
{
	int rc;

	DEWB_LOG_NOTICE(dewb_log, "Initializing %s block device driver version %s", DEV_NAME, DEV_REL_VERSION);

	/* Zeroing device tab */
	memset(devtab, 0, sizeof(devtab));

	rc = dewb_sysfs_init();
	if (rc) {
		DEWB_LOG_ERR(dewb_log, "Failed to initialize with code: %d", rc);
		return rc;
	}

	return 0;
}
 
static void __exit dewblock_cleanup(void)
{
	DEWB_LOG_NOTICE(dewb_log, "Cleaning up %s block device driver", DEV_NAME);

	/* XXX: Only lock while detaching device
	 */
	//spin_lock(&devtab_lock);
	//(void)_dewb_detach_devices();
	_dewb_detach_devices();
	//spin_unlock(&devtab_lock);

	dewb_sysfs_cleanup();
}

module_init(dewblock_init);
module_exit(dewblock_cleanup);
