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
#include <linux/types.h>     // for uintx_t
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/socket.h>
#include <linux/tcp.h>
#include "dewb.h"

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

/* dewb_cdmi_init (URL)
 *
 * Parse url and initialise cdmi structure in all threads descriptors
 * 
 */
int dewb_cdmi_init(dewb_debug_t *dbg,
		struct dewb_cdmi_desc_s *desc,
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

	strncpy(desc->url, url, DEWB_URL_SIZE);

	desc->url[DEWB_URL_SIZE] = 0;
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

	DEWB_LOG_DEBUG(dbg->level, "Decoded URL [ip=%s port=%d file=%s]",
                   desc->ip_addr, desc->port, desc->filename);

	return 0;	
}

/* dewb_cdmi_connect
 *
 * Connect thread pools descriptors
 *
 * Returns 0 if successfull or a negative value depending the error.
 */
int dewb_cdmi_connect(dewb_debug_t *dbg,
		struct dewb_cdmi_desc_s *desc)
{
	int ret;
	int arg = 1;

	if (desc->state == CDMI_CONNECTED)
		return -EINVAL;

	/* Init socket */
	ret = sock_create(PF_INET, SOCK_STREAM, IPPROTO_TCP, &desc->socket);
	if (ret) {
		DEWB_LOG_ERR(dbg->level, "Unable to create socket: %d", ret);
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
		DEWB_LOG_ERR(dbg->level, "Unable to connect to cdmi server: %d", -ret);
		goto out_error;
	}
	desc->state = CDMI_CONNECTED;

	ret = kernel_setsockopt(desc->socket,
		IPPROTO_TCP, TCP_NODELAY, (char *)&arg,
		sizeof(arg));
	if (ret < 0) {
		DEWB_LOG_ERR(dbg->level, "setsockopt failed: %d", ret);
		goto out_error;
	}

	/* TODO: set request timeout value (Issue #22) */
	if (desc->timeout.tv_sec > 0) {
		DEWB_LOG_DEBUG(dbg->level, "dewb_cdmi_connect: set socket timeout %lu", desc->timeout.tv_sec);
		ret = kernel_setsockopt(desc->socket, SOL_SOCKET, SO_RCVTIMEO, 
			(char *)&desc->timeout, sizeof(struct timeval));
		if (ret < 0) {
			DEWB_LOG_ERR(dbg->level, "Failed to set socket receive timeout value: %d", ret);
		}
		ret = kernel_setsockopt(desc->socket, SOL_SOCKET, SO_SNDTIMEO, (char *)&desc->timeout, sizeof(struct timeval));
		if (ret < 0) {
			DEWB_LOG_ERR(dbg->level, "Failed to set socket send timeout value: %d", ret);
		}
	}

	/* As we established a new connection, reset the number of
	   HTTP requests sent */
	desc->nb_requests = 0;

	return 0;
out_error:
	kernel_sock_shutdown(desc->socket, SHUT_RDWR);
	desc->socket = NULL;

	return ret;
}

/* dewb_cdmi_disconnect
 *
 * Disctonnect current descriptor from CDMI server
 *
 */
int dewb_cdmi_disconnect(dewb_debug_t *dbg,
			struct dewb_cdmi_desc_s *desc)
{
	if (!desc->socket)
		return 0;

	kernel_sock_shutdown(desc->socket, SHUT_RDWR);
	desc->socket = NULL;
	desc->state  = CDMI_DISCONNECTED;
	return 0;
}


/* static void sock_xmit_timeout(unsigned long arg)
{
        struct task_struct *task = (struct task_struct *)arg;

        DEWB_LOG(KERN_WARNING, "dewb sock: killing hung xmit (%s, pid: %d)\n",
                task->comm, task->pid);
        force_sig(SIGKILL, task);
} */

/*
 *  Send or receive packet.
 */
