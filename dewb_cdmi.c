#include <linux/module.h>    // included for all kernel modules
#include <linux/kernel.h>    // included for KERN_INFO
#include <linux/types.h>     // for uintx_t
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/socket.h>
#include <linux/tcp.h>
#include "dewb.h"

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

	strncpy(desc->url, url, DEWB_URL_SIZE + 1);

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
	
	DEWB_DEBUG("Decoded URL [ip=%s port=%d file=%s]",
		ip, desc->port, desc->filename);

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
		DEWB_ERROR("Unable to create socket: %d", ret);
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
		DEWB_ERROR( "Unable to connect to cdmi server : %d", -ret);
		goto out_error;
	}
	desc->state = CDMI_CONNECTED;

	ret = kernel_setsockopt(desc->socket,
		IPPROTO_TCP, TCP_NODELAY, (char *)&arg,
		sizeof(arg));
	if (ret < 0) {
		DEWB_ERROR("setsockopt failed: %d", ret);
		goto out_error;
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
		DEWB_ERROR("Attempted %s on closed socket in sock_xmit\n",
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
		} else
			result = kernel_recvmsg(desc->socket, &msg, &iov, 1, size,
						msg.msg_flags);
		DEWB_DEBUG("Result = %d", result);
		if (signal_pending(current)) {
			siginfo_t info;
			DEWB_INFO("nbd (pid %d: %s) got signal %d\n",
				task_pid_nr(current), current->comm,
				dequeue_signal_lock(current, &current->blocked, &info));
			result = -EINTR;
			//sock_shutdown(nbd, !send);
			break;
		}

		if (result && (!send) && (!strict_receive))
			break;

		if (result == 0)  {
			DEWB_DEBUG("HERE size was %d", size);
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
		DEWB_DEBUG("Limit of %u requests reached reconnecting socket", DEWB_REUSE_LIMIT);
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
		DEWB_DEBUG("Limit of %u requests reached reconnecting socket", DEWB_REUSE_LIMIT);
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


	/* Now iterate through the sglist */
	for (i = 0; i < desc->sgl_size; i++) {
		char *buff = sg_virt(&desc->sgl[i]);
		int length = desc->sgl[i].length;

		ret = sock_xmit(dbg, desc, 1, buff, length, 0);
		if (ret == -EPIPE) {
			dewb_cdmi_disconnect(dbg, desc);
			ret = dewb_cdmi_connect(dbg, desc);
			if (ret) return ret;
			goto xmit_again;
		}
		if (ret != length) {
			DEWB_ERROR("Unable to send all : %d instead of %d bytes",
				ret, length);
			return -EIO;
		}
	}
	
	ret = sock_xmit(dbg, desc, 1, "\r\n", 2, 0);
	if (ret == -EPIPE) {
		dewb_cdmi_disconnect(dbg, desc);
		ret = dewb_cdmi_connect(dbg, desc);
		if (ret) return ret;
		goto xmit_again;
	}
	if (ret != 2)
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

	if (size != flush_size)
		return -EIO;
	
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

	int ret, len;

	/* Construct a GET (?metadata) command */
	len = dewb_http_mkmetadata(buff, DEWB_XMIT_BUFFER_SIZE, 
			desc->ip_addr, desc->filename);
	if (len <= 0) return len;

	len = sock_send_receive(dbg, desc, len, 0);
	if (len < 0) return len;
	
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
		DEWB_ERROR("ERROR sending sglist: %d", len);
		return len;
	}

	if (len > 512) {/* Shall not get more than that */
		DEWB_ERROR("Incorrect response size: %d", len);
		ret = -EIO;
		goto out;
	}

	if (strncmp(desc->xmit_buff, "HTTP/1.1 204 No Content",
			strlen("HTTP/1.1 204 No Content"))) {
			DEWB_ERROR("Unable to get back HTTP confirmation buffer");
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
		DEWB_DEBUG("getrange: skipheader failed: %d", ret);
		ret = -EIO;
		goto out;
	}

	// More bytes may have to be read
	while (len < size) {
		DEWB_DEBUG("Have to read more [read=%d, toread=%d]",
			len, size - len);
		ret = sock_xmit(dbg, desc, 0, desc->xmit_buff + rcv, size - len, 0);
		if (ret < 0) {
			DEWB_ERROR("ERROR sock xmit ret = %d", ret);
			return -EIO;
		}
		len += ret;
		rcv += ret;
	}

	if (len != size) {
		DEWB_DEBUG("getrange error: len: %d size:%d", len, size);
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
