/*
 * Copyright (C) 2014 SCALITY SA - http://www.scality.com
 * Copyright 1997-2000, 2008 Pavel Machek <pavel@ucw.cz>
 * Parts copyright 2001 Steven Whitehouse <steve@chygwyn.com>
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

#include <linux/module.h>    // included for all kernel modules
#include <linux/kernel.h>    // included for KERN_INFO
#include <linux/types.h>     // for uintx_t
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/socket.h>
#include <linux/tcp.h>
#include "srb.h"

#include "jsmn/jsmn.h"

/* TODO: add username and password support */
#define CDMI_DISCONNECTED	0
#define CDMI_CONNECTED		1

#define PROTO_HTTP		"http://"

static int is_digit(char c)
{
	if ((c >= '0') && (c <= '9'))
		return 1;
	return 0;
}

static int ip_valid_char(char c)
{

	if (is_digit(c))
		return 1;
	if (c == '.')
		return 1;

	return 0;
}
/* This helper copies the ip part of the remaining url in buffer
 * pointed by ip.

 * In case of success get_ip() returns the number of bytes read from
 * url, it returns 0 otherwise.
 */
static int get_ip(const char *url, char *ip)
{
	int i;

	for (i = 0; i < 15; i++) {
		
		/* End of hostname part */
		if ((url[i] == ':') || (url[i] == '/'))
			break;

		/* allowed chars 0-9 and . */
		if (!ip_valid_char(url[i]))
			return 0;

		*ip = url[i];
		ip++;
	}

	*ip = 0;
	if ((url[i] != ':') && (url[i] != '/'))
		return 0;

	return i;
}

static int get_port(const char *url, int *port)
{
	int i;
	*port = 0;
	for (i = 0; is_digit(url[i]); i++)
	{
		*port *= 10;
		*port += url[i] - '0';
	}
	return i;
}

/* srb_cdmi_init (URL)
 *
 * Parse url and initialise cdmi structure in all threads descriptors
 *
 */
int srb_cdmi_init(srb_debug_t *dbg,
		struct srb_cdmi_desc_s *desc,
		const char *url)
{
	/*          1    1 */
	/* 123456789012345 */
	/* 255.255.255.255 */
	char ip[16];
	int ret;
	int port = 80;

	desc->filename[0] = 0;
	*ip = 0;

	strncpy(desc->url, url, SRB_URL_SIZE);

	desc->url[SRB_URL_SIZE] = 0;
	/* Only 'http://' supported for the moment */
	if (strncmp(url, PROTO_HTTP, strlen(PROTO_HTTP)))
		return -EINVAL;

	url += strlen(PROTO_HTTP);

	ret = get_ip(url, ip);
	if (ret == 0)
		return -EINVAL;
	url += ret;

	/* Decode optional port number*/
	if (url[0] == ':') {
		url++;
		ret = get_port(url, &port);
		if (ret == 0) {
			return -EINVAL;
		}
		url += ret;
	}

	/* Now we should get the page '/mypage' */
	if (url[0] != '/') {
		return -EINVAL;
	}

	strcpy(desc->filename, url);
	strcpy(desc->ip_addr, ip);
	desc->port	  = port;
	desc->state	  = CDMI_DISCONNECTED;

	SRB_LOG_DEBUG(dbg->level, "Decoded URL [ip=%s port=%d file=%s]",
	              desc->ip_addr, desc->port, desc->filename);

	return 0;	
}

/* srb_cdmi_connect
 *
 * Connect thread pools descriptors
 *
 * Returns 0 if successfull or a negative value depending the error.
 */
int srb_cdmi_connect(srb_debug_t *dbg,
		struct srb_cdmi_desc_s *desc)
{
	int ret;
	int arg = 1;

	if (!desc)
		return -EINVAL;

	if (desc->state == CDMI_CONNECTED)
		return 0;

	/* Init socket */
	ret = sock_create_kern(PF_INET, SOCK_STREAM, IPPROTO_TCP, &desc->socket);
	if (ret < 0) {
		SRB_LOG_ERR(dbg->level, "Unable to create socket: %d", ret);
		goto out_error;
	}
	memset(&desc->sockaddr, 0, sizeof(desc->sockaddr));

