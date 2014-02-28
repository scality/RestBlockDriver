#ifndef __DEWBLOCK_H__
# define  __DEWBLOCK_H__

#include <linux/inet.h>
#include <linux/socket.h>
#include <linux/net.h>	     // for in_aton()
#include <linux/in.h>
#include <linux/net.h>

#include <net/sock.h>
/* Constants */

#define MB			(1024 * 1024)
#define GB			(1024 * MB)

/* Unix device constants */
#define DEV_NAME		"dewb"
#define DEV_MINORS		256
#define DEV_DEFAULT_DISKSIZE	(50 * MB)
#define DEV_MAX			16
#define DEV_SECTORSIZE		8192

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

#define DEWB_DEBUG(fmt, a...) \
	do { if (dev->debug)  \
              printk(KERN_NOTICE "%s @%s:%d: " fmt "\n" ,       \
		      dev->name, __func__, __LINE__, ##a);	\
        } while (0)


#define DEWB_INFO(fmt, a...) \
	printk(KERN_INFO "dewb: " fmt "\n" , ##a)

#define DEWB_ERROR(fmt, a...) \
	printk(KERN_ERR "dewb: " fmt "\n" , ##a)

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
	struct socket		*socket;
	struct sockaddr_in      sockaddr;
};

/* dewb device definition */
typedef struct dewb_device_s {

	/* Device subsystem related data */
	int			id;		/* device ID */
	int			major;		/* blkdev assigned major */
	char			name[32];	/* blkdev name, e.g. dewba */
	struct gendisk		*disk;
	uint64_t		disk_size;	/* Size in bytes */

	struct request_queue	*q;
	spinlock_t		rq_lock;	/* request queue lock */

	struct task_struct	*thread;
	/* 
	** List of requests received by the drivers, but still to be
	** processed. This due to network latency.
	*/
	spinlock_t		waiting_lock;	/* request queue lock */
	wait_queue_head_t	waiting_wq;
	struct list_head	waiting_queue; /* Requests to be sent */

	/* Debug traces */
	int			debug;
	
	/* Dewpoint specific data */
	struct dewb_cdmi_desc_s cdmi_desc;
	
} dewb_device_t;

/* dewb.c */
int dewb_device_add(char *url);
int dewb_device_remove(dewb_device_t *dev);
int dewb_device_remove_by_id(int dev_id);

/* dewb_sysfs.c*/
int dewb_sysfs_init(void);
void dewb_sysfs_device_init(dewb_device_t *dev);
void dewb_sysfs_cleanup(void);

/* dewb_cdmi.c */
int dewb_cdmi_init(dewb_device_t *dev, const char *url);
int dewb_cdmi_connect(dewb_device_t *dev);
int dewb_cdmi_disconnect(dewb_device_t *dev);
int dewb_cdmi_getsize(dewb_device_t *dev, uint64_t *size);
int dewb_cdmi_getrange(dewb_device_t *dev, char *buff,
		uint64_t offset, int size);

int dewb_cdmi_putrange(dewb_device_t *dev, char *buff,
		uint64_t offset, int size);
int dewb_cdmi_flush(dewb_device_t *dev, unsigned long size);

/* dewb_http.c */
int dewb_http_mkhead(char *buff, int len, char *host, char *page);
int dewb_http_mkrange(char *cmd, char *buff, int len, char *host, char *page, 
		uint64_t start, uint64_t end);

int dewb_http_mktruncate(char *buff, int len, char *host, char *page, unsigned long size);
int dewb_http_header_get_uint64(char *buff, int len, char *key, uint64_t *value);
int dewb_http_skipheader(char **buff, int *len);
int dewb_http_mkmetadata(char *buff, int len, char *host, char *page);

#endif
