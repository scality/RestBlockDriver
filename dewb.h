#ifndef __DEWBLOCK_H__
# define  __DEWBLOCK_H__

#include <linux/inet.h>
#include <linux/socket.h>
#include <linux/net.h>	     // for in_aton()
#include <linux/in.h>
#include <linux/net.h>
#include <linux/scatterlist.h>
#include <net/sock.h>

/* Constants */

#define MB			(1024 * 1024)
#define GB			(1024 * MB)

/* Unix device constants */
#define DEV_NAME		"dewb"
#define DEV_MINORS		256
#define DEV_DEFAULT_DISKSIZE	(50 * MB)
#define DEV_MAX			16
#define DEV_SECTORSIZE		1 * MB
#define DEV_NB_PHYS_SEGS	512

/* Dewpoint server related constants */
#define DEWB_HTTP_HEADER_SIZE	1024
#define DEWB_URL_SIZE		64
#define DEWB_REUSE_LIMIT	500 /* Max number of requests sent to
				     * a single HTTP connection before
				     * restarting a new one.
				     */

#define DEWB_DEBUG_LEVEL	0   /* We do not want to be polluted
				     * by default */


#define DEWB_XMIT_BUFFER_SIZE	(DEWB_HTTP_HEADER_SIZE + DEV_SECTORSIZE)

#define DEWB_THREAD_POOL_SIZE	8

