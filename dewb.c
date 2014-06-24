
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
static dewb_mirror_t	*mirrors = NULL;
static DEFINE_SPINLOCK(devtab_lock);

/*
 * Handle an I/O request.
 */
#if 0
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
#endif 

void dewb_xfer_scl(struct dewb_device_s *dev, 
		struct dewb_cdmi_desc_s *desc, 
		struct request *req)
{

	int ret;
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
	if (ret)
		DEWB_ERROR("IO failed: %d", ret);
	
}

static int dewb_thread(void *data)
{
	struct dewb_device_s *dev = data;
	struct request *req;
	unsigned long flags;
	int th_id;
	
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

		DEWB_DEV_DEBUG("NEW REQUEST [tid:%d]", th_id);
		/* Create scatterlist */
		sg_init_table(dev->thread_cdmi_desc[th_id].sgl, DEV_NB_PHYS_SEGS);
		dev->thread_cdmi_desc[th_id].sgl_size = 
			blk_rq_map_sg(dev->q, req, 
				dev->thread_cdmi_desc[th_id].sgl);

		DEWB_DEV_DEBUG("Scatterlist size = %d", 
			dev->thread_cdmi_desc[th_id].sgl_size);

		DEWB_DEV_DEBUG("[sector = %lu, nr_sectors=%u w=%d]",
			blk_rq_pos(req), blk_rq_sectors(req),
			rq_data_dir(req) == WRITE);

		/* Call scatter function */
		dewb_xfer_scl(dev, &dev->thread_cdmi_desc[th_id], req);
		DEWB_DEV_DEBUG("END REQUEST [tid:%d]", th_id);
		
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
			DEWB_DEV_DEBUG("Skip non-CMD request");
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

/*
 * After linux kernel v3.10, this function stops returning anything
 * (becomes void). To avoid supporting too many things, just keep it int
 * and ignore th associated warning.
 */
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
	.open	 =	dewb_open,
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

	blk_queue_max_hw_sectors(q, DEV_NB_PHYS_SEGS);
	q->queuedata	= dev;
	
	dev->disk	= disk;
	dev->q		= disk->queue = q;
	dev->nb_threads = 0;
	//blk_queue_flush(q, REQ_FLUSH | REQ_FUA);
	///blk_queue_max_phys_segments(q, DEV_NB_PHYS_SEGS);

	for (i = 0; i < DEWB_THREAD_POOL_SIZE; i++) {

		if ((ret = dewb_cdmi_connect(&dev->debug,
					&dev->thread_cdmi_desc[i]))) {
			DEWB_ERROR("Unable to connect to CDMI endpoint : %d",
				ret);
			put_disk(disk);
			return -EIO;
		}

	}
	/* Caution: be sure to call this before spawning threads */
	ret = dewb_cdmi_getsize(&dev->debug,
				&dev->thread_cdmi_desc[0],
				&dev->disk_size);
	if (ret != 0)
	{
		DEWB_ERROR("Could not retrieve volume size.");
		put_disk(disk);
		return ret;
	}

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
	dev->debug.name = &dev->name[0];
	dev->debug.level = DEWB_DEBUG_LEVEL;
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

static int _dewb_reconstruct_url(char *url, char *name,
				 const char *baseurl, const char *basepath,
				 const char *filename)
{
	int urllen = 0;
	int namelen = 0;
	int seplen = 0;

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

static int _dewb_detach_devices(void)
{
	int ret;
	int i = 0;
	int errcount = 0;

	for (i=0; i<DEV_MAX; ++i)
	{
		if (!device_free_slot(&devtab[i]))
		{
			ret = __dewb_device_detach(&devtab[i]);
			if (ret != 0)
			{
				DEWB_ERROR("Could not remove device for volume at unload %s",
					   devtab[i].thread_cdmi_desc[0].filename);
			}
		}
	}

	return errcount;
}

static void _dewb_mirror_free(dewb_mirror_t *mirror)
{
	if (mirror)
		kfree(mirror);
}

static int _dewb_mirror_new(dewb_debug_t *dbg, const char *url, dewb_mirror_t **mirror)
{
	dewb_mirror_t	*new = NULL;
	int		ret = 0;

	new = kcalloc(1, sizeof(*new), GFP_KERNEL);
	if (new == NULL)
	{
		DEWB_ERROR("Cannot allocate memory to add a new mirror.");
		ret = -ENOMEM;
		goto end;
	}

	ret = dewb_cdmi_init(dbg, &new->cdmi_desc, url);
	if (ret != 0)
	{
		DEWB_ERROR("Could not initialize mirror descriptor (parse URL).");
		goto end;
	}

	if (mirror)
	{
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
{
	char url[DEWB_URL_SIZE];
	char name[DEWB_URL_SIZE];
	int ret;
	int found = 0;
	dewb_mirror_t *mirror = NULL;

	spin_lock(&devtab_lock);
	mirror = mirrors;
	while (mirror != NULL)
	{
		DEWB_INFO("Browsing mirror: %s", mirror->cdmi_desc.url);
		ret = _dewb_reconstruct_url(url, name,
					    mirror->cdmi_desc.url,
					    mirror->cdmi_desc.filename,
					    filename);
		DEWB_INFO("Dewb reconstruct url yielded %s, %i", url, ret);
		if (ret == 0)
		{
			memcpy(pick, &mirror->cdmi_desc,
			       sizeof(mirror->cdmi_desc));
			strncpy(pick->url, url, DEWB_URL_SIZE);
			strncpy(pick->filename, name, DEWB_URL_SIZE);
			DEWB_INFO("Copied into pick: url=%s, name=%s", pick->url, pick->filename);
			found = 1;
			break ;
		}
		mirror = mirror->next;
	}
	spin_unlock(&devtab_lock);

	DEWB_INFO("Browsed all mirrors");

	if (!found)
	{
		DEWB_ERROR("Could not match any mirror for filename %s", filename);
		// No such device or adress seems to match 'missing mirror'
		ret = -ENXIO;
		goto end;
	}

	ret = 0;
end:
	return ret;
}

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

	debug.name = "<Mirror-Adder>";
	debug.level = DEWB_DEBUG_LEVEL;

	if (strlen(url) >= DEWB_URL_SIZE)
	{
		DEWB_ERROR("Url too big: '%s'", url);
		ret = -EINVAL;
		goto err_out_dev;
	}

	ret = _dewb_mirror_new(&debug, url, &new);
	if (ret != 0)
		goto err_out_dev;

	cdmi_desc = kcalloc(1, sizeof(*cdmi_desc), GFP_KERNEL);
	if (cdmi_desc == NULL)
	{
		ret = -ENOMEM;
		goto err_out_mirror_alloc;
	}
	memcpy(cdmi_desc, &new->cdmi_desc, sizeof(new->cdmi_desc));

	spin_lock(&devtab_lock);
	cur = mirrors;
	while (cur != NULL)
	{
		if (strcmp(url, cur->cdmi_desc.url) == 0)
		{
			found = 1;
			break ;
		}
		last = cur;
		cur = cur->next;
	}
	if (found == 0)
	{
		if (mirrors == NULL)
			was_first = 1;
		if (last != NULL)
			last->next = new;
		else
			mirrors = new;
		new = NULL;
	}
	spin_unlock(&devtab_lock);

	if (was_first)
	{
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

	if (strlen(url) >= DEWB_URL_SIZE)
	{
		DEWB_ERROR("Url too big: '%s'", url);
		ret = -EINVAL;
		goto end;
	}

	// Check if it's the last mirror. If yes, remove all devices.
	spin_lock(&devtab_lock);
	ret = 0;
	if (mirrors != NULL && mirrors->next == NULL)
	{
		ret = _dewb_detach_devices();
	}
	spin_unlock(&devtab_lock);

	if (ret != 0)
	{
		DEWB_ERROR("Could not remove all devices; not removing mirror.");
		ret = -EBUSY;
		goto end;
	}

	spin_lock(&devtab_lock);
	cur = mirrors;
	while (cur != NULL)
	{
		if (strcmp(url, cur->cdmi_desc.url) == 0)
		{
			found = 1;
			break ;
		}
		prev = cur;
		cur = cur->next;
	}
	if (found == 0)
	{
		DEWB_ERROR("Cannot remove mirror: Url is not part of mirrors");
		ret = -ENOENT;
	}
	else
	{
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

	spin_lock(&devtab_lock);
	cur = mirrors;
	while (cur)
	{
		if (printed != 0)
		{
			len = snprintf(buf + printed, max_size - printed, ",");
			if (len == -1 || len != 1)
			{
				DEWB_ERROR("Not enough space to print mirrors list in buffer.");
				ret = -ENOMEM;
				break ;
			}
			printed += len;
		}

		len = snprintf(buf + printed, max_size - printed, "%s", cur->cdmi_desc.url);
		if (len == -1 || len > (max_size - printed))
		{
			DEWB_ERROR("Not enough space to print mirrors list in buffer.");
			ret = -ENOMEM;
			break ;
		}
		printed += len;

		cur = cur->next;
	}
	spin_unlock(&devtab_lock);

	len = snprintf(buf + printed, max_size - printed, "\n");
	if (len == -1 || len != 1)
	{
		DEWB_ERROR("Not enough space to print mirrors list in buffer.");
		ret = -ENOMEM;
	}
	printed += len;

	return ret < 0 ? ret : printed;
}

int dewb_device_detach_by_name(const char *filename)
{
	int ret;
	int i;

	spin_lock(&devtab_lock);	
	for (i = 0; i < DEV_MAX; ++i)
	{
		if (!device_free_slot(&devtab[i]))
		{
			const char *fname
			    = kbasename(devtab[i].thread_cdmi_desc[0].filename);
			if (strcmp(filename, fname) == 0)
			{
				ret = __dewb_device_detach(&devtab[i]);
				if (ret != 0)
				{
					DEWB_ERROR("Cannot detach"
						   " volume automatically.");
					break ;
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

	spin_lock(&devtab_lock);

	dev = &devtab[dev_id];
	ret = __dewb_device_detach(dev);

	spin_unlock(&devtab_lock);

	return ret;
}

int dewb_device_attach(const char *filename)
{
	dewb_device_t *dev;
	ssize_t rc;
	int irc;
	int i;
	struct dewb_cdmi_desc_s *cdmi_desc = NULL;

	cdmi_desc = kmalloc(sizeof(*cdmi_desc), GFP_KERNEL);
	if (cdmi_desc == NULL)
	{
		rc = -ENOMEM;
		goto err_out_mod;
	}

	/* Allocate dev structure */
	dev = dewb_device_new();
	if (!dev) {
		rc=  -ENOMEM;
		goto err_out_cdmi_desc;
	}

	init_waitqueue_head(&dev->waiting_wq);
	INIT_LIST_HEAD(&dev->waiting_queue);
	spin_lock_init(&dev->waiting_lock);

	/* Pick a convenient mirror to get dewb_cdmi_desc
	 * TODO: #13 We need to manage failover by using every mirror
	 */
	rc = _dewb_mirror_pick(filename, cdmi_desc);
	if (rc != 0)
	{
		goto err_out_dev;
	}
	DEWB_INFO("Adding Device: Picked mirror [ip=%s port=%d fullpath=%s]",
		  cdmi_desc->ip_addr, cdmi_desc->port, cdmi_desc->filename);

	/* Parse add command */
	for (i = 0; i < DEWB_THREAD_POOL_SIZE; i++) {
		memcpy(&dev->thread_cdmi_desc[i], cdmi_desc, sizeof(*cdmi_desc));
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
	
	DEWB_DEV_DEBUG("Added device (major:%d)", dev->major);

	return rc;
err_out_unregister:
	unregister_blkdev(dev->major, dev->name);
err_out_dev:
	dewb_device_free(dev);
err_out_cdmi_desc:
	kfree(cdmi_desc);
err_out_mod:
	DEWB_ERROR("Error adding device %s", filename);
	return rc;
}

int dewb_device_create(const char *filename, unsigned long long size)
{
	dewb_debug_t debug;
	struct dewb_cdmi_desc_s *cdmi_desc = NULL;
	int rc;

	debug.name = NULL;
	debug.level = 0;

	cdmi_desc = kmalloc(sizeof(*cdmi_desc), GFP_KERNEL);
	if (cdmi_desc == NULL)
	{
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

	rc = dewb_device_attach(filename);
	if (rc != 0)
	{
		DEWB_ERROR("Cannot add created volume automatically.");
		goto err_out_cdmi;
	}

	kfree(cdmi_desc);

	return rc;
err_out_cdmi:
	dewb_cdmi_disconnect(&debug, cdmi_desc);
err_out_alloc:
	kfree(cdmi_desc);
err_out_mod:
	DEWB_ERROR("Error creating device %s", filename);
	return rc;
}

int dewb_device_extend(const char *filename, unsigned long long size)
{
	dewb_debug_t debug;
	struct dewb_cdmi_desc_s *cdmi_desc = NULL;
	int i;
	int rc;

	debug.name = NULL;
	debug.level = 0;

	cdmi_desc = kmalloc(sizeof(*cdmi_desc), GFP_KERNEL);
	if (cdmi_desc == NULL)
	{
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
			const char *fname
			    = kbasename(devtab[i].thread_cdmi_desc[0].filename);
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
	DEWB_ERROR("Error creating device %s", filename);
	return rc;
}

int dewb_device_destroy(const char *filename)
{
	dewb_debug_t debug;
	struct dewb_cdmi_desc_s *cdmi_desc = NULL;
	int rc;
	int remove_err = 0;
	int i;

	debug.name = NULL;
	debug.level = 0;

	cdmi_desc = kmalloc(sizeof(*cdmi_desc), GFP_KERNEL);
	if (cdmi_desc == NULL)
	{
		rc = -ENOMEM;
		goto err_out_mod;
	}

	// Remove every device (normally only 1) associated to filename
	spin_lock(&devtab_lock);
	for (i = 0; i < DEV_MAX; ++i)
	{
		if (!device_free_slot(&devtab[i]))
		{
			const char *fname
			    = kbasename(devtab[i].thread_cdmi_desc[0].filename);
			if (strcmp(filename, fname) == 0)
			{
				rc = __dewb_device_detach(&devtab[i]);
				if (rc != 0)
				{
					DEWB_ERROR("Cannot add created"
						   " volume automatically.");
					remove_err = 1;
					break ;
				}
			}
		}
	}
	spin_unlock(&devtab_lock);

	if (remove_err)
	{
		DEWB_ERROR("Could not remove every device associated to "
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
	DEWB_ERROR("Error destroying volume %s", filename);
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

	spin_lock(&devtab_lock);
	(void)_dewb_detach_devices();
	spin_unlock(&devtab_lock);

	dewb_sysfs_cleanup();

	return ;
}

module_init(dewblock_init);
module_exit(dewblock_cleanup);
