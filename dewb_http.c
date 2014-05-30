
#include <linux/string.h>
#include <linux/errno.h>

#include "dewb.h"

#define HTTP_VER	"HTTP/1.1"
#define HTTP_OK		"HTTP/1.1 200 OK"
#define HTTP_METADATA	"?metadata"

#define HTTP_KEEPALIVE	"Connection: keep-alive" CRLF \
	                "Keep-Alive: timeout=3600 "
#define HTTP_TRUNCATE	"X-Scal-Truncate"
#define HTTP_USER_AGENT "User-Agent: dewblock_driver/1.0"

#define CRLF "\r\n"

static int add_buffer(char **buff, int *len, char *str)
{
	int len_str;

	len_str = strlen(str);
	if (len_str > (*len - 1))
		return -1;

	strcpy(*buff, str);
	*buff += len_str;
	*len -= len_str;
	return 0;
}

int dewb_http_get_status(char *buf, int len, enum dewb_http_statuscode *code)
{
	int ret;
	long status;
	char codebuf[8];
	int codelen = 0;

	if (!strncmp(buf, HTTP_VER, strlen(HTTP_VER)))
	{
		buf = buf + strlen(HTTP_VER);
		while (*buf != 0 && *buf == ' ')
			++buf;

		while (codelen < sizeof(codebuf) - 1 && buf[codelen] != 0
		       && buf[codelen] >= '0' && buf[codelen] <= '9')
		{
			codebuf[codelen] = buf[codelen];
			codelen++;
		}
		codebuf[codelen] = 0;

		ret = kstrtol(codebuf, 10, &status);
		if (ret != 0)
		{
			DEWB_ERROR("Could not retrieve HTTP status code: err %i (buf=%.*s)", ret, 5, buf);
			return -1;
		}

		// Known codes have their values fixed to the enum, so keep them
		// Otherwise, consider it as an unknown extension.
		switch (status)
		{
		case 100: case 101:
		case 200: case 201: case 202: case 203: case 204: case 205: case 206:
		case 300: case 301: case 302: case 303: case 304: case 305: case 307:
		case 400: case 401: case 402: case 403: case 404: case 405: case 40 : case 407: case 408: case 409:
		case 410: case 411: case 412: case 413: case 414: case 415: case 416: case 417:
		case 500: case 501: case 502: case 503: case 504: case 505:
			*code = status;
			break ;
		default:
			*code = DEWB_HTTP_STATUS_EXTENSION;
			break ;
		}

		return 0;
	}

	return -1;
}


enum dewb_http_statusrange dewb_http_get_status_range(enum dewb_http_statuscode status)
{
	enum dewb_http_statusrange range = DEWB_HTTP_STATUSRANGE_EXTENDED;

	if (status >= 100 && status < 200)
		range = DEWB_HTTP_STATUSRANGE_INFORMATIONAL;
	else if (status >= 200 && status < 300)
		range = DEWB_HTTP_STATUSRANGE_SUCCESS;
	else if (status >= 300 && status < 400)
		range = DEWB_HTTP_STATUSRANGE_REDIRECTION;
	else if (status >= 400 && status < 500)
		range = DEWB_HTTP_STATUSRANGE_CLIENTERROR;
	else if (status >= 500 && status < 600)
		range = DEWB_HTTP_STATUSRANGE_SERVERERROR;

	return range;
}

#if 0
// Add a \0 at the end of the buffer
static int finish_buffer(char **buff, int *len)
{

	if (*len == 0)
		return -1;

	**buff = 0;
	(*len)--;

	return 0;
}
#endif

static int frame_status_ok(char *buff, int len)
{
	if (!strncmp(buff, HTTP_OK, strlen(HTTP_OK)))
		return 1;
	return 0;
}

int dewb_http_mkhead(char *buff, int len, char *host, char *page)
{
	char *bufp = buff;
	int mylen = len;
	int ret;

	*buff = 0;
	ret = add_buffer(&bufp, &mylen, "HEAD ");
	if (ret)
		return -ENOMEM;

	ret = add_buffer(&bufp, &mylen, page);
	if (ret)
		return -ENOMEM;

	ret = add_buffer(&bufp, &mylen, " " HTTP_VER CRLF);
	if (ret)
		return -ENOMEM;

	ret = add_buffer(&bufp, &mylen, HTTP_KEEPALIVE CRLF);
	if (ret)
		return -ENOMEM;

	ret = add_buffer(&bufp, &mylen, HTTP_USER_AGENT CRLF);
	if (ret)
		return -ENOMEM;
	
	ret = add_buffer(&bufp, &mylen, "Host: ");
	if (ret)
		return -ENOMEM;

	ret = add_buffer(&bufp, &mylen, host);
	if (ret)
		return -ENOMEM;

	ret = add_buffer(&bufp, &mylen, CRLF CRLF);
	if (ret)
		return -ENOMEM;

	return (len - mylen);
}