	/* Connecting socket */
	desc->sockaddr.sin_family	= AF_INET;
	desc->sockaddr.sin_addr.s_addr  = in_aton(desc->ip_addr);
	desc->sockaddr.sin_port		= htons(desc->port);
	ret = desc->socket->ops->connect(desc->socket,
				(struct sockaddr*)&desc->sockaddr,
				sizeof(struct sockaddr_in), !O_NONBLOCK);
	if (ret < 0) {
		SRB_LOG_ERR(dbg->level, "Unable to connect to cdmi server: %d", ret);
		goto out_error;
	}
	desc->state = CDMI_CONNECTED;

	ret = kernel_setsockopt(desc->socket,
		IPPROTO_TCP, TCP_NODELAY, (char *)&arg,
		sizeof(arg));
	if (ret < 0) {
		SRB_LOG_ERR(dbg->level, "setsockopt failed: %d", ret);
		goto out_error;
	}

	/* TODO: set request timeout value (Issue #22) */
	if (desc->timeout.tv_sec > 0) {
		SRB_LOG_DEBUG(dbg->level, "srb_cdmi_connect: set socket timeout %lu", desc->timeout.tv_sec);
		ret = kernel_setsockopt(desc->socket, SOL_SOCKET, SO_RCVTIMEO,
			(char *)&desc->timeout, sizeof(struct timeval));
		if (ret < 0) {
			SRB_LOG_ERR(dbg->level, "Failed to set socket receive timeout value: %d", ret);
		}
		ret = kernel_setsockopt(desc->socket, SOL_SOCKET, SO_SNDTIMEO, (char *)&desc->timeout, sizeof(struct timeval));
		if (ret < 0) {
			SRB_LOG_ERR(dbg->level, "Failed to set socket send timeout value: %d", ret);
		}
	}

	/* As we established a new connection, reset the number of
	   HTTP requests sent */
	desc->nb_requests = 0;

	return 0;

out_error:
	if (desc->socket) {
		kernel_sock_shutdown(desc->socket, SHUT_RDWR);
		sock_release(desc->socket);
	}
	desc->socket = NULL;
	desc->state = CDMI_DISCONNECTED;

	return ret;
}

/* srb_cdmi_disconnect
 *
 * Disctonnect current descriptor from CDMI server
 *
 */
int srb_cdmi_disconnect(srb_debug_t *dbg,
			struct srb_cdmi_desc_s *desc)
{
	if (!desc)
		return -EINVAL;
	if (!desc->socket || desc->state == CDMI_DISCONNECTED)
		return 0;

	kernel_sock_shutdown(desc->socket, SHUT_RDWR);
	sock_release(desc->socket);
	desc->socket = NULL;
	desc->state = CDMI_DISCONNECTED;

	return 0;
}


/*
 *  Send or receive packet.
 */
static int sock_xmit(srb_debug_t *dbg,
		struct srb_cdmi_desc_s *desc,
		int send,
		void *buf, int size,
		int strict_receive)
{
	int result;
	struct msghdr msg;
	struct kvec iov;
	sigset_t blocked, oldset;
	unsigned long pflags = current->flags;

	if (unlikely(!desc->socket)) {
		SRB_LOG_ERR(dbg->level, "Attempted %s on closed socket in sock_xmit\n",
			(send ? "send" : "recv"));
		return -EINVAL;
	}

	
	/* Allow interception of SIGKILL only
	 * Don't allow other signals to interrupt the transmission */
	siginitsetinv(&blocked, sigmask(SIGKILL));
	sigprocmask(SIG_SETMASK, &blocked, &oldset);

	current->flags |= PF_MEMALLOC;
	do {
		desc->socket->sk->sk_allocation = GFP_NOIO | __GFP_MEMALLOC;
		iov.iov_base = buf;
		iov.iov_len = size;
		msg.msg_name = NULL;
		msg.msg_namelen = 0;
		msg.msg_control = NULL;
		msg.msg_controllen = 0;
		msg.msg_flags = MSG_NOSIGNAL;

		if (send) {
			result = kernel_sendmsg(desc->socket, &msg, &iov, 1, size);
			SRB_LOG_DEBUG(dbg->level, "sock_xmit: Sent %d bytes->\n%.*s", result, result, (char*)buf);
		} else {
			result = kernel_recvmsg(desc->socket, &msg, &iov, 1, size,
						msg.msg_flags);
			SRB_LOG_DEBUG(dbg->level, "sock_xmit: Received %d bytes->\n%.*s", result, result, (char*)buf);
		}
		SRB_LOG_DEBUG(dbg->level, "Result for socket exchange: %d", result);
		if (signal_pending(current)) {
			siginfo_t info;
			SRB_LOG_INFO(dbg->level, "srb (pid %d: %s) got signal %d\n",
				task_pid_nr(current), current->comm,
				dequeue_signal_lock(current, &current->blocked, &info));
			result = -EINTR;
			break;
		}

		if (result && (!send) && (!strict_receive))
			break;

		if (result == 0)  {
			SRB_LOG_DEBUG(dbg->level, "Empty socket exhange (size: %d)", size);
			result = -EPIPE;
			break;
		}

		if (result < 0) {
			break;
		}
		size -= result;
		buf += result;
	} while (size > 0);

	sigprocmask(SIG_SETMASK, &oldset, NULL);
	tsk_restore_flags(current, pflags, PF_MEMALLOC);

	return result;
}

