/*
 * Copyright (C) 2014 SCALITY SA - http://www.scality.com
 *
 * This file is part of ScalityRestBlock.
 *
 * ScalityRestBlock is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ScalityRestBlock is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ScalityRestBlock.  If not, see <http://www.gnu.org/licenses/>.
 *
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

#include "srb.h"


// LKM information
MODULE_AUTHOR("Laurent Meyer <laurent.meyer@digitam.net>");
MODULE_AUTHOR("David Pineau <david.pineau@scality.com>");
MODULE_DESCRIPTION("Block Device Driver for REST-based storage");
MODULE_LICENSE("GPL");
MODULE_VERSION(DEV_REL_VERSION);

static srb_device_t	devtab[DEV_MAX];
static srb_server_t	*servers = NULL;
static DEFINE_SPINLOCK(devtab_lock);

/* Module parameters (LKM parameters)
 */
unsigned short srb_log = SRB_LOG_LEVEL_DFLT;
unsigned short req_timeout = SRB_REQ_TIMEOUT_DFLT;
unsigned short nb_req_retries = SRB_NB_REQ_RETRIES_DFLT;
unsigned short server_conn_timeout = SRB_CONN_TIMEOUT_DFLT;
unsigned int thread_pool_size = SRB_THREAD_POOL_SIZE_DFLT;
MODULE_PARM_DESC(debug, "Global log level for ScalityRestBlock LKM");
module_param_named(debug, srb_log, ushort, 0644);

MODULE_PARM_DESC(req_timeout, "Global timeout for request");
module_param(req_timeout, ushort, 0644);

MODULE_PARM_DESC(nb_req_retries, "Global number of retries for request");
module_param(nb_req_retries, ushort, 0644);

MODULE_PARM_DESC(server_conn_timeout, "Global timeout for connection to server(s)");
module_param(server_conn_timeout, ushort, 0644);

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
int srb_xfer_scl(struct srb_device_s *dev,
		struct srb_cdmi_desc_s *desc,
		struct request *req)
{
	int ret = 0;
	struct timeval tv_start;
	struct timeval tv_end;

	SRB_LOG_DEBUG(dev->debug.level, "srb_xfer_scl: CDMI request %p (%s) for device %p with cdmi %p",
		req, req_code_to_str(rq_data_dir(req)), dev, desc);

	if (SRB_DEBUG <= dev->debug.level)
		do_gettimeofday(&tv_start);

        if (rq_data_dir(req) == WRITE) {
                ret = srb_cdmi_putrange(&dev->debug,
                                        desc,
                                        blk_rq_pos(req) * 512ULL,
                                        blk_rq_sectors(req) * 512ULL);
        }
        else {
                ret = srb_cdmi_getrange(&dev->debug,
                                        desc,
                                        blk_rq_pos(req) * 512ULL,
                                        blk_rq_sectors(req) * 512ULL);
        }

	if (SRB_DEBUG <= dev->debug.level) {
		do_gettimeofday(&tv_end);
		SRB_LOG_DEBUG(dev->debug.level, "cdmi request time: %ldms",
			(tv_end.tv_sec - tv_start.tv_sec)*1000 + (tv_end.tv_usec - tv_start.tv_usec)/1000);
	}

	if (ret) {
		SRB_LOG_ERR(dev->debug.level, "CDMI Request using scatterlist failed with IO error: %d", ret);
		return -EIO;
	}

	return ret;
}

/*
 * Free internal disk
 */
static int srb_free_disk(struct srb_device_s *dev)
{
	struct gendisk *disk = NULL;

	disk = dev->disk;
	if (!disk) {
		SRB_LOG_ERR(dev->debug.level, "%s: Disk is no more available", dev->name);
		return -EINVAL;
	}
	dev->disk = NULL;

	/* free disk */
	if (disk->flags & GENHD_FL_UP) {
		del_gendisk(disk);
		if (disk->queue)
			blk_cleanup_queue(disk->queue);
	}

	put_disk(disk);

	return 0;
}

/*
 * Thread for srb
 */
static int srb_thread(void *data)
{
	struct srb_device_s *dev;
	struct request *req;
	unsigned long flags;
	int th_id;
	int th_ret = 0;
	char buff[256];
	struct req_iterator iter;
	struct bio_vec *bvec;
	struct srb_cdmi_desc_s *cdmi_desc;

	SRB_LOG_DEBUG(((struct srb_device_s *)data)->debug.level, "srb_thread: thread function with data %p", data);

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

		/* TODO: improve kthread termination, otherwise calling we can not
		  terminate a kthread calling kthread_stop() */
		/* if (kthread_should_stop()) {
			printk(KERN_INFO "srb_thread: immediate kthread exit\n");
			do_exit(0);
		} */

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
		
		if (blk_rq_sectors(req) == 0) {
			blk_end_request_all(req, 0);
			continue;
		}

		req_flags_to_str(req->cmd_flags, buff);
		SRB_LOG_DEBUG(dev->debug.level, "srb_thread: thread %d: New REQ of type %s (%d) flags: %s (%llu)",
			th_id, req_code_to_str(rq_data_dir(req)), rq_data_dir(req), buff, req->cmd_flags);
		if (req->cmd_flags & REQ_FLUSH) {
			SRB_LOG_DEBUG(dev->debug.level, "DEBUG CMD REQ_FLUSH\n");
		}
		/* XXX: Use iterator instead of internal function (cf linux/blkdev.h)
		 *  __rq_for_each_bio(bio, req) {
		 */
		rq_for_each_segment(bvec, req, iter) {
			if (iter.bio->bi_rw & REQ_FLUSH) {
				SRB_LOG_DEBUG(dev->debug.level, "DEBUG VR BIO REQ_FLUSH\n");
			}
		}

		/* Create scatterlist */
		cdmi_desc = dev->thread_cdmi_desc[th_id];
		sg_init_table(dev->thread_cdmi_desc[th_id]->sgl, DEV_NB_PHYS_SEGS);
		dev->thread_cdmi_desc[th_id]->sgl_size = blk_rq_map_sg(dev->q, req, dev->thread_cdmi_desc[th_id]->sgl);

		SRB_LOG_DEBUG(dev->debug.level, "srb_thread: scatter_list size %d [nb_seg = %d, sector = %lu, nr_sectors=%u w=%d]",
			DEV_NB_PHYS_SEGS, dev->thread_cdmi_desc[th_id]->sgl_size, blk_rq_pos(req), blk_rq_sectors(req), rq_data_dir(req) == WRITE);

		/* Call scatter function */
		th_ret = srb_xfer_scl(dev, dev->thread_cdmi_desc[th_id], req);

		//SRB_DEV_DEBUG("END REQUEST [tid:%d]", th_id);
		SRB_LOG_DEBUG(dev->debug.level, "srb_thread: thread %d: REQ done with returned code %d",
			th_id, th_ret);;
	
		/* No IO error testing for the moment */
		blk_end_request_all(req, 0);
	}

	return 0;
}