#define DEWB_INTERNAL_DBG(dbg, fmt, a...) \
	do { if ((dbg)->level)					\
		printk(KERN_NOTICE "%s @%s:%d: " fmt "\n" ,	\
			(dbg)->name, __func__, __LINE__, ##a);	\
	} while (0)

#define DEWB_DEBUG(fmt, a...) DEWB_INTERNAL_DBG(dbg, fmt, ##a)

#define DEWB_DEV_DEBUG(fmt, a...) DEWB_INTERNAL_DBG(&dev->debug, fmt, ##a)


#define DEWB_INFO(fmt, a...) \
	printk(KERN_INFO "dewb: " fmt "\n" , ##a)

#define DEWB_ERROR(fmt, a...) \
	printk(KERN_ERR "dewb: " fmt "\n" , ##a)

#define DEWB_MIN(x, y) ((x) < (y) ? (x) : (y))
#define DEWB_N_JSON_TOKENS	128

/*
 * Status Ranges extracted from RFC 2616
 */
enum dewb_http_statusrange
{
	DEWB_HTTP_STATUSRANGE_INFORMATIONAL	= 1, // 1..
	DEWB_HTTP_STATUSRANGE_SUCCESS		= 2, // 2..
	DEWB_HTTP_STATUSRANGE_REDIRECTION	= 3, // 3..
	DEWB_HTTP_STATUSRANGE_CLIENTERROR	= 4, // 4..
	DEWB_HTTP_STATUSRANGE_SERVERERROR	= 5, // 5..
	DEWB_HTTP_STATUSRANGE_EXTENDED		= 0
};

/*
 * Codes extracted from RFC 2616
 */
enum dewb_http_statuscode
{
	DEWB_HTTP_STATUS_CONTINUE		= 100,	// "100"  ; Section 10.1.1: Continue
	DEWB_HTTP_STATUS_SWITCHPROTO		= 101,	// "101"  ; Section 10.1.2: Switching Protocols
	DEWB_HTTP_STATUS_OK			= 200,	// "200"  ; Section 10.2.1: OK
	DEWB_HTTP_STATUS_CREATED		= 201,	// "201"  ; Section 10.2.2: Created
	DEWB_HTTP_STATUS_ACCEPTED		= 202,	// "202"  ; Section 10.2.3: Accepted
	DEWB_HTTP_STATUS_NONAUTH_INFO		= 203,	// "203"  ; Section 10.2.4: Non-Authoritative Information
	DEWB_HTTP_STATUS_NOCONTENT		= 204,	// "204"  ; Section 10.2.5: No Content
	DEWB_HTTP_STATUS_RESETCONTENT		= 205,	// "205"  ; Section 10.2.6: Reset Content
	DEWB_HTTP_STATUS_PARTIAL		= 206,	// "206"  ; Section 10.2.7: Partial Content
	DEWB_HTTP_STATUS_MULTIPLE_CHOICES	= 300,	// "300"  ; Section 10.3.1: Multiple Choices
	DEWB_HTTP_STATUS_MOVED			= 301,	// "301"  ; Section 10.3.2: Moved Permanently
	DEWB_HTTP_STATUS_FOUND			= 302,	// "302"  ; Section 10.3.3: Found
	DEWB_HTTP_STATUS_SEEOTHER		= 303,	// "303"  ; Section 10.3.4: See Other
	DEWB_HTTP_STATUS_NOTMODIF		= 304,	// "304"  ; Section 10.3.5: Not Modified
	DEWB_HTTP_STATUS_USE_PROXY		= 305,	// "305"  ; Section 10.3.6: Use Proxy
	DEWB_HTTP_STATUS_TEMP_REDIR		= 307,	// "307"  ; Section 10.3.8: Temporary Redirect
	DEWB_HTTP_STATUS_BADREQ			= 400,	// "400"  ; Section 10.4.1: Bad Request
	DEWB_HTTP_STATUS_UNAUTH			= 401,	// "401"  ; Section 10.4.2: Unauthorized
	DEWB_HTTP_STATUS_PAYMENT_REQ		= 402,	// "402"  ; Section 10.4.3: Payment Required
	DEWB_HTTP_STATUS_FORBIDDEN		= 403,	// "403"  ; Section 10.4.4: Forbidden
	DEWB_HTTP_STATUS_NOT_FOUND		= 404,	// "404"  ; Section 10.4.5: Not Found
	DEWB_HTTP_STATUS_NOT_ALLOWED		= 405,	// "405"  ; Section 10.4.6: Method Not Allowed
	DEWB_HTTP_STATUS_NOT_ACCEPTABLE		= 406,	// "406"  ; Section 10.4.7: Not Acceptable
	DEWB_HTTP_STATUS_PROXYAUTH_REQ		= 407,	// "407"  ; Section 10.4.8: Proxy Authentication Required
	DEWB_HTTP_STATUS_REQTIMEOUT		= 408,	// "408"  ; Section 10.4.9: Request Time-out
	DEWB_HTTP_STATUS_CONFLICT		= 409,	// "409"  ; Section 10.4.10: Conflict
	DEWB_HTTP_STATUS_GONE			= 410,	// "410"  ; Section 10.4.11: Gone
	DEWB_HTTP_STATUS_LENGTH_REQ		= 410,	// "411"  ; Section 10.4.12: Length Required
	DEWB_HTTP_STATUS_PRECOND_FAILED		= 412,	// "412"  ; Section 10.4.13: Precondition Failed
	DEWB_HTTP_STATUS_ENTITY_TOOLARGE	= 413,	// "413"  ; Section 10.4.14: Request Entity Too Large
	DEWB_HTTP_STATUS_URI_TOOLARGE		= 414,	// "414"  ; Section 10.4.15: Request-URI Too Large
	DEWB_HTTP_STATUS_UNSUP_MEDIA		= 415,	// "415"  ; Section 10.4.16: Unsupported Media Type
	DEWB_HTTP_STATUS_BADRANGE		= 416,	// "416"  ; Section 10.4.17: Requested range not satisfiable
	DEWB_HTTP_STATUS_EXPECT_FAILED		= 417,	// "417"  ; Section 10.4.18: Expectation Failed
	DEWB_HTTP_STATUS_INTERNAL_ERROR		= 500,	// "500"  ; Section 10.5.1: Internal Server Error
	DEWB_HTTP_STATUS_NOTIMPL		= 501,	// "501"  ; Section 10.5.2: Not Implemented
	DEWB_HTTP_STATUS_BAD_GW			= 502,	// "502"  ; Section 10.5.3: Bad Gateway
	DEWB_HTTP_STATUS_SERVICE_UNAVAIL	= 503,	// "503"  ; Section 10.5.4: Service Unavailable
	DEWB_HTTP_STATUS_GW_TIMEOUT		= 504,	// "504"  ; Section 10.5.5: Gateway Time-out
	DEWB_HTTP_STATUS_VERSION_NOTSUPP	= 505,	// "505"  ; Section 10.5.6: HTTP Version not supported
	DEWB_HTTP_STATUS_EXTENSION		= 0	//	; extension-code
};

/* dewb_cdmi.c */
struct dewb_cdmi_desc_s {
	/* For /sys/block/dewb?/dewb_url */
	char			url[DEWB_URL_SIZE + 1];
	uint8_t			state;
	char			ip_addr[16];
	uint16_t		port;
	char			filename[DEWB_URL_SIZE + 1];
	char			xmit_buff[DEWB_XMIT_BUFFER_SIZE];
	uint64_t		nb_requests; /* Number of HTTP
					      * requests already sent
					      * through this socket */ 
	struct scatterlist	sgl[DEV_NB_PHYS_SEGS];
	int			sgl_size;
	struct socket		*socket;
	struct sockaddr_in	sockaddr;
};

/* dewb device definition */
typedef struct dewb_debug_s {
	const char		*name;
	int			level;
} dewb_debug_t;

typedef struct dewb_device_s {

	/* Device subsystem related data */
	int			id;		/* device ID */
	int			major;		/* blkdev assigned major */
	char			name[32];	/* blkdev name, e.g. dewba */
	struct gendisk		*disk;
	uint64_t		disk_size;	/* Size in bytes */
	int			users;		/* Number of users who
						 * opened dev*/

	struct request_queue	*q;
	spinlock_t		rq_lock;	/* request queue lock */

	struct task_struct	*thread[DEWB_THREAD_POOL_SIZE];
	int			nb_threads;

	/* Dewpoint specific data */
	struct dewb_cdmi_desc_s	thread_cdmi_desc[DEWB_THREAD_POOL_SIZE];

	/* 
	** List of requests received by the drivers, but still to be
	** processed. This due to network latency.
	*/
	spinlock_t		waiting_lock;	/* request queue lock */
	wait_queue_head_t	waiting_wq;
	struct list_head	waiting_queue; /* Requests to be sent */

	/* Debug traces */
	dewb_debug_t		debug;
} dewb_device_t;

typedef struct dewb_mirror_s {
	struct dewb_mirror_s   	*next;
	struct dewb_cdmi_desc_s	cdmi_desc;
} dewb_mirror_t;

/* dewb.c */
int dewb_device_create(const char *filename, unsigned long long size);
int dewb_device_extend(const char *filename, unsigned long long size);
int dewb_device_destroy(const char *filename);

int dewb_device_attach(const char *filename);
int dewb_device_detach_by_name(const char *filename);
int dewb_device_detach_by_id(int dev_id);

int dewb_mirror_add(const char *url);
int dewb_mirror_remove(const char *url);
ssize_t dewb_mirrors_dump(char *buf, ssize_t max_size);

/* dewb_sysfs.c*/
int dewb_sysfs_init(void);
void dewb_sysfs_device_init(dewb_device_t *dev);
void dewb_sysfs_cleanup(void);

/* dewb_cdmi.c */
int dewb_cdmi_init(dewb_debug_t *dbg, struct dewb_cdmi_desc_s *desc,
		const char *url);
int dewb_cdmi_connect(dewb_debug_t *dbg, struct dewb_cdmi_desc_s *desc);
int dewb_cdmi_disconnect(dewb_debug_t *dbg, struct dewb_cdmi_desc_s *desc);

int dewb_cdmi_getsize(dewb_debug_t *dbg, struct dewb_cdmi_desc_s *desc,
		uint64_t *size);

int dewb_cdmi_getrange(dewb_debug_t *dbg, struct dewb_cdmi_desc_s *desc,
		uint64_t offset, int size);

int dewb_cdmi_putrange(dewb_debug_t *dbg, struct dewb_cdmi_desc_s *desc,
		uint64_t offset, int size);

int dewb_cdmi_flush(dewb_debug_t *dbg, struct dewb_cdmi_desc_s *desc,
		unsigned long flush_size);

int dewb_cdmi_truncate(dewb_debug_t *dbg, struct dewb_cdmi_desc_s *desc,
		unsigned long trunc_size);

int dewb_cdmi_create(dewb_debug_t *dbg, struct dewb_cdmi_desc_s *desc,
		     unsigned long long trunc_size);
int dewb_cdmi_extend(dewb_debug_t *dbg, struct dewb_cdmi_desc_s *desc,
		     unsigned long long trunc_size);
int dewb_cdmi_delete(dewb_debug_t *dbg, struct dewb_cdmi_desc_s *desc);

int dewb_cdmi_list(dewb_debug_t *dbg, struct dewb_cdmi_desc_s *desc,
		   int (*volume_cb)(const char *));

/* dewb_http.c */
int dewb_http_mklist(char *buff, int len, char *host, char *page);
int dewb_http_mkhead(char *buff, int len, char *host, char *page);
int dewb_http_mkrange(char *cmd, char *buff, int len, char *host, char *page, 
		uint64_t start, uint64_t end);

int dewb_http_mkcreate(char *buff, int len, char *host, char *page);
int dewb_http_mktruncate(char *buff, int len, char *host, char *page,
			unsigned long long size);
int dewb_http_mkdelete(char *buff, int len, char *host, char *page);
int dewb_http_header_get_uint64(char *buff, int len, char *key,
			uint64_t *value);
int dewb_http_skipheader(char **buff, int *len);
int dewb_http_mkmetadata(char *buff, int len, char *host, char *page);

int dewb_http_get_status(char *buf, int len, enum dewb_http_statuscode *code);
enum dewb_http_statusrange dewb_http_get_status_range(enum dewb_http_statuscode status);

#endif