static int sock_send_receive(srb_debug_t *dbg,
			struct srb_cdmi_desc_s *desc,
			int send_size, int rcv_size)
{
	char *buff = desc->xmit_buff;
	int strict_rcv = 1;
	int ret = 0;
	int rcvd = 0;
	char *rcvbuf = NULL;
	int has_epiped = 0;

	if (rcv_size == 0) {
		strict_rcv = 0;
		rcv_size = SRB_XMIT_BUFFER_SIZE;
	}
	rcvbuf = kmalloc(rcv_size, GFP_KERNEL);
	if (rcvbuf == NULL) {
		ret = -ENOMEM;
		goto cleanup;
	}

	/*
	 * Check if the connection needs to be restarted:
	 * Reconnect the socket after a predefined number of HTTP
	 * requests sent.
	 */
	if (desc->nb_requests == SRB_REUSE_LIMIT) {
		SRB_LOG_DEBUG(dbg->level, "Limit of %u requests reached reconnecting socket", SRB_REUSE_LIMIT);
		srb_cdmi_disconnect(dbg, desc);
	}
	else
		desc->nb_requests++;

	/*
	 * Reconnect whether it's been resetted manually (SRB_REUSE_LIMIT) or
	 * it was left disconnected
	 */
	if (desc->state == CDMI_DISCONNECTED) {
		ret = srb_cdmi_connect(dbg, desc);
		if (ret)
			goto cleanup;
	}

	/* Send buffer */
retry_once:
	ret = sock_xmit(dbg, desc, 1, buff, send_size, 0);
	if (ret == -EPIPE) {
		SRB_LOG_ERR(dbg->level, "Transmission error (%d), reconnecting...", ret);
		srb_cdmi_disconnect(dbg, desc);
		if (has_epiped == 0) {
			has_epiped = 1;
			ret = srb_cdmi_connect(dbg, desc);
			if (ret)
				goto cleanup;
			goto retry_once;
		}
		goto cleanup;
	}
	if (ret != send_size) {
		ret = -EIO;
		goto cleanup;
	}
	
	/* Receive response - We want to make sure we received a full response */
	rcvd = 0;
	while (!srb_http_check_response_complete(rcvbuf, rcvd))
	{
		if (rcvd)
			SRB_LOG_WARN(dbg->level, "Response not read fully in one go: "
			             "read %i bytes until now", rcvd);

		ret = sock_xmit(dbg, desc, 0, rcvbuf+rcvd, rcv_size-rcvd, strict_rcv);
		/* Is the connection to be reopened ? */
		if (ret < 0) {
			if (ret == -EPIPE) {
				srb_cdmi_disconnect(dbg, desc);
				if (has_epiped == 0) {
					has_epiped = 1;
					ret = srb_cdmi_connect(dbg, desc);
					if (ret)
						goto cleanup;
					goto retry_once;
				}
			}
			goto cleanup;
		}
		rcvd += ret;
		ret = rcvd;
	}

	memcpy(buff, rcvbuf, rcvd);

cleanup:
	if (rcvbuf)
		kfree(rcvbuf);
	return ret;
}