static void srb_rq_fn(struct request_queue *q)
{
	struct srb_device_s *dev = q->queuedata;	
	struct request *req;
	unsigned long flags;

	while ((req = blk_fetch_request(q)) != NULL) {
		if (req->cmd_type != REQ_TYPE_FS) {
			SRB_LOG_DEBUG(dev->debug.level, "srb_rq_fn: Skip non-CMD request");

			__blk_end_request_all(req, -EIO);
			continue;
		}

		spin_lock_irqsave(&dev->waiting_lock, flags);
		list_add_tail(&req->queuelist, &dev->waiting_queue);
		spin_unlock_irqrestore(&dev->waiting_lock, flags);
		wake_up_nr(&dev->waiting_wq, 1);
	}
}

static int srb_open(struct block_device *bdev, fmode_t mode)
{
	//srb_device_t *dev = bdev->bd_disk->private_data;
	srb_device_t *dev;

	SRB_LOG_INFO(srb_log, "srb_open: opening block device %s", bdev->bd_disk->disk_name);

	dev = bdev->bd_disk->private_data;

	spin_lock(&devtab_lock);
	dev->users++;
	spin_unlock(&devtab_lock);

	return 0;
}

/*
 * After linux kernel v3.10, this function stops returning anything
 * (becomes void). For simplicity, we currently don't support earlier kernels.
 */
static void srb_release(struct gendisk *disk, fmode_t mode)
{
	srb_device_t *dev;

	SRB_LOG_INFO(srb_log, "srb_release: releasing disk %s", disk->disk_name);

	dev = disk->private_data;

	spin_lock(&devtab_lock);
	dev->users--;
	spin_unlock(&devtab_lock);
}

static const struct block_device_operations srb_fops =
{
	.owner   =	THIS_MODULE,
	.open	 =	srb_open,
	.release =	srb_release,
};

