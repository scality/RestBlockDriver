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
 * along with ScalityRestBlock.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <srb/srb-http.h>
#include <srb/srb-log.h>
#include "srb.h"

#define HTTP_VER	"HTTP/1.1"
#define HTTP_OK		"HTTP/1.1 200 OK"
#define HTTP_METADATA	"?metadata"

#define HTTP_KEEPALIVE	"Connection: keep-alive" CRLF \
	                "Keep-Alive: timeout=3600 "
#define HTTP_TRUNCATE	"X-Scal-Truncate"
#define HTTP_USER_AGENT	"User-Agent: srb/" DEV_REL_VERSION
#define HTTP_CDMI_VERS	"X-CDMI-Specification-Version: 1.0.1"

#define CR '\r'
#define LF '\n'
#define CRLF "\r\n"


static int add_buffer(char **buff, int *len, char *str)
{
	int len_str;

	len_str = strlen(str);
	if (len_str > (*len - 1))
		return -1;

	strncpy(*buff, str, len_str);
	*buff += len_str;
	*len -= len_str;

	return 0;
}

int srb_http_get_status(char *buf, int len, enum srb_http_statuscode *code)
{
	int ret;
	long status;
	char codebuf[8];
	int codelen = 0;
	char savechar = 0;

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
		// Save and set \0 for kstrtol to succeed.
		savechar = codebuf[codelen];
		codebuf[codelen] = 0;

		ret = kstrtol(codebuf, 10, &status);
		// Restore char
		codebuf[codelen] = savechar;
		if (ret != 0)
		{
			SRB_LOG_ERR(srb_log, "Could not retrieve HTTP status code: err %i (buf=%.*s)", ret, 5, buf);
			return -1;
		}

		// Known codes have their values fixed to the enum, so keep them
		// Otherwise, consider it as an unknown extension.
		switch (status)
		{
		case 100: case 101:
		case 200: case 201: case 202: case 203: case 204: case 205: case 206:
		case 300: case 301: case 302: case 303: case 304: case 305: case 307:
		case 400: case 401: case 402: case 403: case 404: case 405: case 406: case 407: case 408: case 409:
		case 410: case 411: case 412: case 413: case 414: case 415: case 416: case 417:
		case 500: case 501: case 502: case 503: case 504: case 505:
			*code = status;
			break ;
		default:
			*code = SRB_HTTP_STATUS_EXTENSION;
			break ;
		}

		return 0;
	}

	return -1;
}
EXPORT_SYMBOL(srb_http_get_status);


enum srb_http_statusrange srb_http_get_status_range(enum srb_http_statuscode status)
{
	enum srb_http_statusrange range = SRB_HTTP_STATUSRANGE_EXTENDED;

	if (status >= 100 && status < 200)
		range = SRB_HTTP_STATUSRANGE_INFORMATIONAL;
	else if (status >= 200 && status < 300)
		range = SRB_HTTP_STATUSRANGE_SUCCESS;
	else if (status >= 300 && status < 400)
		range = SRB_HTTP_STATUSRANGE_REDIRECTION;
	else if (status >= 400 && status < 500)
		range = SRB_HTTP_STATUSRANGE_CLIENTERROR;
	else if (status >= 500 && status < 600)
		range = SRB_HTTP_STATUSRANGE_SERVERERROR;

	return range;
}
EXPORT_SYMBOL(srb_http_get_status_range);

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

int srb_http_check_response_complete(char *buff, int len)
{
	int hdr_end = 0;
	int ret = 1;
	uint64_t contentlen = 0;

	while (hdr_end < len && ret != 0)
	{
		if (len - hdr_end >= 4)
			ret = strncmp(buff+hdr_end, CRLF CRLF, 4);

		if (ret != 0)
		{
			hdr_end += 1;
			while (hdr_end < len && buff[hdr_end] != CR)
				hdr_end += 1;
		}
	}
	
	if (hdr_end == len)
		return 0;

	// Go over CRLFCRLF
	hdr_end += 4;

	// NOTE: Ignore return status, we actually only need contentlen in case of
	// success, the default value of 0 being as useful to us as the proper
	// value.
	(void)srb_http_header_get_uint64(buff, len,
	                                  "Content-Length", &contentlen);

	return hdr_end + contentlen <= len;
}
EXPORT_SYMBOL(srb_http_check_response_complete);

int srb_http_mkmetadata(char *buff, int len, char *host, char *page)
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
EXPORT_SYMBOL(srb_http_mkmetadata);

int srb_http_mkrange(char *cmd, char *buff, int len, char *host, char *page,
		uint64_t start, uint64_t end)
{
	char *bufp = buff;
	char range_str[64];
	int mylen = len;
	int ret = 0;

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
	sprintf(range_str, "%lu-%lu",
		(unsigned long) start, (unsigned long) end);

	ret = add_buffer(&bufp, &mylen, range_str);
	if (ret)
		return -ENOMEM;

	if (!strncmp("PUT", cmd, 3)) {
		sprintf(range_str, CRLF "Content-Length: %lu",
			(unsigned long)(end - start + 1UL));

		ret = add_buffer(&bufp, &mylen, range_str);
		if (ret)
			return -ENOMEM;
	}

	ret = add_buffer(&bufp, &mylen, CRLF CRLF);
	if (ret)
		return -ENOMEM;

	return (len - mylen);
}
EXPORT_SYMBOL(srb_http_mkrange);

int srb_http_header_get_uint64(char *buff, int len, char *key, uint64_t *value)
{
	int ret;
	int ipos=0;
	int keylen;
	int span;
	int endpos = -1;
	char endchar = 0;

	keylen = strlen(key);
	while (ipos < len && strncasecmp(&buff[ipos], key, keylen) != 0) {
		++ipos;
	}
	if (ipos == len)
		return -EIO;

	/* Skip the key and the ': ' */
	span = 0;
	while (ipos+keylen+span < len && buff[ipos+keylen+span] != ':')
		++span;
	if (ipos+keylen+span == len)
		return -EIO;
	++span;
	while (ipos+keylen+span < len && buff[ipos+keylen+span] == ' ')
		++span;
	if (ipos+keylen+span == len)
		return -EIO;

	/*
	 * kstrtou64 seem to return -EINVAL whenever the digits of the number
	 * are followed by anything other than \n and \0. Because of this, we
	 * must do a save/restore on the following byte.
	 */
	endpos = ipos + keylen + span;
	while (endpos < len && buff[endpos] >= '0' && buff[endpos] <= '9')
		endpos++;
	if (endpos < len)
	{
		endchar = buff[endpos];
		buff[endpos] = 0;
	}

	/* Now, retrieve the value */
	ret = kstrtou64(&buff[ipos+keylen+span], 10, value);

	/* Now, restore the endbyte if need be */
	if (endpos < len)
		buff[endpos] = endchar;

	if (ret != 0)
		return -EIO;

	return 0;
}
EXPORT_SYMBOL(srb_http_header_get_uint64);

int srb_http_skipheader(char **buff, int *len)
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
EXPORT_SYMBOL(srb_http_skipheader);
