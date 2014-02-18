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
#define DEV_SECTORSIZE		4096

/* Dewpoint server related constants */
#define DEWB_HTTP_HEADER_SIZE	1024
#define DEWB_URL_SIZE		64

#define DEWB_XMIT_BUFFER_SIZE	(DEWB_HTTP_HEADER_SIZE + DEV_SECTORSIZE)

//#define DEWB_DEBUG
#ifdef DEWB_DEBUG
#undef DEWB_DEBUG
#define DEWB_DEBUG(fmt, a...) \
	printk(KERN_NOTICE "dewb @%s:%d: " fmt "\n" , __func__, __LINE__, ##a)
#else
#define DEWB_DEBUG(fmt, a...) \
	do { if (0) printk(fmt, ##a); } while (0)
#endif

#define DEWB_INFO(fmt, a...) \
	printk(KERN_INFO "dewb: " fmt "\n" , ##a)

#define DEWB_ERROR(fmt, a...) \
	printk(KERN_ERR "dewb: " fmt "\n" , ##a)



/* dewb_cdmi.c */
struct dewb_cdmi_desc_s {
	uint8_t			state;
	char			ip_addr[16];
	uint16_t		port;
	char			filename[DEWB_URL_SIZE];
	char			xmit_buff[DEWB_XMIT_BUFFER_SIZE];
	struct socket		*socket;
	struct sockaddr_in      sockaddr;
};

/* dewb_cdmi.c */
int dewb_cdmi_init(struct dewb_cdmi_desc_s *desc, const char *url);
int dewb_cdmi_connect(struct dewb_cdmi_desc_s *desc);
int dewb_cdmi_disconnect(struct dewb_cdmi_desc_s *desc);
int dewb_cdmi_getsize(struct dewb_cdmi_desc_s *desc, uint64_t *size);
int dewb_cdmi_getrange(struct dewb_cdmi_desc_s *desc, char *buff,
		uint64_t offset, int size);

int dewb_cdmi_putrange(struct dewb_cdmi_desc_s *desc, char *buff,
		uint64_t offset, int size);
int dewb_cdmi_flush(struct dewb_cdmi_desc_s *desc, unsigned long size);

/* dewb_http.c */
int dewb_http_mkhead(char *buff, int len, char *host, char *page);
int dewb_http_mkrange(char *cmd, char *buff, int len, char *host, char *page, 
		uint64_t start, uint64_t end);

int dewb_http_mktruncate(char *buff, int len, char *host, char *page, unsigned long size);
int dewb_http_header_get_uint64(char *buff, int len, char *key, uint64_t *value);
int dewb_http_skipheader(char **buff, int *len);
int dewb_http_mkmetadata(char *buff, int len, char *host, char *page);

#endif