static int sock_xmit(dewb_debug_t *dbg,
		struct dewb_cdmi_desc_s *desc,
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
		DEWB_LOG_ERR(dbg->level, "Attempted %s on closed socket in sock_xmit\n",
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
			/* TODO: add a socket timer
			struct timer_list ti; 
			DEWB_LOG(KERN_INFO, "Initializing request timeout: %d", desc->xmit_timeout);
			if (desc->xmit_timeout > 0) {
				init_timer(&ti);
				ti.function = sock_xmit_timeout;
				ti.data = (unsigned long) current;
				ti.expires = jiffies + req_timeout;
				add_timer(&ti);
			} */

			result = kernel_sendmsg(desc->socket, &msg, &iov, 1, size);

			/* if (desc->xmit_timeout > 0)
				del_timer_sync(&ti); */
		} else
			result = kernel_recvmsg(desc->socket, &msg, &iov, 1, size,
						msg.msg_flags);
		DEWB_LOG_DEBUG(dbg->level, "Result for socket exchange: %d", result);
		if (signal_pending(current)) {
			siginfo_t info;
			DEWB_LOG_INFO(dbg->level, "dewb (pid %d: %s) got signal %d\n",
				task_pid_nr(current), current->comm,
				dequeue_signal_lock(current, &current->blocked, &info));
			result = -EINTR;
			//sock_shutdown(nbd, !send);
			break;
		}

		if (result && (!send) && (!strict_receive))
			break;

		if (result == 0)  {
			DEWB_LOG_DEBUG(dbg->level, "Empty socket exhange (size: %d)", size);
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

static int sock_send_receive(dewb_debug_t *dbg,
			struct dewb_cdmi_desc_s *desc,
			int send_size, int rcv_size)
{
	char *buff = desc->xmit_buff;
	int strict_rcv = (rcv_size) ? 1 : 0;
	int ret;

	if (rcv_size == 0)
		rcv_size = DEWB_XMIT_BUFFER_SIZE;

	/* Check if the connection needs to be restarted */
	/* Reconnect the socket after a predefined number of HTTP
	 * requests sent.
	 */
	if (desc->nb_requests == DEWB_REUSE_LIMIT) {
		DEWB_LOG_DEBUG(dbg->level, "Limit of %u requests reached reconnecting socket", DEWB_REUSE_LIMIT);
		dewb_cdmi_disconnect(dbg, desc);
		ret = dewb_cdmi_connect(dbg, desc);
		if (ret) return ret;
	}
	else
		desc->nb_requests++;

	/* Send buffer */
xmit_again:
	ret = sock_xmit(dbg, desc, 1, buff, send_size, 0);
	if (ret == -EPIPE) {
		dewb_cdmi_disconnect(dbg, desc);
		ret = dewb_cdmi_connect(dbg, desc);
		if (ret) return ret;
		goto xmit_again;
	}
	if (ret != send_size)
		return -EIO;
	
	/* Receive response */
	ret = sock_xmit(dbg, desc, 0, buff, rcv_size, strict_rcv);
	/* Is the connection to be reopened ? */
	if (ret == -EPIPE) {
		dewb_cdmi_disconnect(dbg, desc);
		ret = dewb_cdmi_connect(dbg, desc);
		if (ret) return ret;
		goto xmit_again;
	}

	return ret;
}

static int sock_send_sglist_receive(dewb_debug_t *dbg,
				struct dewb_cdmi_desc_s *desc,
				int send_size, int rcv_size)
{
	char *buff = desc->xmit_buff;
	int strict_rcv = (rcv_size) ? 1 : 0;
	int i;
	int ret;

	if (rcv_size == 0)
		rcv_size = DEWB_XMIT_BUFFER_SIZE;

	/* Check if the connection needs to be restarted */
	/* Reconnect the socket after a predefined number of HTTP
	 * requests sent.
	 */
	if (desc->nb_requests == DEWB_REUSE_LIMIT) {
		DEWB_LOG_DEBUG(dbg->level, "Limit of %u requests reached reconnecting socket", DEWB_REUSE_LIMIT);
		dewb_cdmi_disconnect(dbg, desc);
		ret = dewb_cdmi_connect(dbg, desc);
		if (ret) return ret;
	}
	else
		desc->nb_requests++;

	/* Send buffer */
xmit_again:
	ret = sock_xmit(dbg, desc, 1, buff, send_size, 0);
	if (ret == -EPIPE) {
		DEWB_LOG_ERR(dbg->level, "Transmission error (%d), reconnecting...", ret);
		dewb_cdmi_disconnect(dbg, desc);
		ret = dewb_cdmi_connect(dbg, desc);
		if (ret) return ret;
		goto xmit_again;
	}
	if (ret != send_size) {
		DEWB_LOG_ERR(dbg->level, "Incomplete transmission (%d of %d), returning", ret, send_size);
		return -EIO;
	}

	/* Now iterate through the sglist */
	for (i = 0; i < desc->sgl_size; i++) {
		char *buff = sg_virt(&desc->sgl[i]);
		int length = desc->sgl[i].length;

		ret = sock_xmit(dbg, desc, 1, buff, length, 0);
		if (ret == -EPIPE) {
			DEWB_LOG_ERR(dbg->level, "Transmission error (%d), reconnecting...", ret);
			dewb_cdmi_disconnect(dbg, desc);
			ret = dewb_cdmi_connect(dbg, desc);
			if (ret) return ret;
			goto xmit_again;
		}
		if (ret != length) {
			DEWB_LOG_ERR(dbg->level, "Incomplete transmission (%d of %d), returning",
				ret, length);
			return -EIO;
		}
	}
	
	ret = sock_xmit(dbg, desc, 1, "\r\n", 2, 0);
	if (ret == -EPIPE) {
		DEWB_LOG_ERR(dbg->level, "Transmission error (%d), reconnecting...", ret);
		dewb_cdmi_disconnect(dbg, desc);
		ret = dewb_cdmi_connect(dbg, desc);
		if (ret) return ret;
		goto xmit_again;
	}
	if (ret != 2) {
		DEWB_LOG_ERR(dbg->level, "Incomplete transmission %d of %d), returning", ret, 2);
		return -EIO;
	}

	/* Receive response */
	ret = sock_xmit(dbg, desc, 0, buff, rcv_size, strict_rcv);
	/* Is the connection to be reopened ? */
	if (ret == -EPIPE) {
		DEWB_LOG_ERR(dbg->level, "Transmission error (%d), reconnecting...", ret);
		dewb_cdmi_disconnect(dbg, desc);
		ret = dewb_cdmi_connect(dbg, desc);
		if (ret) return ret;
		goto xmit_again;
	}

	return ret;
}


/* int dewb_cdmi_list(dewb_debug_t *dbg,
		   struct dewb_cdmi_desc_s *desc,
		   int (*volume_cb)(const char *)) */
int dewb_cdmi_list(dewb_debug_t *dbg,
		   struct dewb_cdmi_desc_s *desc,
		   int (*volume_cb)(struct dewb_cdmi_desc_s *, const char *))
{
	jsmn_parser	json_parser;
	jsmntok_t	*json_tokens = NULL;
	unsigned int	n_tokens = 0;
	jsmnerr_t	json_err = JSMN_ERROR_NOMEM;

	char filename[DEWB_URL_SIZE+1];
	char *buff = desc->xmit_buff;

	enum dewb_http_statuscode code;
	char *content = NULL;
	uint64_t contentlen;

	int len;
	int ret;
	int array, obj, found;
	int cb_errcount = 0;

	if (!desc->socket)
		return 0;

	// Construct HTTTP GET (for listing container)
	len = dewb_http_mklist(buff, DEWB_XMIT_BUFFER_SIZE,
			       desc->ip_addr, desc->filename);
	if (len <= 0) return len;

	len = sock_send_receive(dbg, desc, len, 0);
	if (len < 0) return len;

	// Check response status
	ret = dewb_http_get_status(buff, len, &code);
	if (ret == -1)
	{
		DEWB_LOG_ERR(dbg->level, "[list] Cannot retrieve response status");
		ret = -EIO;
		goto err;
	}
	if (code != DEWB_HTTP_STATUS_OK)
	{
		DEWB_LOG_ERR(dbg->level, "[list] Mirror listing yielded "
			   "response status %i", code);
		ret = -EIO;
		goto err;
	}

	// Get content length
	ret = dewb_http_header_get_uint64(buff, len,
					  "Content-Length", &contentlen);
	if (ret)
	{
		DEWB_LOG_ERR(dbg->level, "[list] Could not find content length in "
			   "response headers.");
		ret = -EIO;
		goto err;
	}

	content = kmalloc(contentlen, GFP_KERNEL);
	if (content == NULL)
	{
		DEWB_LOG_ERR(dbg->level, "[list] Cannot allocate enough memory to"
			   " read volume repository.");
		ret = -ENOMEM;
		goto err;
	}

	// Skip header
	ret = dewb_http_skipheader(&buff, &len);
	if (ret) {
		DEWB_LOG_ERR(dbg->level, "getrange: skipheader failed: %d", ret);
		ret = -EIO;
		goto err;
	}
	if (len > contentlen)
	{
		DEWB_LOG_ERR(dbg->level, "[list] More data left than expected:"
			   " len=%i > contentlen=%llu", len, contentlen);
		ret = -EIO;
		goto err;
	}

	// Get body
	// First, copy leftovers from buff
	memcpy(content, buff, len);
	// More bytes may have to be read; get them
	while (len < contentlen) {
		ret = sock_xmit(dbg, desc, 0, content + len, contentlen - len, 1);
		if (ret < 0) {
			DEWB_LOG_ERR(dbg->level, "ERROR sock xmit ret = %d", ret);
			ret = -EIO;
			goto err;
		}
		len += ret;
	}

	if (len != contentlen) {
		DEWB_LOG_ERR(dbg->level, "getrange error: len: %d contentlen:%llu", len, contentlen);
		ret = -EIO;
		goto err;
	}

	// Now retrieve the list of objects...
#define DEWB_N_JSON_TOKENS	128
	jsmn_init(&json_parser);
	n_tokens = DEWB_N_JSON_TOKENS;
	do
	{
		n_tokens *= 2;
		json_tokens = krealloc(json_tokens,
				       n_tokens * sizeof(*json_tokens),
				       GFP_KERNEL);
		if (json_tokens != NULL)
		{
			json_err = jsmn_parse(&json_parser, content, contentlen,
					      json_tokens, n_tokens);
		}
	} while (json_tokens != NULL
		 && json_err < 0 && json_err == JSMN_ERROR_NOMEM);

	if (json_err < 0)
	{
		if (json_tokens == NULL)
		{
			DEWB_LOG_ERR(dbg->level, "Could not allocate enough memory to "
				   "parse JSON volume list.");
			ret = -ENOMEM;
		}
		else
		{
			DEWB_LOG_ERR(dbg->level, "Could not parse Json: error %i", json_err);
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
					DEWB_LOG_DEBUG(dbg->level, "Found children list:");
					// List children of the array we found.
					for (ret = 1;
					     array + ret < json_err
					     && json_tokens[array+ret].parent == array;
					     ++ret)
					{
						len = json_tokens[array+ret].end
							- json_tokens[array+ret].start;
						DEWB_LOG_DEBUG(dbg->level, "Volume %i: %.*s", ret, len,
							   &content[json_tokens[array+ret].start]);
						strncpy(filename,
							&content[json_tokens[array+ret].start],
							DEWB_MIN(DEWB_URL_SIZE, len));
						filename[DEWB_MIN(DEWB_URL_SIZE, len)] = 0;

						//if (volume_cb(filename) != 0)
						if (volume_cb(desc, filename) != 0) {
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

int dewb_cdmi_flush(dewb_debug_t *dbg,
		struct dewb_cdmi_desc_s *desc, 
		unsigned long flush_size)
{
	char *buff = desc->xmit_buff;
	uint64_t size;
	int len;
	int ret;

	if (!desc->socket)
		return 0;

	/* Construct HTTP truncate */
	len = dewb_http_mktruncate(buff, DEWB_XMIT_BUFFER_SIZE, 
				desc->ip_addr, desc->filename, flush_size);
	if (len <= 0) return len;
	
	len = sock_send_receive(dbg, desc, len, 0);
	if (len < 0) return len;

	buff[len] = 0;
	ret = dewb_http_header_get_uint64(buff, len, "Content-Length", &size);
	if (ret)
		return -EIO;

	return 0;
}

int dewb_cdmi_extend(dewb_debug_t *dbg,
		struct dewb_cdmi_desc_s *desc,
		unsigned long long trunc_size)
{
	char *buff = desc->xmit_buff;
	unsigned long long cur_size = 0;
	int len;
	int ret;
	enum dewb_http_statuscode code;

	if (!desc->socket)
		return 0;

	ret = dewb_cdmi_getsize(dbg, desc, &cur_size);
	if (ret != 0)
	{
		DEWB_LOG_ERR(dbg->level, "[extend] Could not get size of existing volume.");
		return ret;
	}

	if (cur_size >= trunc_size)
	{
		DEWB_LOG_ERR(dbg->level, "[extend] Cannot shrink a volume.");
		return -EINVAL;
	}

	/* Construct/send HTTP truncate */
	len = dewb_http_mktruncate(buff, DEWB_XMIT_BUFFER_SIZE,
				   desc->ip_addr, desc->filename, trunc_size);
	if (len <= 0) return len;

	len = sock_send_receive(dbg, desc, len, 0);
	if (len < 0) return len;

	ret = dewb_http_get_status(buff, len, &code);
	if (ret == -1)
	{
		DEWB_LOG_ERR(dbg->level, "[extend] Cannot retrieve response status");
		return -EIO;
	}

	if (dewb_http_get_status_range(code) != DEWB_HTTP_STATUSRANGE_SUCCESS)
	{
		DEWB_LOG_ERR(dbg->level, "[extend] Status of extend operation = %i.", code);
		return -EIO;
	}

	return 0;
}

int dewb_cdmi_create(dewb_debug_t *dbg,
		struct dewb_cdmi_desc_s *desc,
		unsigned long long trunc_size)
{
	char *buff = desc->xmit_buff;
	int len;
	int ret;
	enum dewb_http_statuscode code;

	if (!desc->socket)
		return 0;

	/* Construct/send HTTP create */
	len = dewb_http_mkcreate(buff, DEWB_XMIT_BUFFER_SIZE,
				 desc->ip_addr, desc->filename);
	if (len <= 0) return len;

	len = sock_send_receive(dbg, desc, len, 0);
	if (len < 0) return len;

	ret = dewb_http_get_status(buff, len, &code);
	if (ret == -1)
	{
		DEWB_LOG_ERR(dbg->level, "[create] Cannot retrieve response status from %.*s", 32, buff);
		return -EIO;
	}

	if (dewb_http_get_status_range(code) != DEWB_HTTP_STATUSRANGE_SUCCESS)
	{
		DEWB_LOG_ERR(dbg->level, "[create] Status of create operation = %i.", code);
		return -EIO;
	}

	/* Construct/send HTTP truncate */
	len = dewb_http_mktruncate(buff, DEWB_XMIT_BUFFER_SIZE,
				   desc->ip_addr, desc->filename, trunc_size);
	if (len <= 0) return len;

	len = sock_send_receive(dbg, desc, len, 0);
	if (len < 0) return len;

	ret = dewb_http_get_status(buff, len, &code);
	if (ret == -1)
	{
		DEWB_LOG_ERR(dbg->level, "[create] Cannot retrieve response status");
		return -EIO;
	}

	if (dewb_http_get_status_range(code) != DEWB_HTTP_STATUSRANGE_SUCCESS)
	{
		DEWB_LOG_ERR(dbg->level, "[create] Status of create operation = %i.", code);
		return -EIO;
	}

	return 0;
}

int dewb_cdmi_delete(dewb_debug_t *dbg, struct dewb_cdmi_desc_s *desc)
{
	char *buff = desc->xmit_buff;
	int len;
	int ret;
	enum dewb_http_statuscode code;

	if (!desc->socket)
		return 0;

	/* Construct HTTP delete */
	len = dewb_http_mkdelete(buff, DEWB_XMIT_BUFFER_SIZE,
				desc->ip_addr, desc->filename);
	if (len <= 0) return len;
	
	len = sock_send_receive(dbg, desc, len, 0);
	if (len < 0) return len;

	ret = dewb_http_get_status(buff, len, &code);
	if (ret == -1)
	{
		DEWB_LOG_ERR(dbg->level, "[destroy] Cannot retrieve response status");
		return -EIO;
	}

	if (dewb_http_get_status_range(code) != DEWB_HTTP_STATUSRANGE_SUCCESS)
	{
		DEWB_LOG_ERR(dbg->level, "[destroy] Status of delete operation = %i.", code);
		if (code == DEWB_HTTP_STATUS_NOT_FOUND)
			return -ENOENT;
		return -EIO;
	}

	return 0;
}

/* HACK: due to a bug in HTTP HEAD from scality,using metadata instead */
#if 0
int dewb_cdmi_getsize(dewb_debug_t *dbg, struct dewb_cdmi_desc_s *desc,
		uint64_t *size)
{
	char *buff = desc->xmit_buff;

	int ret, len;

	/* Construct a HEAD command */
	len = dewb_http_mkhead(buff, DEWB_XMIT_BUFFER_SIZE, 
			desc->ip_addr, desc->filename);
	if (len <= 0) return len;

	len = sock_send_receive(dbg, desc, len, 0);
	if (len < 0) return len;
	
	buff[len] = 0;
	ret = dewb_http_header_get_uint64(buff, len, "Content-Length", size);
	if (ret)
		return -EIO;

	return 0;
}
#endif

int dewb_cdmi_getsize(dewb_debug_t *dbg, struct dewb_cdmi_desc_s *desc,
		uint64_t *size)
{
	char *buff = desc->xmit_buff;

	enum dewb_http_statuscode code;
	int ret, len;

	/* Construct a GET (?metadata) command */
	len = dewb_http_mkmetadata(buff, DEWB_XMIT_BUFFER_SIZE, 
			desc->ip_addr, desc->filename);
	if (len <= 0) return len;

	len = sock_send_receive(dbg, desc, len, 0);
	if (len < 0) return len;
	
	ret = dewb_http_get_status(buff, len, &code);
	if (ret != 0)
	{
		DEWB_LOG_ERR(dbg->level, "Cannot get http response status.");
		return -EIO;
	}
	if (dewb_http_get_status_range(code) != DEWB_HTTP_STATUSRANGE_SUCCESS)
	{
		DEWB_LOG_ERR(dbg->level, "Http server responded with bad status: %i", code);
		if (code == DEWB_HTTP_STATUS_NOT_FOUND)
			return -ENODEV;
		return -EIO;
	}

	buff[len] = 0;
	ret = dewb_http_header_get_uint64(buff, len, "\"cdmi_size\"", size);
	if (ret)
		return -EIO;

	return 0;
}

/* dewb_cdmi_putrange(desc, buff, offset, size)
 *
 * sends a buffer to CDMI server through a CDMI put range at primitive
 * at specified "offset" reading "size" bytes from "buff".
 */
int dewb_cdmi_putrange(dewb_debug_t *dbg,
		struct dewb_cdmi_desc_s *desc,
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
	ret = dewb_http_mkrange("PUT", xmit_buff, DEWB_XMIT_BUFFER_SIZE, 
				desc->ip_addr, desc->filename, 
				start, end);
	if (ret <= 0) return ret;
	
	xmit_buff += ret;
	header_size = ret;

	len = sock_send_sglist_receive(dbg, desc, header_size, 0);
	if (len < 0) {
		DEWB_LOG_ERR(dbg->level, "ERROR sending sglist: %d", len);
		return len;
	}

	if (len > 512) {/* Shall not get more than that */
		DEWB_LOG_ERR(dbg->level, "Incorrect response size: %d", len);
		ret = -EIO;
		goto out;
	}

	if (strncmp(desc->xmit_buff, "HTTP/1.1 204 No Content",
			strlen("HTTP/1.1 204 No Content"))) {
			DEWB_LOG_ERR(dbg->level, "Unable to get back HTTP confirmation buffer");
		ret = -EIO;
		goto out;
	}

	ret = 0;
out:
	return ret;
}

/* dewb_cdmi_getrange(desc, start, end, buff) */
/*
 * get a buffer from th CDMI server through a CDMI get range primitive
 *
 */
int dewb_cdmi_getrange(dewb_debug_t *dbg,
		struct dewb_cdmi_desc_s *desc,
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
	len = dewb_http_mkrange("GET", xmit_buff, DEWB_XMIT_BUFFER_SIZE, 
				desc->ip_addr, desc->filename, 
				start, end);
	if (len <= 0) 
		goto out;
	
	rcv = len = sock_send_receive(dbg, desc, len, 0);
	if (len < 0) return len;	

	/* Skip header */
	ret = dewb_http_skipheader(&xmit_buff, &len);
	if (ret) {
		DEWB_LOG_DEBUG(dbg->level, "getrange: skipheader failed: %d", ret);
		ret = -EIO;
		goto out;
	}

	// More bytes may have to be read
	while (len < size) {
		DEWB_LOG_DEBUG(dbg->level, "Have to read more [read=%d, toread=%d]",
			len, size - len);
		ret = sock_xmit(dbg, desc, 0, desc->xmit_buff + rcv, size - len, 0);
		if (ret < 0) {
			DEWB_LOG_ERR(dbg->level, "ERROR sock xmit ret = %d", ret);
			return -EIO;
		}
		len += ret;
		rcv += ret;
	}

	if (len != size) {
		DEWB_LOG_DEBUG(dbg->level, "getrange error: len: %d size:%d", len, size);
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
/* dewb_cdmi_sync(desc, start, end) */
/*
 * asks the CDMI server to sync from start offset to end offset
 */
