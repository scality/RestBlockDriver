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

#include <linux/genhd.h>
#include <linux/scatterlist.h>

#include <srb/srb.h>
#include <srb/srb-cdmi.h>
#include <srb/srb-log.h>

/* Unix device constants */
#define DEV_NAME		"srb"
#define DEV_REL_VERSION		"0.6.1"		// Set version of srb LKM
#define DEV_SECTORSIZE		SRB_DEV_SECTORSIZE
#define DEV_NB_PHYS_SEGS	SRB_DEV_NB_PHYS_SEGS

/*
 * Linux Kernel Module (LKM) parameters
 */
extern unsigned short srb_log;
extern unsigned short nb_req_retries;

/* srb.c */
int srb_device_attach(const char *filename, const char *devname);
int srb_device_detach(const char *devname);

int srb_server_add(const char *url);
int srb_server_remove(const char *url);
ssize_t srb_servers_dump(char *buf, ssize_t max_size);

struct srb_device;
typedef struct srb_device srb_device_t;
const srb_debug_t * srb_device_get_debug(const struct srb_device *dev);
int srb_device_get_debug_level(const struct srb_device *dev);
int srb_device_get_major(const struct srb_device *dev);
const char * srb_device_get_name(const struct srb_device *dev);
void srb_device_set_debug_level(struct srb_device *dev, int level);
struct srb_cdmi_desc * srb_device_get_thread_cdmi_desc(const struct srb_device *dev, int idx);
struct gendisk * srb_device_get_disk(const struct srb_device *dev);

/* srb_sysfs.c*/
int srb_sysfs_init(void);
void srb_sysfs_device_init(srb_device_t *dev);
void srb_sysfs_cleanup(void);
#endif