static int srb_init_disk(struct srb_device_s *dev)
{
	struct gendisk *disk = NULL;
	struct request_queue *q;
	int i;
	int ret = 0;

	SRB_LOG_INFO(srb_log, "srb_init_disk: initializing disk for device: %s", dev->name);

	/* create gendisk info */
	disk = alloc_disk(DEV_MINORS);
	if (!disk) {
		SRB_LOG_WARN(srb_log, "srb_init_disk: unable to allocate memory for disk for device: %s",
			dev->name);
		return -ENOMEM;
	}
	SRB_LOG_DEBUG(srb_log, "Creating new disk: %p", disk);

	strcpy(disk->disk_name, dev->name);
	disk->major	   = dev->major;
	disk->first_minor  = 0;
	disk->fops	   = &srb_fops;
	disk->private_data = dev;

	/* init rq */
	q = blk_init_queue(srb_rq_fn, &dev->rq_lock);
	if (!q) {
		SRB_LOG_WARN(srb_log, "srb_init_disk: unable to init block queue for device: %p, disk: %p",
			dev, disk);
		//put_disk(disk);
		srb_free_disk(dev);
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
	//for (i = 0; i < SRB_THREAD_POOL_SIZE; i++) {
	for (i = 0; i < thread_pool_size; i++) {
		//if ((ret = srb_cdmi_connect(&dev->debug, &dev->thread_cdmi_desc[i]))) {
		if ((ret = srb_cdmi_connect(&dev->debug, dev->thread_cdmi_desc[i]))) {
			SRB_LOG_ERR(srb_log, "Unable to connect to CDMI endpoint: %d",
				ret);
			//put_disk(disk);
			srb_free_disk(dev);
			return -EIO;
		}
	}
	/* Caution: be sure to call this before spawning threads */
	//ret = srb_cdmi_getsize(&dev->debug, &dev->thread_cdmi_desc[0],
	ret = srb_cdmi_getsize(&dev->debug, dev->thread_cdmi_desc[0], &dev->disk_size);
	if (ret != 0) {
		SRB_LOG_ERR(srb_log, "Could not retrieve volume size.");
		//put_disk(disk);
		srb_free_disk(dev);
		return ret;
	}

	set_capacity(disk, dev->disk_size / 512ULL);

	//TODO: Make thread pool variable (Issue #33)
	//for (i = 0; i < SRB_THREAD_POOL_SIZE; i++) {
	for (i = 0; i < thread_pool_size; i++) {
		dev->thread[i] = kthread_create(srb_thread, dev, "%s",
						dev->disk->disk_name);
		if (IS_ERR(dev->thread[i])) {
			SRB_LOG_ERR(srb_log, "Unable to create worker thread (id %d)", i);
			//put_disk(disk);
			dev->thread[i] = NULL;
			srb_free_disk(dev);
			goto err_kthread;
		}
		wake_up_process(dev->thread[i]);
	}
	add_disk(disk);

	SRB_LOG_INFO(srb_log, "%s: Added of size 0x%llx",
		disk->disk_name, (unsigned long long)dev->disk_size);

	return 0;

err_kthread:
	for (i = 0; i < thread_pool_size; i++) {
		if (dev->thread[i] != NULL)
			kthread_stop(dev->thread[i]);
	}

	return -EIO;
}
#define device_free_slot(X) ((X)->name[0] == 0)


/* This function gets the next free slot in device tab (devtab)
** and sets its name and id.
** Note : all the remaining fields states are undefived, it is
** the caller responsability to set them.
*/
static int srb_device_new(const char *devname, srb_device_t *dev)
{
	int ret = -EINVAL;
	int i;

	SRB_LOG_INFO(srb_log, "srb_device_new: creating new device %s"
		      " with %d threads", devname, thread_pool_size);

	if (NULL == dev) {
		ret = -EINVAL;
		goto out;
	}

	if (NULL == devname || strlen(devname) >= DISK_NAME_LEN) {
		SRB_LOG_ERR(srb_log, "srb_device_new: "
			     "Invalid (or too long) device name '%s'",
			     devname == NULL ? "" : devname);
		ret = -EINVAL;
		goto out;
	}

	/* Lock table to protect against concurrent devices
	 * creation
	 */
	dev->debug.name = &dev->name[0];
	dev->debug.level = srb_log;
	dev->users = 0;
	strncpy(dev->name, devname, strlen(devname));

	/* XXX: dynamic allocation of thread pool and cdmi connection pool
	 * NB: The memory allocation for the thread is an array of pointer
	 *     whereas the allocation for the cdmi connection pool is an array
	 *     of cdmi connection structure
	 */
	dev->thread_cdmi_desc = kmalloc(sizeof(struct srb_cdmi_desc_s *) * thread_pool_size, GFP_KERNEL);
	if (dev->thread_cdmi_desc == NULL) {
		SRB_LOG_CRIT(srb_log, "srb_device_new: Unable to allocate memory for CDMI struct pointer");
		ret = -ENOMEM;
		goto err_mem;
	}
	for (i = 0; i < thread_pool_size; i++) {
		dev->thread_cdmi_desc[i] = kmalloc(sizeof(struct srb_cdmi_desc_s), GFP_KERNEL);
		if (dev->thread_cdmi_desc[i] == NULL) {
			SRB_LOG_CRIT(srb_log, "srb_device_new: Unable to allocate memory for CDMI struct, step %d", i);
			ret = -ENOMEM;
			goto err_mem;
		}
	}
	dev->thread = kmalloc(sizeof(struct task_struct *) * thread_pool_size, GFP_KERNEL);
	if (dev->thread == NULL) {
		SRB_LOG_CRIT(srb_log, "srb_device_new: Unable to allocate memory for kernel thread struct");
		ret = -ENOMEM;
		goto err_mem;
	}

	return 0;

err_mem:
	if (NULL != dev && NULL != dev->thread_cdmi_desc) {
		for (i = 0; i < thread_pool_size; i++) {
			if (dev->thread_cdmi_desc[i])
				kfree(dev->thread_cdmi_desc[i]);
		}
		kfree(dev->thread_cdmi_desc);
	}
out:
	return ret;
}

/* This helper marks the given device slot as empty
** CAUTION: the devab lock must be held
*/
static void __srb_device_free(srb_device_t *dev)
{
	SRB_LOG_INFO(srb_log, "__srb_device_free: freeing device: %s", dev->name);

	memset(dev->name, 0, DISK_NAME_LEN);
	dev->major = 0;
	dev->id = -1;
}

static void srb_device_free(srb_device_t *dev)
{
	int i = 0;

	SRB_LOG_INFO(srb_log, "srb_device_free: freeing device: %s", dev->name);

	__srb_device_free(dev);

	/* TODO: free kernel memory for CDMI struct (Issue #33)
	 */
	if (dev->thread_cdmi_desc) {
		for (i = 0; i < thread_pool_size; i++) {
			if (dev->thread_cdmi_desc[i])
				kfree(dev->thread_cdmi_desc[i]);
		}
		kfree(dev->thread_cdmi_desc);
	}
	if (dev->thread)
		kfree(dev->thread);
}

static int _srb_reconstruct_url(char *url, char *name,
				 const char *baseurl, const char *basepath,
				 const char *filename)
{
	int urllen = 0;
	int namelen = 0;
	int seplen = 0;

	SRB_LOG_DEBUG(srb_log, "_srb_reconstruct_url: construction of URL with url: %s, name: %s, baseurl: %s, basepath: %p, filename: %s",
		url, name, baseurl, basepath, filename);

	urllen = strlen(baseurl);
	if (baseurl[urllen - 1] != '/')
		seplen = 1;

	if (seplen)
	{
		urllen = snprintf(url, SRB_URL_SIZE, "%s/%s", baseurl, filename);
		namelen = snprintf(name, SRB_URL_SIZE, "%s/%s", basepath, filename);
	}
	else
	{
		urllen = snprintf(url, SRB_URL_SIZE, "%s%s", baseurl, filename);
		namelen = snprintf(name, SRB_URL_SIZE, "%s%s", basepath, filename);
	}

	if (urllen >= SRB_URL_SIZE || namelen >= SRB_URL_SIZE)
		return -EINVAL;

	return 0;
}

static int __srb_device_detach(srb_device_t *dev)
{
	int i;
	int ret = 0;

	SRB_LOG_DEBUG(srb_log, "__srb_device_detach: detaching device %s (%p)", dev->name, dev);

	if (!dev) {
		SRB_LOG_WARN(srb_log, "__srb_device_detach: empty device");
		return -EINVAL;
	}

	if (dev->users > 0) {
		SRB_LOG_ERR(srb_log, "%s: Unable to remove, device still opened (#users: %d)", dev->name, dev->users);
		return -EBUSY;
	}

	if (!dev->disk) {
		SRB_LOG_ERR(srb_log, "%s: Disk is no more available", dev->name);
		return -EINVAL;
	}

	SRB_LOG_INFO(srb_log, "%s: Removing disk", dev->disk->disk_name);

	/* TODO: Make thread pool variable (Issue #33)
	 */
	for (i = 0; i < thread_pool_size; i++) {
		if (dev->thread[i])
			kthread_stop(dev->thread[i]);
	}

	/* free disk */
	ret = srb_free_disk(dev);
	if (0 != ret) {
		SRB_LOG_WARN(srb_log, "%s: Failed to remove disk: %d", dev->name, ret);
	}

	SRB_LOG_INFO(srb_log, "%s: Removing disk for major %d", dev->name, dev->major);
	/* Remove device */
	unregister_blkdev(dev->major, DEV_NAME);

	/* Mark slot as empty */
	if (NULL != dev)
		srb_device_free(dev);

	return 0;
}

static int _srb_detach_devices(void)
{
	int ret;
	int i = 0;
	int errcount = 0;
	int dev_in_use[DEV_MAX];

	SRB_LOG_INFO(srb_log, "_srb_detach_devices: detaching devices");

	/* mark all device that are not used */
	spin_lock(&devtab_lock);
	for (i = 0; i < DEV_MAX; ++i) {
		dev_in_use[i] = 0;
		if (!device_free_slot(&devtab[i])) {
			if (devtab[i].state == DEV_IN_USE) {
				dev_in_use[i] = 1;	
			} else {
				devtab[i].state = DEV_IN_USE;
				dev_in_use[i] = 2;
			}
		}
	}
	spin_unlock(&devtab_lock);

	/* detach all marked devices */
	for (i = 0; i < DEV_MAX; ++i) {
		if (2 == dev_in_use[i]) {
			ret = __srb_device_detach(&devtab[i]);
			if (ret != 0) {
				SRB_LOG_ERR(srb_log, "Could not remove device %s for volume at unload %s",
					devtab[i].name, devtab[i].thread_cdmi_desc ? devtab[i].thread_cdmi_desc[0]->filename : "NULL");
				errcount++;
			}
		}
	}

	/* mark all device that are not used */
	spin_lock(&devtab_lock);
	for (i = 0; i < DEV_MAX; ++i) {
		if (2 == dev_in_use[i]) {
			devtab[i].state = DEV_UNUSED;
		}
	}
	spin_unlock(&devtab_lock);

	return errcount;
}

static void _srb_server_free(srb_server_t *server)
{
	SRB_LOG_DEBUG(srb_log, "_srb_server_free: deleting server url %s (%p)", server->cdmi_desc.url, server);

	if (server) {
		server->next = NULL;
		kfree(server);
		server = NULL;
	}
}

static int _srb_server_new(srb_debug_t *dbg, const char *url, srb_server_t **server)
{
	srb_server_t	*new = NULL;
	int		ret = 0;

	SRB_LOG_DEBUG(dbg->level, "_srb_server_new: creating server with url: %s, servers: %p", url, *server);

	new = kcalloc(1, sizeof(struct srb_server_s), GFP_KERNEL);
	if (new == NULL) {
		SRB_LOG_ERR(dbg->level, "Cannot allocate memory to add a new server.");
		ret = -ENOMEM;
		goto end;
	}

	ret = srb_cdmi_init(dbg, &new->cdmi_desc, url);
	if (ret != 0) {
		SRB_LOG_ERR(dbg->level, "Could not initialize server descriptor (parse URL).");
		goto end;
	}

	if (server) {
		new->next = NULL;
		*server = new;
	}

	return 0;
end:
	_srb_server_free(new);

	return ret;
}


/*
 * XXX NOTE XXX: #13 This function picks only one server that has enough free
 * space in the URL buffer to append the filename.
 */
static int _srb_server_pick(const char *filename, struct srb_cdmi_desc_s *pick)
{
	char url[SRB_URL_SIZE];
	char name[SRB_URL_SIZE];
	int ret;
	int found = 0;
	srb_server_t *server = NULL;

	SRB_LOG_DEBUG(srb_log, "_srb_server_pick: picking server with filename: %s, with CDMI pick %p", filename, pick);

	spin_lock(&devtab_lock);
	server = servers;
	while (server != NULL) {
		SRB_LOG_INFO(srb_log, "Browsing server: %s", server->cdmi_desc.url);
		ret = _srb_reconstruct_url(url, name,
					    server->cdmi_desc.url,
					    server->cdmi_desc.filename,
					    filename);
		SRB_LOG_INFO(srb_log, "Dewb reconstruct url yielded %s, %i", url, ret);
		if (ret == 0) {
			memcpy(pick, &server->cdmi_desc, sizeof(struct srb_cdmi_desc_s));
			strncpy(pick->url, url, SRB_URL_SIZE);
			strncpy(pick->filename, name, SRB_URL_SIZE);
			SRB_LOG_INFO(srb_log, "Copied into pick: url=%s, name=%s", pick->url, pick->filename);
			found = 1;
			break ;
		}
		server = server->next;
	}
	spin_unlock(&devtab_lock);

	SRB_LOG_INFO(srb_log, "Browsed all servers");

	if (!found) {
		SRB_LOG_ERR(srb_log, "Could not match any server for filename %s", filename);
		// No such device or adress seems to match 'missing server'
		ret = -ENXIO;
		goto end;
	}

	ret = 0;
end:
	return ret;
}

/* XXX: Respect ISO C90
 *      Fix compilation warning: ISO C90 forbids mixed declarations and code
 *      Fix design: only create new server if not found in the list
 */
int srb_server_add(const char *url)
{
	int		ret = 0;
	int		found = 0;
	srb_server_t	*cur = NULL;
	srb_server_t	*last = NULL;
	srb_server_t	*new = NULL;
	srb_debug_t	debug;

	SRB_LOG_INFO(srb_log, "srb_server_add: adding server %s", url);

	debug.name = "<Server-Url-Adder>";
	debug.level = srb_log;

	if (strlen(url) >= SRB_URL_SIZE) {
		SRB_LOG_ERR(srb_log, "Url too big: '%s'", url);
		ret = -EINVAL;
		goto err_out_dev;
	}

	ret = _srb_server_new(&debug, url, &new);
	if (ret != 0)
		goto err_out_dev;

	spin_lock(&devtab_lock);
	cur = servers;
	while (cur != NULL) {
		if (strcmp(url, cur->cdmi_desc.url) == 0) {
			found = 1;
			break;
		}
		last = cur;
		cur = cur->next;
	}
	if (found == 0) {
		if (last != NULL)
			last->next = new;
		else
			servers = new;
		new = NULL;
	}
	spin_unlock(&devtab_lock);

	if (found)
		_srb_server_free(new);

	return 0;

err_out_dev:

	return ret;
}

static int _locked_server_remove(const char *url)
{
	int		ret = 0;
	int		i;
	int		found = 0;
	srb_server_t	*cur = NULL;
	srb_server_t	*prev = NULL;

	cur = servers;
	while (cur != NULL) {
		if (strcmp(url, cur->cdmi_desc.url) == 0) {
			found = 1;
			break;
		}
		prev = cur;
		cur = cur->next;
	}

	if (found == 0) {
		SRB_LOG_ERR(srb_log, "Cannot remove server: "
			     "Url is not part of servers");
		ret = -ENOENT;
		goto end;
	}

	/* Only one server == Last server, make sure there are no devices
	 * attached anymore before removing */
	if (servers != NULL && servers->next == NULL) {
		for (i = 0; i < DEV_MAX; ++i) {
			if (!device_free_slot(&devtab[i])) {
				SRB_LOG_ERR(srb_log,
					     "Could not remove all devices; "
					     "not removing server.");
				ret = -EBUSY;
				goto end;
			}
		}
	}


	if (prev)
		prev->next = cur->next;
	else
		servers = cur->next;

	_srb_server_free(cur);

	ret = 0;

end:
	return ret;
}

int srb_server_remove(const char *url)
{
	int		ret = 0;

	SRB_LOG_INFO(srb_log, "srb_server_remove: removing server %s", url);

	if (strlen(url) >= SRB_URL_SIZE) {
		SRB_LOG_ERR(srb_log, "Url too big: '%s'", url);
		ret = -EINVAL;
		goto end;
	}

	spin_lock(&devtab_lock);
	ret = _locked_server_remove(url);
	spin_unlock(&devtab_lock);

end:
	return ret;
}

ssize_t srb_servers_dump(char *buf, ssize_t max_size)
{
	srb_server_t	*cur = NULL;
	ssize_t		printed = 0;
	ssize_t		len = 0;
	ssize_t		ret = 0;

	SRB_LOG_INFO(srb_log, "srb_servers_dump: dumping servers: buf: %p, max_size: %ld", buf, max_size);

	spin_lock(&devtab_lock);
	cur = servers;
	while (cur) {
		if (printed != 0) {
			len = snprintf(buf + printed, max_size - printed, ",");
			if (len == -1 || len != 1) {
				SRB_LOG_ERR(srb_log, "Not enough space to print servers list in buffer.");
				ret = -ENOMEM;
				break;
			}
			printed += len;
		}

		len = snprintf(buf + printed, max_size - printed, "%s", cur->cdmi_desc.url);
		if (len == -1 || len > (max_size - printed)) {
			SRB_LOG_ERR(srb_log, "Not enough space to print servers list in buffer.");
			ret = -ENOMEM;
			break;
		}
		printed += len;

		cur = cur->next;
	}
	spin_unlock(&devtab_lock);

	len = snprintf(buf + printed, max_size - printed, "\n");
	if (len == -1 || len != 1) {
		SRB_LOG_ERR(srb_log, "Not enough space to print servers list in buffer.");
		ret = -ENOMEM;
	}
	printed += len;

	return ret < 0 ? ret : printed;
}

struct cdmi_volume_list_data {
	char	*buf;
	size_t	max_size;
	size_t	printed;
};
int _srb_volume_dump(struct cdmi_volume_list_data *cb_data, const char *volname)
{
	int ret;
	int len;

	SRB_LOG_DEBUG(srb_log, "Adding volume %s to listing", volname);
	if (cb_data->printed + strlen(volname) + 1 < cb_data->max_size)
	{
		len = snprintf(cb_data->buf + cb_data->printed,
			       cb_data->max_size - cb_data->printed,
			       "%s\n", volname);
		if (len == -1
		    || len > cb_data->max_size - cb_data->printed) {
			SRB_LOG_ERR(srb_log, "Not enough space to print"
				     " volume list in buffer.");
			ret = -ENOMEM;
			goto end;
		}
		cb_data->printed += len;
	}

	ret = 0;

end:
	return ret;
}

int srb_volumes_dump(char *buf, size_t max_size)
{
	int			ret = 0;
	int			connected = 0;
	struct srb_cdmi_desc_s *cdmi_desc;
	struct cdmi_volume_list_data cb_data = { buf, max_size, 0};
	srb_debug_t debug;

	SRB_LOG_INFO(srb_log, "srb_volumes_dump: dumping volumes: buf: %p, max_size: %ld", buf, max_size);

	cdmi_desc = kmalloc(sizeof(*cdmi_desc), GFP_KERNEL);
	if (cdmi_desc == NULL) {
		ret = -ENOMEM;
		goto cleanup;
	}

	/* Find server for directory (filename must be empty, not NULL) */
	ret = _srb_server_pick("", cdmi_desc);
	if (ret != 0) {
		SRB_LOG_ERR(srb_log, "Unable to get server: %i", ret);
		goto cleanup;
	}
	SRB_LOG_INFO(srb_log, "Dumping volumes: Picked server "
		      "[ip=%s port=%d fullpath=%s]",
		      cdmi_desc->ip_addr, cdmi_desc->port,
		      cdmi_desc->filename);

	/* Inherit log level from srb LKM */
	debug.name = "<Volumes Dumper>";
	debug.level = srb_log;

	ret = srb_cdmi_connect(&debug, cdmi_desc);
	if (ret != 0)
		goto cleanup;
	connected = 1;

	ret = srb_cdmi_list(&debug, cdmi_desc,
			     (srb_cdmi_list_cb)_srb_volume_dump, (void*)&cb_data);
	if (ret != 0)
		goto cleanup;

	ret = 0;

cleanup:
	if (NULL != cdmi_desc)
	{
		if (connected)
			srb_cdmi_disconnect(&debug, cdmi_desc);
		kfree(cdmi_desc);
	}

	return ret < 0 ? ret : cb_data.printed;
}

int srb_device_detach(const char *devname)
{
	int ret = 0;
	int i;
	int found = 0;
	struct srb_device_s *dev = NULL;

	SRB_LOG_INFO(srb_log, "srb_device_detach: detaching device name %s",
		      devname);

	/* get device to detach and mark it */
	spin_lock(&devtab_lock);
	for (i = 0; i < DEV_MAX; ++i) {
		if (!device_free_slot(&devtab[i])) {
			if (strcmp(devname, devtab[i].name) == 0) {
				found = 1;
				if (devtab[i].state == DEV_IN_USE) {
					ret = -EBUSY;
				} else {
					dev = &devtab[i];
					/* mark it as ongoing operation */
					dev->state = DEV_IN_USE;
					ret = 0;
				}
				break;
			}
		}
	}
	spin_unlock(&devtab_lock);

	/* check status */
	if (1 == found && 0 != ret) {
		SRB_LOG_ERR(srb_log, "Device %s in use", devname);
		return ret;
	}

	/* real stuff */
	if (1 == found) {
		ret = __srb_device_detach(&devtab[i]);
		if (ret != 0) {
			SRB_LOG_ERR(srb_log, "Cannot detach device %s", devname);
		}
	} else {
		SRB_LOG_ERR(srb_log, "Device %s not found as attached", devname);
		return -EINVAL;
	}

	/* mark device as unsued == available */
	spin_lock(&devtab_lock);
	dev->state = DEV_UNUSED;
	spin_unlock(&devtab_lock);

	return ret;
}

/* TODO: Remove useless memory allocation
 */
int srb_device_attach(const char *filename, const char *devname)
{
	srb_device_t *dev = NULL;
	int rc = 0;
	int i;
	int do_unregister = 0;
	struct srb_cdmi_desc_s *cdmi_desc = NULL;
	int found = 0;

	SRB_LOG_INFO(srb_log, "srb_device_attach: attaching "
		      "filename %s as device %s",
		      filename, devname);

	/* check if volume is already attached, otherwise use the first empty slot */
	spin_lock(&devtab_lock);
	for (i = 0; i < DEV_MAX; ++i) {
		if (!device_free_slot(&devtab[i])) {
			const char *fname = kbasename(devtab[i].thread_cdmi_desc[0]->filename);
			if (strlen(fname) == strlen(filename) && strncmp(fname, filename, strlen(filename)) == 0) {
				found = 1;
				dev = &devtab[i];
				break;
			}
		}
	}
	if (0 == found) {
		for (i = 0; i < DEV_MAX; ++i) {
			if (device_free_slot(&devtab[i])) {
				dev = &devtab[i];
				dev->id = i;
				dev->state = DEV_IN_USE;
				break;
			}
		}
	}
	spin_unlock(&devtab_lock);

	if (1 == found) {
		SRB_LOG_ERR(srb_log, "Volume %s already attached as device %s", filename, dev->name);
		dev = NULL;
		return -EEXIST;
	} else {
		SRB_LOG_INFO(srb_log, "Volume %s not attached as device, using device slot %d", filename, dev->id);
	}

	cdmi_desc = kmalloc(sizeof(struct srb_cdmi_desc_s), GFP_KERNEL);
	if (cdmi_desc == NULL) {
		SRB_LOG_ERR(srb_log, "Unable to allocate memory for cdmi struct");
		rc = -ENOMEM;
		goto cleanup;
	}

	/* Allocate dev structure */
	rc = srb_device_new(devname, dev);
	if (rc != 0) {
		SRB_LOG_ERR(srb_log, "Unable to create new device: %i", rc);
		goto cleanup;
	} else {
		SRB_LOG_INFO(srb_log, "New device created for %s", devname);
	}

	init_waitqueue_head(&dev->waiting_wq);
	INIT_LIST_HEAD(&dev->waiting_queue);
	spin_lock_init(&dev->waiting_lock);

	/* Pick a convenient server to get srb_cdmi_desc
	 * TODO: #13 We need to manage failover by using every server
	 * NB: _srb_server_pick fills the cdmi_desc sruct
	 */
	rc = _srb_server_pick(filename, cdmi_desc);
	if (rc != 0) {
		SRB_LOG_ERR(srb_log, "Unable to get server: %i", rc);
		goto cleanup;
	}
	SRB_LOG_INFO(srb_log, "Adding Device: Picked server "
		      "[ip=%s port=%d fullpath=%s]",
		      cdmi_desc->ip_addr, cdmi_desc->port,
		      cdmi_desc->filename);

	/* set timeout value */
	cdmi_desc->timeout.tv_sec = req_timeout;
	cdmi_desc->timeout.tv_usec = 0;

	for (i = 0; i < thread_pool_size; i++) {	
		memcpy(dev->thread_cdmi_desc[i], cdmi_desc,
		       sizeof(struct srb_cdmi_desc_s));
	}
	//rc = register_blkdev(0, dev->name);
	rc = register_blkdev(0, DEV_NAME);
	if (rc < 0) {
		SRB_LOG_ERR(srb_log, "Could not register_blkdev()");
		goto cleanup;
	}
	dev->major = rc;

	rc = srb_init_disk(dev);
	if (rc < 0) {
		do_unregister = 1;
		goto cleanup;
	}

	srb_sysfs_device_init(dev);

	SRB_LOG_INFO(srb_log, "Added device %s (id: %d, major:%d) for server "
		      "[ip=%s port=%d fullpath=%s]",
		      dev->name, dev->id, dev->major, cdmi_desc->ip_addr,
		      cdmi_desc->port, cdmi_desc->filename);

	/* mark device as unsued == available */
	spin_lock(&devtab_lock);
	dev->state = DEV_UNUSED;
	spin_unlock(&devtab_lock);

	// Prevent releasing device <=> Validate operation
	dev = NULL;

	rc = 0;

cleanup:
	if (do_unregister)
		//unregister_blkdev(dev->major, dev->name);
		unregister_blkdev(dev->major, DEV_NAME);
	if (NULL != dev) {
		srb_device_free(dev);
		/* mark device as unsued == available */
		spin_lock(&devtab_lock);
		dev->state = DEV_UNUSED;
		spin_unlock(&devtab_lock);
	}
	if (NULL != cdmi_desc)
		kfree(cdmi_desc);

	if (rc < 0)
		SRB_LOG_ERR(srb_log, "Error adding device %s", filename);

	return rc;
}

int srb_device_create(const char *filename, unsigned long long size)
{
	srb_debug_t debug;
	struct srb_cdmi_desc_s *cdmi_desc;
	int rc;

	SRB_LOG_INFO(srb_log, "srb_device_create: creating volume %s of size %llu", filename, size);

	debug.name = NULL;
	debug.level = srb_log;

	cdmi_desc = kmalloc(sizeof(struct srb_cdmi_desc_s), GFP_KERNEL);
	if (cdmi_desc == NULL) {
		rc = -ENOMEM;
		goto err_out_mod;
	}

	/* Now, setup a cdmi connection then Truncate(create) the file. */
	rc = _srb_server_pick(filename, cdmi_desc);
	if (rc != 0)
		goto err_out_alloc;

	rc = srb_cdmi_connect(&debug, cdmi_desc);
	if (rc != 0)
		goto err_out_alloc;

	rc = srb_cdmi_create(&debug, cdmi_desc, size);
	if (rc != 0)
		goto err_out_cdmi;

	srb_cdmi_disconnect(&debug, cdmi_desc);

	SRB_LOG_INFO(srb_log, "Created volume with filename %s", filename);

	if (cdmi_desc)
		kfree(cdmi_desc);

	return rc;

err_out_cdmi:
	srb_cdmi_disconnect(&debug, cdmi_desc);
err_out_alloc:
	if (cdmi_desc)
		kfree(cdmi_desc);
err_out_mod:
	SRB_LOG_ERR(srb_log, "Error creating volume %s", filename);

	return rc;
}

int srb_device_extend(const char *filename, unsigned long long size)
{
	srb_debug_t debug;
	struct srb_cdmi_desc_s *cdmi_desc = NULL;
	int i;
	int rc = 0;
	struct srb_device_s *dev = NULL;

	SRB_LOG_INFO(srb_log, "srb_device_extend: extending volume %s to %llu size", filename, size);

	debug.name = NULL;
	debug.level = srb_log;

	/* check if volume is already attached, otherwise use the first empty slot */
	spin_lock(&devtab_lock);
	for (i = 0; i < DEV_MAX; ++i) {
		if (!device_free_slot(&devtab[i])) {
			const char *fname = kbasename(devtab[i].thread_cdmi_desc[0]->filename);
			if (strlen(fname) == strlen(filename) && strncmp(fname, filename, strlen(filename)) == 0) {
				dev = &devtab[i];
				if (dev->state == DEV_IN_USE)
					rc = -EBUSY;
				else
					dev->state = DEV_IN_USE;
				break;
			}
		}
	}
	spin_unlock(&devtab_lock);
	if (0 != rc) {
		SRB_LOG_ERR(srb_log, "Volume %s attached on device %s and in use", filename, dev->name);
		dev = NULL;
		return rc;
	}

	cdmi_desc = kmalloc(sizeof(struct srb_cdmi_desc_s), GFP_KERNEL);
	if (cdmi_desc == NULL) {
		rc = -ENOMEM;
		goto err_out_mod;
	}
	memset(cdmi_desc, 0, sizeof(struct srb_cdmi_desc_s));

	/* Now, setup a cdmi connection then Truncate(create) the file. */
	rc = _srb_server_pick(filename, cdmi_desc);
	if (rc != 0)
		goto err_out_alloc;

	rc = srb_cdmi_connect(&debug, cdmi_desc);
	if (rc != 0)
		goto err_out_alloc;

	rc = srb_cdmi_extend(&debug, cdmi_desc, size);
	if (rc != 0)
		goto err_out_cdmi;

	rc = srb_cdmi_disconnect(&debug, cdmi_desc);
	if (rc != 0)
		goto err_out_cdmi;

	if (cdmi_desc)
		kfree(cdmi_desc);

	// Find device (normally only 1) associated to filename and update their size
	spin_lock(&devtab_lock);
	if (dev) {
		devtab[i].disk_size = size;
		set_capacity(devtab[i].disk, devtab[i].disk_size / 512ULL);
		revalidate_disk(devtab[i].disk);
		dev->state = DEV_UNUSED;
	}
	spin_unlock(&devtab_lock);

	SRB_LOG_INFO(srb_log, "Extended filename %s", filename);

	return rc;

err_out_cdmi:
	srb_cdmi_disconnect(&debug, cdmi_desc);
err_out_alloc:
	if (cdmi_desc)
		kfree(cdmi_desc);
err_out_mod:
	SRB_LOG_ERR(srb_log, "Error extending device %s", filename);

	return rc;
}

int srb_device_destroy(const char *filename)
{
	srb_debug_t debug;
	struct srb_cdmi_desc_s *cdmi_desc = NULL;
	int rc = -EIO;
	int i;
	int found = 0;

	SRB_LOG_INFO(srb_log, "srb_device_destroy: destroying volume: %s", filename);

	debug.name = NULL;
	debug.level = srb_log;

	// Check that there is no device associated to filename
	spin_lock(&devtab_lock);
	for (i = 0; i < DEV_MAX; ++i) {
		if (!device_free_slot(&devtab[i])) {
			const char *fname = kbasename(
				devtab[i].thread_cdmi_desc[0]->filename);
			if (strlen(fname) == strlen(filename) && strncmp(fname, filename, strlen(fname)) == 0) {
				found = 1;
				break;
			}
		}
	}
	spin_unlock(&devtab_lock);

	if (found) {
		SRB_LOG_ERR(srb_log, "Found a device associated to volume %s", filename);
		rc = -EBUSY;
		goto err_out_mod;
	}

	cdmi_desc = kmalloc(sizeof(struct srb_cdmi_desc_s), GFP_KERNEL);
	if (cdmi_desc == NULL) {
		SRB_LOG_ERR(srb_log, "Unable to allocate memory for temporary CDMI");
		rc = -ENOMEM;
		goto err_out_mod;
	}

	/* First, setup a cdmi connection then Delete the file. */
	rc = _srb_server_pick(filename, cdmi_desc);
	if (rc != 0)
		goto err_out_alloc;

	rc = srb_cdmi_connect(&debug, cdmi_desc);
	if (rc != 0)
		goto err_out_alloc;

	rc = srb_cdmi_delete(&debug, cdmi_desc);
	if (rc != 0)
		goto err_out_cdmi;

	srb_cdmi_disconnect(&debug, cdmi_desc);

	if (cdmi_desc)
		kfree(cdmi_desc);

	SRB_LOG_INFO(srb_log, "Destroyed volume %s", filename);

	return rc;

err_out_cdmi:
	srb_cdmi_disconnect(&debug, cdmi_desc);
err_out_alloc:
	if (cdmi_desc)
		kfree(cdmi_desc);
err_out_mod:
	SRB_LOG_ERR(srb_log, "Error destroying volume %s", filename);

	return rc;
}

static int __init srb_init(void)
{
	int rc;

	SRB_LOG_NOTICE(srb_log, "Initializing %s block device driver version %s", DEV_NAME, DEV_REL_VERSION);

	/* Zeroing device tab */
	memset(devtab, 0, sizeof(devtab));

	rc = srb_sysfs_init();
	if (rc) {
		SRB_LOG_ERR(srb_log, "Failed to initialize with code: %d", rc);
		return rc;
	}

	return 0;
}

static void __exit srb_cleanup(void)
{
	SRB_LOG_NOTICE(srb_log, "Cleaning up %s block device driver", DEV_NAME);

	_srb_detach_devices();

	srb_sysfs_cleanup();
}

module_init(srb_init);
module_exit(srb_cleanup);