static int sock_send_sglist_receive(srb_debug_t *dbg,
				struct srb_cdmi_desc_s *desc,
				int send_size, int rcv_size)
{
	char *buff = desc->xmit_buff;
	int strict_rcv = 1;
	int i;
	int ret;
	int rcvd;
	char *rcvbuf = NULL;
	int has_epiped = 0;

	if (rcv_size == 0) {
		strict_rcv = 0;
		rcv_size = SRB_XMIT_BUFFER_SIZE;
	}
	rcvbuf = kmalloc(rcv_size, GFP_KERNEL);
	if (rcvbuf == NULL) {
		ret = -ENOMEM;
		goto cleanup;
	}

	/*
	 * Check if the connection needs to be restarted:
	 * Reconnect the socket after a predefined number of HTTP
	 * requests sent.
	 */
	if (desc->nb_requests == SRB_REUSE_LIMIT) {
		SRB_LOG_DEBUG(dbg->level, "Limit of %u requests reached reconnecting socket", SRB_REUSE_LIMIT);
		srb_cdmi_disconnect(dbg, desc);
	}
	else
		desc->nb_requests++;

	/*
	 * Reconnect whether it's been resetted manually (SRB_REUSE_LIMIT) or
	 * it was left disconnected
	 */
	if (desc->state == CDMI_DISCONNECTED) {
		ret = srb_cdmi_connect(dbg, desc);
		if (ret)
			goto cleanup;
	}

	/* Send buffer */
retry_once:
	ret = sock_xmit(dbg, desc, 1, buff, send_size, 0);
	if (ret == -EPIPE) {
		SRB_LOG_ERR(dbg->level, "Transmission error (%d), reconnecting...", ret);
		srb_cdmi_disconnect(dbg, desc);
		if (has_epiped == 0) {
			has_epiped = 1;
			ret = srb_cdmi_connect(dbg, desc);
			if (ret)
				goto cleanup;
			goto retry_once;
		}
		goto cleanup;
	}
	if (ret != send_size) {
		SRB_LOG_ERR(dbg->level, "Incomplete transmission (%d of %d), returning", ret, send_size);
		ret = -EIO;
		goto cleanup;
	}

	/* Now iterate through the sglist */
	for (i = 0; i < desc->sgl_size; i++) {
		char *buff = sg_virt(&desc->sgl[i]);
		int length = desc->sgl[i].length;

		ret = sock_xmit(dbg, desc, 1, buff, length, 0);
		if (ret == -EPIPE) {
			SRB_LOG_ERR(dbg->level, "Transmission error (%d), reconnecting...", ret);
			srb_cdmi_disconnect(dbg, desc);
			if (has_epiped == 0) {
				has_epiped = 1;
				ret = srb_cdmi_connect(dbg, desc);
				if (ret)
					goto cleanup;
				goto retry_once;
			}
			goto cleanup;
		}
		if (ret != length) {
			SRB_LOG_ERR(dbg->level, "Incomplete transmission (%d of %d), returning",
				ret, length);
			ret = -EIO;
			goto cleanup;
		}
	}
	
	/* Receive response */
	rcvd = 0;
	while (!srb_http_check_response_complete(rcvbuf, rcvd))
	{
		if (rcvd)
			SRB_LOG_WARN(dbg->level, "Response not read fully in one go: "
						  "read %i bytes until now", rcvd);

		ret = sock_xmit(dbg, desc, 0, rcvbuf+rcvd, rcv_size-rcvd, strict_rcv);
		/* Is the connection to be reopened ? */
		if (ret < 0) {
			if (ret == -EPIPE) {
				srb_cdmi_disconnect(dbg, desc);
				if (has_epiped == 0) {
					has_epiped = 1;
					ret = srb_cdmi_connect(dbg, desc);
					if (ret)
						goto cleanup;
					goto retry_once;
				}
			}
			goto cleanup;
		}
		rcvd += ret;
		ret = rcvd;
	}

	memcpy(buff, rcvbuf, rcvd);

cleanup:
	if (rcvbuf)
		kfree(rcvbuf);
	return ret;
}

static int retried_send_receive(srb_debug_t *dbg,
				struct srb_cdmi_desc_s *desc,
				int send_size, int rcv_size,
				int do_sglist, int attempts)
{
	int ret = -1;
	int i;

	if (attempts < 1)
		return -EINVAL;

	/*
         * TODO: Handle CDMI request retry (Failover: Issue #22)
	 *
	 * This meanss that in case of EPIPE (only?),
	 * we must switch to another server url.
	 */
	for (i = 0; i < attempts; i++) {
		if (do_sglist) {
			ret = sock_send_sglist_receive(dbg, desc, send_size, rcv_size);
		}
		else {
			ret = sock_send_receive(dbg, desc, send_size, rcv_size);
		}

		/* If some data is returned, then the response is whole */
		if (ret >= 0)
			break;
		else if (i < attempts - 1)
			SRB_LOG_NOTICE(dbg->level, "Retrying CDMI request... %d", (i + 1));
	}

	return ret;
}

