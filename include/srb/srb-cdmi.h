/*
 * Copyright (C) 2015 Scality SA - http://www.scality.com
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

#ifndef __SRB_CDMI_H__
#define __SRB_CDMI_H__

#include <linux/in.h>

#include <srb/srb.h>
#include <srb/srb-log.h>

#define SRB_CDMI_URL_SIZE (256)
#define SRB_CDMI_HTTP_HEADER_SIZE (1024)

#define SRB_CDMI_XMIT_BUFFER_SIZE (SRB_CDMI_HTTP_HEADER_SIZE + SRB_DEV_SECTORSIZE)

struct srb_cdmi_desc {
	/* For /sys/block/srb?/srb_url */
	char			url[SRB_CDMI_URL_SIZE + 1];
	uint8_t			state;
	char			ip_addr[16];
	uint16_t		port;
	char			filename[SRB_CDMI_URL_SIZE + 1];
	char			xmit_buff[SRB_CDMI_XMIT_BUFFER_SIZE];
	uint64_t		nb_requests; /* Number of HTTP
					      * requests already sent
					      * through this socket */
	struct scatterlist	sgl[SRB_DEV_NB_PHYS_SEGS];
	int			sgl_size;
	struct socket		*socket;
	struct sockaddr_in	sockaddr;
	struct timeval		timeout;
};

int srb_cdmi_init(srb_debug_t *dbg, struct srb_cdmi_desc *desc,
		const char *url);
int srb_cdmi_connect(srb_debug_t *dbg, struct srb_cdmi_desc *desc);
int srb_cdmi_disconnect(srb_debug_t *dbg, struct srb_cdmi_desc *desc);

int srb_cdmi_getsize(srb_debug_t *dbg, struct srb_cdmi_desc *desc,
		uint64_t *size);

int srb_cdmi_getrange(srb_debug_t *dbg, struct srb_cdmi_desc *desc,
		uint64_t offset, int size);

int srb_cdmi_putrange(srb_debug_t *dbg, struct srb_cdmi_desc *desc,
		uint64_t offset, int size);
#endif
