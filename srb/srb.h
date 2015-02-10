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
 * along with Foobar.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef __SRBLOCK_H__
# define  __SRBLOCK_H__

#include <linux/inet.h>
#include <linux/socket.h>
#include <linux/net.h>	     // for in_aton()
#include <linux/in.h>
#include <linux/net.h>
#include <linux/scatterlist.h>
#include <net/sock.h>
#include <linux/genhd.h>

#include <srb/srb.h>
#include <srb/srb-cdmi.h>
#include <srb/srb-log.h>

/* Constants */
#define kB			1024
#define MB			(1024 * kB)
#define GB			(1024 * MB)

/* Unix device constants */
#define DEV_NAME		"srb"
#define DEV_REL_VERSION		"0.6.1"		// Set version of srb LKM
#define DEV_MINORS		256
#define DEV_DEFAULT_DISKSIZE	(50 * MB)
#define DEV_MAX			64
#define DEV_SECTORSIZE		SRB_DEV_SECTORSIZE
#define DEV_NB_PHYS_SEGS	SRB_DEV_NB_PHYS_SEGS

/* Device state (reduce spinlock section and avoid multiple operation on same device) */
#define DEV_IN_USE		1
#define DEV_UNUSED		0

/* Dewpoint server related constants */
#define SRB_REUSE_LIMIT	100 /* Max number of requests sent to
				     * a single HTTP connection before
				     * restarting a new one.
				     */

/*
 * Linux Kernel Module (LKM) parameters
 */
extern unsigned short srb_log;
extern unsigned short req_timeout;
extern unsigned short nb_req_retries;
extern unsigned short server_conn_timeout;
extern unsigned int thread_pool_size;

/*
 * Default values for ScalityRestBlock LKM parameters
 */
#define SRB_REQ_TIMEOUT_DFLT		30
#define SRB_NB_REQ_RETRIES_DFLT	3
#define SRB_CONN_TIMEOUT_DFLT		30
#define SRB_LOG_LEVEL_DFLT		SRB_INFO
#define SRB_THREAD_POOL_SIZE_DFLT	8

#define SRB_DEBUG_LEVEL	0   /* We do not want to be polluted
			     * by default */

#define SRB_MIN(x, y) ((x) < (y) ? (x) : (y))
#define SRB_N_JSON_TOKENS	128

typedef struct srb_device_s {
	/* Device subsystem related data */
	int			id;		/* device ID */
	int			major;		/* blkdev assigned major */

	/* NOTE: use const from ./linux/genhd.h */
	char			name[DISK_NAME_LEN];	/* blkdev name, e.g. srba */
	struct gendisk		*disk;
	uint64_t		disk_size;	/* Size in bytes */
	int			users;		/* Number of users who
						 * opened dev */
	int			state; 		/* for create extend attach detach destroy purpose */

	struct request_queue	*q;
	spinlock_t		rq_lock;	/* request queue lock */

	struct task_struct	**thread;	/* allow dynamic allocation during device creation */
	int			nb_threads;

	/* Dewpoint specific data */
	struct srb_cdmi_desc_s	 **thread_cdmi_desc;	/* allow dynamic allocation during device creation*/

	/*
	** List of requests received by the drivers, but still to be
	** processed. This due to network latency.
	*/
	spinlock_t		waiting_lock;	/* wait_queue lock */
	wait_queue_head_t	waiting_wq;
	struct list_head	waiting_queue;  /* Requests to be sent */

	/* Debug traces */
	srb_debug_t		debug;
} srb_device_t;

typedef struct srb_server_s {
	struct srb_server_s   	*next;
	struct srb_cdmi_desc_s	cdmi_desc;
} srb_server_t;

/* srb.c */
int srb_device_attach(const char *filename, const char *devname);
int srb_device_detach(const char *devname);

int srb_server_add(const char *url);
int srb_server_remove(const char *url);
ssize_t srb_servers_dump(char *buf, ssize_t max_size);

/* srb_sysfs.c*/
int srb_sysfs_init(void);
void srb_sysfs_device_init(srb_device_t *dev);
void srb_sysfs_cleanup(void);
#endif