int srb_cdmi_list(srb_debug_t *dbg,
		   struct srb_cdmi_desc_s *desc,
		   int (*volume_cb)(void * data, const char *),
		   void *cb_data)
{
	jsmn_parser	json_parser;
	jsmntok_t	*json_tokens = NULL, *json_tokens2 = NULL;
	unsigned int	n_tokens = 0;
	jsmnerr_t	json_err = JSMN_ERROR_NOMEM;

	char filename[SRB_URL_SIZE+1];
	char *buff = desc->xmit_buff;

	enum srb_http_statuscode code;
	char *content = NULL;
	uint64_t contentlen;

	int len;
	int ret;
	int array, obj, found;
	int cb_errcount = 0;

	if (!desc->socket)
		return 0;

	// Construct HTTTP GET (for listing container)
	len = srb_http_mklist(buff, SRB_XMIT_BUFFER_SIZE,
			       desc->ip_addr, desc->filename);
	if (len <= 0) return len;

	len = sock_send_receive(dbg, desc, len, 0);
	if (len < 0) return len;

	// Check response status
	ret = srb_http_get_status(buff, len, &code);
	if (ret == -1)
	{
		SRB_LOG_ERR(dbg->level, "[list] Cannot retrieve response status");
		ret = -EIO;
		goto err;
	}
	if (code != SRB_HTTP_STATUS_OK)
	{
		SRB_LOG_ERR(dbg->level, "[list] Server listing yielded "
			   "response status %i", code);
		ret = -EIO;
		goto err;
	}

	// Get content length
	ret = srb_http_header_get_uint64(buff, len,
					  "Content-Length", &contentlen);
	if (ret)
	{
		SRB_LOG_ERR(dbg->level, "[list] Could not find content length in "
			   "response headers.");
		ret = -EIO;
		goto err;
	}

	content = kmalloc(contentlen, GFP_KERNEL);
	if (content == NULL)
	{
		SRB_LOG_ERR(dbg->level, "[list] Cannot allocate enough memory to"
			   " read volume repository.");
		ret = -ENOMEM;
		goto err;
	}
	memset(content, 0, contentlen);

	// Skip header
	ret = srb_http_skipheader(&buff, &len);
	if (ret) {
		SRB_LOG_ERR(dbg->level, "[list] skipheader failed: %d", ret);
		ret = -EIO;
		goto err;
	}
	if (len > contentlen)
	{
		SRB_LOG_ERR(dbg->level, "[list] More data left than expected:"
			   " len=%i > contentlen=%llu", len, contentlen);
		ret = -EIO;
		goto err;
	}

	// Get body
	// First, copy leftovers from buff
	memcpy(content, buff, len);
	if (len != contentlen) {
		SRB_LOG_ERR(dbg->level, "[list] Cannot read whole listing: "
		            "len: %d contentlen:%llu", len, contentlen);
		ret = -EIO;
		goto err;
	}

	// Now retrieve the list of objects...
#define SRB_N_JSON_TOKENS	128
	jsmn_init(&json_parser);
	n_tokens = SRB_N_JSON_TOKENS;
	do
	{
		n_tokens *= 2;
		json_tokens2 = json_tokens;
		json_tokens = krealloc(json_tokens2,
				       n_tokens * sizeof(*json_tokens),
				       GFP_KERNEL);
		if (json_tokens != NULL)
		{
			json_err = jsmn_parse(&json_parser, content, contentlen,
					      json_tokens, n_tokens);
		} else {
			/* krealloc doesn't free the original */
			kfree(json_tokens2);
		}

	} while (json_tokens != NULL
		 && json_err < 0 && json_err == JSMN_ERROR_NOMEM);

	if (json_err < 0)
	{
		if (json_tokens == NULL)
		{
			SRB_LOG_ERR(dbg->level, "Could not allocate enough memory to "
				   "parse JSON volume list.");
			ret = -ENOMEM;
		}
		else
		{
			SRB_LOG_ERR(dbg->level, "Could not parse Json: error %i", json_err);
			ret = -EIO;
		}
		goto err;
	}

	// Find the "children" object within json root object
	// for each sub-loop, ret (aka tmp iterator) is intialized to 1
	// in order to start after the object/array token it iterates within.
	found = 0;
	for (ret = 0; ret < json_err; ret ++)
	{
		jsmntok_t   *objtok = &json_tokens[ret];
		// Dive into root object
		if (objtok->parent == -1 && objtok->type == JSMN_OBJECT)
		{
			obj = ret;
			for (ret = 1; obj+ret < json_err; ret += 2)
			{
				jsmntok_t   *artok = &json_tokens[obj+ret];
				// Dive into key 'children' (should contain an array)
				if (artok->type == JSMN_STRING
				    && strncmp("children", &content[artok->start],
					       artok->end - artok->start) == 0
				    && obj+ret < json_err
				    && json_tokens[obj+ret+1].type == JSMN_ARRAY)
				{
					array = obj + ret + 1;
					SRB_LOG_DEBUG(dbg->level, "Found children list:");
					// List children of the array we found.
					for (ret = 1;
					     array + ret < json_err
					     && json_tokens[array+ret].parent == array
					     && ret < DEV_MAX;
					     ++ret)
					{
						len = json_tokens[array+ret].end
							- json_tokens[array+ret].start;
						SRB_LOG_DEBUG(dbg->level, "Volume %i: %.*s", ret, len,
							   &content[json_tokens[array+ret].start]);
						strncpy(filename,
							&content[json_tokens[array+ret].start],
							SRB_MIN(SRB_URL_SIZE, len));
						filename[SRB_MIN(SRB_URL_SIZE, len)] = 0;

						if (volume_cb(cb_data, filename) != 0) {
							cb_errcount += 1;
						}
					}
					found = 1;
					break ;
				}
			}
			if (found)
				break ;
		}
	}

	ret = 0;
	if (cb_errcount != 0)
		ret = -EIO;

err:
	if (json_tokens)
		kfree(json_tokens);
	if (content)
		kfree(content);

	return ret;
}