int dewb_http_mktruncate(char *buff, int len, char *host, char *page, unsigned long size)
{
	char *bufp = buff;
	int mylen = len;
	char buf[64];
	int ret;

	*buff = 0;
	ret = add_buffer(&bufp, &mylen, "PUT ");
	if (ret)
		return -ENOMEM;

	ret = add_buffer(&bufp, &mylen, page);
	if (ret)
		return -ENOMEM;

	ret = add_buffer(&bufp, &mylen, " " HTTP_VER CRLF);
	if (ret)
		return -ENOMEM;

	ret = add_buffer(&bufp, &mylen, HTTP_KEEPALIVE CRLF);
	if (ret)
		return -ENOMEM;

	ret = add_buffer(&bufp, &mylen, HTTP_USER_AGENT CRLF);
	if (ret)
		return -ENOMEM;
	
	ret = add_buffer(&bufp, &mylen, "Host: ");
	if (ret)
		return -ENOMEM;

	ret = add_buffer(&bufp, &mylen, host);
	if (ret)
		return -ENOMEM;

	ret = add_buffer(&bufp, &mylen, CRLF);
	if (ret)
		return -ENOMEM;

	sprintf(buf, "%s: %lu", HTTP_TRUNCATE, size);

	ret = add_buffer(&bufp, &mylen, buf);
	if (ret)
		return -ENOMEM;

	ret = add_buffer(&bufp, &mylen, CRLF CRLF);
	if (ret)
		return -ENOMEM;

	return (len - mylen);
}

int dewb_http_mkmetadata(char *buff, int len, char *host, char *page)
{
	char *bufp = buff;
	int mylen = len;
	int ret;

	*buff = 0;
	ret = add_buffer(&bufp, &mylen, "GET ");
	if (ret)
		return -ENOMEM;

	ret = add_buffer(&bufp, &mylen, page);
	if (ret)
		return -ENOMEM;

	ret = add_buffer(&bufp, &mylen, HTTP_METADATA " " HTTP_VER CRLF);
	if (ret)
		return -ENOMEM;

	ret = add_buffer(&bufp, &mylen, HTTP_KEEPALIVE CRLF);
	if (ret)
		return -ENOMEM;

	ret = add_buffer(&bufp, &mylen, HTTP_USER_AGENT CRLF);
	if (ret)
		return -ENOMEM;
	
	ret = add_buffer(&bufp, &mylen, "Host: ");
	if (ret)
		return -ENOMEM;

	ret = add_buffer(&bufp, &mylen, host);
	if (ret)
		return -ENOMEM;

	ret = add_buffer(&bufp, &mylen, CRLF CRLF);
	if (ret)
		return -ENOMEM;

	return (len - mylen);
}

int dewb_http_mkrange(char *cmd, char *buff, int len, char *host, char *page, 
		uint64_t start, uint64_t end)
{
	char *bufp = buff;
	char range_str[64];
	int mylen = len;
	int ret;

	*buff = 0;
	ret = add_buffer(&bufp, &mylen, cmd);
	if (ret)
		return -ENOMEM;

	ret = add_buffer(&bufp, &mylen, " ");
	if (ret)
		return -ENOMEM;

	ret = add_buffer(&bufp, &mylen, page);
	if (ret)
		return -ENOMEM;

	ret = add_buffer(&bufp, &mylen, " " HTTP_VER CRLF);
	if (ret)
		return -ENOMEM;

	ret = add_buffer(&bufp, &mylen, HTTP_KEEPALIVE CRLF);
	if (ret)
		return -ENOMEM;

	ret = add_buffer(&bufp, &mylen, HTTP_USER_AGENT CRLF);
	if (ret)
		return -ENOMEM;
	
	ret = add_buffer(&bufp, &mylen, "Host: ");
	if (ret)
		return -ENOMEM;

	ret = add_buffer(&bufp, &mylen, host);
	if (ret)
		return -ENOMEM;

	ret = add_buffer(&bufp, &mylen,  CRLF);
	if (ret)
		return -ENOMEM;

	ret = add_buffer(&bufp, &mylen, "Range: bytes=");
	if (ret)
		return -ENOMEM;

	/* Construct range info */
	sprintf(range_str, "%lu-%lu" CRLF, 
		(unsigned long )start, (unsigned long )end);

	ret = add_buffer(&bufp, &mylen, range_str);
	if (ret)
		return -ENOMEM;

	if (!strcmp(cmd, "PUT")) {
		ret = add_buffer(&bufp, &mylen, "Content-Length: ");
		if (ret)
			return -ENOMEM;

		sprintf(range_str, "%lu" CRLF CRLF, 
			(unsigned long)(end - start + 1UL));

		ret = add_buffer(&bufp, &mylen, range_str);
		if (ret)
			return -ENOMEM;


	} else {
		ret = add_buffer(&bufp, &mylen, CRLF);
		if (ret)
			return -ENOMEM;
	}

	return (len - mylen);	
}

int dewb_http_header_get_uint64(char *buff, int len, char *key, uint64_t *value)
{
	int ret;
	char *pos;

	if (!frame_status_ok(buff, len))
		return -EIO;

	pos = strstr(buff, key);
	if (!pos)
		return -EIO;

	/* Skip the key and the ' :' */
	pos += strlen(key) + 2;
	ret = sscanf(pos, "%lu", (unsigned long *)value);
	if (ret != 1)
		return -EIO;
	return 0;
}

int dewb_http_skipheader(char **buff, int *len)
{
	int count = 0;

	while (*len)
	{
		if ((**buff == '\r') || (**buff == '\n'))
			count++;
		else
			count = 0;
			
		(*len)--;
		(*buff)++;

		if (count == 4)			
			return 0;
	}

	return -1;
}