int srb_cdmi_flush(srb_debug_t *dbg,
		struct srb_cdmi_desc_s *desc,
		unsigned long flush_size)
{
	char *buff = desc->xmit_buff;
	uint64_t size;
	int len;
	int ret;
	enum srb_http_statuscode code;

	if (!desc->socket)
		return 0;

	/* Construct HTTP truncate */
	len = srb_http_mktruncate(buff, SRB_XMIT_BUFFER_SIZE,
				desc->ip_addr, desc->filename, flush_size);
	if (len <= 0) return len;
	
	len = sock_send_receive(dbg, desc, len, 0);
	if (len < 0) return len;

	ret = srb_http_get_status(buff, len, &code);
	if (ret != 0)
	{
		SRB_LOG_ERR(dbg->level, "Cannot get http response status.");
		return -EIO;
	}
	if (srb_http_get_status_range(code) != SRB_HTTP_STATUSRANGE_SUCCESS)
	{
		SRB_LOG_ERR(dbg->level, "Http server responded with bad status: %i", code);
		if (code == SRB_HTTP_STATUS_NOT_FOUND)
			return -ENODEV;
		return -EIO;
	}

	buff[len] = 0;
	ret = srb_http_header_get_uint64(buff, len, "Content-Length", &size);
	if (ret)
		return -EIO;

	return 0;
}

int srb_cdmi_extend(srb_debug_t *dbg,
		struct srb_cdmi_desc_s *desc,
		unsigned long long trunc_size)
{
	char *buff = desc->xmit_buff;
	unsigned long long cur_size = 0;
	int len;
	int ret;
	enum srb_http_statuscode code;

	if (!desc || !desc->socket)
		return -EINVAL;

	ret = srb_cdmi_getsize(dbg, desc, &cur_size);
	if (ret != 0) {
		SRB_LOG_ERR(dbg->level, "[extend] Could not get size of existing volume.");
		return ret;
	}

	if (cur_size >= trunc_size) {
		SRB_LOG_ERR(dbg->level, "[extend] Cannot shrink a volume.");
		return -EINVAL;
	}

	/* Construct/send HTTP truncate */
	len = srb_http_mktruncate(buff, SRB_XMIT_BUFFER_SIZE,
				   desc->ip_addr, desc->filename, trunc_size);
	if (len <= 0) return len;

	len = sock_send_receive(dbg, desc, len, 0);
	if (len < 0) return len;

	ret = srb_http_get_status(buff, len, &code);
	if (ret == -1) {
		SRB_LOG_ERR(dbg->level, "[extend] Cannot retrieve response status");
		return -EIO;
	}

	if (srb_http_get_status_range(code) != SRB_HTTP_STATUSRANGE_SUCCESS) {
		SRB_LOG_ERR(dbg->level, "[extend] Status of extend operation = %i.", code);
		return -EIO;
	}

	return 0;
}

int srb_cdmi_create(srb_debug_t *dbg,
		struct srb_cdmi_desc_s *desc,
		unsigned long long trunc_size)
{
	char *buff = desc->xmit_buff;
	int len;
	int ret;
	enum srb_http_statuscode code;

	if (!desc || !desc->socket)
		return -EINVAL;

	/* Construct/send HTTP create */
	len = srb_http_mkcreate(buff, SRB_XMIT_BUFFER_SIZE,
				 desc->ip_addr, desc->filename);
	if (len <= 0) return len;

	len = sock_send_receive(dbg, desc, len, 0);
	if (len < 0) return len;

	ret = srb_http_get_status(buff, len, &code);
	if (ret == -1) {
		SRB_LOG_ERR(dbg->level, "[create] Cannot retrieve response status from %.*s", 32, buff);
		return -EIO;
	}

	if (srb_http_get_status_range(code) != SRB_HTTP_STATUSRANGE_SUCCESS) {
		SRB_LOG_ERR(dbg->level, "[create] Status of create operation = %i.", code);
		return -EIO;
	}

	/* Construct/send HTTP truncate */
	len = srb_http_mktruncate(buff, SRB_XMIT_BUFFER_SIZE,
				   desc->ip_addr, desc->filename, trunc_size);
	if (len <= 0) return len;

	len = sock_send_receive(dbg, desc, len, 0);
	if (len < 0) return len;

	ret = srb_http_get_status(buff, len, &code);
	if (ret == -1) {
		SRB_LOG_ERR(dbg->level, "[create-trunc] Cannot retrieve response status");
		return -EIO;
	}

	if (srb_http_get_status_range(code) != SRB_HTTP_STATUSRANGE_SUCCESS) {
		SRB_LOG_ERR(dbg->level, "[create-trunc] Status of create operation = %i.", code);
		return -EIO;
	}

	return 0;
}

int srb_cdmi_delete(srb_debug_t *dbg, struct srb_cdmi_desc_s *desc)
{
	char *buff = desc->xmit_buff;
	int len;
	int ret;
	enum srb_http_statuscode code;

	if (!desc || !desc->socket)
		return -EINVAL;

	/* Construct HTTP delete */
	len = srb_http_mkdelete(buff, SRB_XMIT_BUFFER_SIZE,
				desc->ip_addr, desc->filename);
	if (len <= 0) return len;
	
	len = sock_send_receive(dbg, desc, len, 0);
	if (len < 0) return len;

	ret = srb_http_get_status(buff, len, &code);
	if (ret == -1) {
		SRB_LOG_ERR(dbg->level, "[destroy] Cannot retrieve response status");
		return -EIO;
	}

	if (srb_http_get_status_range(code) != SRB_HTTP_STATUSRANGE_SUCCESS) {
		SRB_LOG_ERR(dbg->level, "[destroy] Status of delete operation = %i.", code);
		if (code == SRB_HTTP_STATUS_NOT_FOUND)
			return -ENOENT;
		return -EIO;
	}

	return 0;
}

/* HACK: due to a bug in HTTP HEAD from scality,using metadata instead */
#if 0
int srb_cdmi_getsize(srb_debug_t *dbg, struct srb_cdmi_desc_s *desc,
		uint64_t *size)
{
	char *buff = desc->xmit_buff;

	int ret, len;

	/* Construct a HEAD command */
	len = srb_http_mkhead(buff, SRB_XMIT_BUFFER_SIZE,
			desc->ip_addr, desc->filename);
	if (len <= 0) return len;

	len = sock_send_receive(dbg, desc, len, 0);
	if (len < 0) return len;
	
	buff[len] = 0;
	ret = srb_http_header_get_uint64(buff, len, "Content-Length", size);
	if (ret)
		return -EIO;

	return 0;
}
#endif

int srb_cdmi_getsize(srb_debug_t *dbg, struct srb_cdmi_desc_s *desc,
		uint64_t *size)
{
	char *buff = desc->xmit_buff;

	enum srb_http_statuscode code;
	int ret, len;

	/* Construct a GET (?metadata) command */
	len = srb_http_mkmetadata(buff, SRB_XMIT_BUFFER_SIZE,
			desc->ip_addr, desc->filename);
	if (len <= 0) return len;

	len = sock_send_receive(dbg, desc, len, 0);
	if (len < 0) return len;
	
	ret = srb_http_get_status(buff, len, &code);
	if (ret != 0)
	{
		SRB_LOG_ERR(dbg->level, "Cannot get http response status.");
		return -EIO;
	}
	if (srb_http_get_status_range(code) != SRB_HTTP_STATUSRANGE_SUCCESS)
	{
		SRB_LOG_ERR(dbg->level, "Http server responded with bad status: %i", code);
		if (code == SRB_HTTP_STATUS_NOT_FOUND)
			return -ENODEV;
		return -EIO;
	}

	buff[len] = 0;
	ret = srb_http_header_get_uint64(buff, len, "\"cdmi_size\"", size);
	if (ret)
		return -EIO;

	return 0;
}

/*
 * sends a buffer to CDMI server through a CDMI put range at primitive
 * at specified "offset" writing "size" bytes from "buff".
 */
int srb_cdmi_putrange(srb_debug_t *dbg,
		struct srb_cdmi_desc_s *desc,
		uint64_t offset, int size)
{
	char *xmit_buff = desc->xmit_buff;
	int header_size;
	int ret = -EIO;
	int len;
	uint64_t start, end;
	
	/* Calculate start, end */
	start = offset;
	end   = offset + size - 1;

	/* Construct a PUT request with range info */
	ret = srb_http_mkrange("PUT", xmit_buff, SRB_XMIT_BUFFER_SIZE,
				desc->ip_addr, desc->filename,
				start, end);
	if (ret <= 0) return ret;
	
	xmit_buff += ret;
	header_size = ret;

	len = retried_send_receive(dbg, desc, header_size, 0, 1/*sglist*/, nb_req_retries);
	if (len < 0) {
		SRB_LOG_ERR(dbg->level, "ERROR sending sglist: %d", len);
		return len;
	}

	if (len > 512) {/* Shall not get more than that */
		SRB_LOG_ERR(dbg->level, "Incorrect response size: %d", len);
		ret = -EIO;
		goto out;
	}

	if (strncmp(desc->xmit_buff, "HTTP/1.1 204 No Content",
			strlen("HTTP/1.1 204 No Content"))) {
			SRB_LOG_ERR(dbg->level, "Unable to get back HTTP confirmation buffer");
		ret = -EIO;
		goto out;
	}

	ret = 0;
out:
	return ret;
}

/*
 * get a buffer from th CDMI server through a CDMI get range primitive
 * at specified "offset" reading "size" bytes from "buff".
 */
int srb_cdmi_getrange(srb_debug_t *dbg,
		struct srb_cdmi_desc_s *desc,
		uint64_t offset, int size)
{
	char *xmit_buff = desc->xmit_buff;
	int len, rcv;
	int ret = -EIO;
	uint64_t start, end;
	int i;

	/* Calculate start, end */
	start = offset;
	end   = offset + size - 1;

	/* Construct a PUT request with range info */
	len = srb_http_mkrange("GET", xmit_buff, SRB_XMIT_BUFFER_SIZE,
				desc->ip_addr, desc->filename,
				start, end);
	if (len <= 0)
		goto out;
	
	rcv = len = retried_send_receive(dbg, desc, len, 0, 0/*no sglist*/, nb_req_retries);
	if (len < 0) return len;	

	/* Skip header */
	ret = srb_http_skipheader(&xmit_buff, &len);
	if (ret) {
		SRB_LOG_DEBUG(dbg->level, "getrange: skipheader failed: %d", ret);
		ret = -EIO;
		goto out;
	}

	// sock_send_receive makes sure to read the whole response,
	// so we shall have the whole data.
	if (len != size) {
		SRB_LOG_DEBUG(dbg->level, "getrange error: len: %d size:%d", len, size);
		ret = -EIO;
		goto out;
	}

	for (i = 0; i < desc->sgl_size; i++) {
		char *buff = sg_virt(&desc->sgl[i]);
		int length = desc->sgl[i].length;

		memcpy(buff, xmit_buff, length);
		xmit_buff += length;
	}
	ret = 0;
out:

	return ret;
}

/* srb_cdmi_sync(desc, start, end) */
/*
 * asks the CDMI server to sync from start offset to end offset
 */
