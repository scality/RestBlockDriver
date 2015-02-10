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

#ifndef __SRB_HTTP_H__
#define __SRB_HTTP_H__

#include <linux/types.h>

/*
 * Status Ranges extracted from RFC 2616
 */
enum srb_http_statusrange
{
	SRB_HTTP_STATUSRANGE_INFORMATIONAL	= 1, // 1..
	SRB_HTTP_STATUSRANGE_SUCCESS		= 2, // 2..
	SRB_HTTP_STATUSRANGE_REDIRECTION	= 3, // 3..
	SRB_HTTP_STATUSRANGE_CLIENTERROR	= 4, // 4..
	SRB_HTTP_STATUSRANGE_SERVERERROR	= 5, // 5..
	SRB_HTTP_STATUSRANGE_EXTENDED		= 0
};

/*
 * Codes extracted from RFC 2616
 */
enum srb_http_statuscode
{
	SRB_HTTP_STATUS_CONTINUE		= 100,	// "100"  ; Section 10.1.1: Continue
	SRB_HTTP_STATUS_SWITCHPROTO		= 101,	// "101"  ; Section 10.1.2: Switching Protocols
	SRB_HTTP_STATUS_OK			= 200,	// "200"  ; Section 10.2.1: OK
	SRB_HTTP_STATUS_CREATED		= 201,	// "201"  ; Section 10.2.2: Created
	SRB_HTTP_STATUS_ACCEPTED		= 202,	// "202"  ; Section 10.2.3: Accepted
	SRB_HTTP_STATUS_NONAUTH_INFO		= 203,	// "203"  ; Section 10.2.4: Non-Authoritative Information
	SRB_HTTP_STATUS_NOCONTENT		= 204,	// "204"  ; Section 10.2.5: No Content
	SRB_HTTP_STATUS_RESETCONTENT		= 205,	// "205"  ; Section 10.2.6: Reset Content
	SRB_HTTP_STATUS_PARTIAL		= 206,	// "206"  ; Section 10.2.7: Partial Content
	SRB_HTTP_STATUS_MULTIPLE_CHOICES	= 300,	// "300"  ; Section 10.3.1: Multiple Choices
	SRB_HTTP_STATUS_MOVED			= 301,	// "301"  ; Section 10.3.2: Moved Permanently
	SRB_HTTP_STATUS_FOUND			= 302,	// "302"  ; Section 10.3.3: Found
	SRB_HTTP_STATUS_SEEOTHER		= 303,	// "303"  ; Section 10.3.4: See Other
	SRB_HTTP_STATUS_NOTMODIF		= 304,	// "304"  ; Section 10.3.5: Not Modified
	SRB_HTTP_STATUS_USE_PROXY		= 305,	// "305"  ; Section 10.3.6: Use Proxy
	SRB_HTTP_STATUS_TEMP_REDIR		= 307,	// "307"  ; Section 10.3.8: Temporary Redirect
	SRB_HTTP_STATUS_BADREQ			= 400,	// "400"  ; Section 10.4.1: Bad Request
	SRB_HTTP_STATUS_UNAUTH			= 401,	// "401"  ; Section 10.4.2: Unauthorized
	SRB_HTTP_STATUS_PAYMENT_REQ		= 402,	// "402"  ; Section 10.4.3: Payment Required
	SRB_HTTP_STATUS_FORBIDDEN		= 403,	// "403"  ; Section 10.4.4: Forbidden
	SRB_HTTP_STATUS_NOT_FOUND		= 404,	// "404"  ; Section 10.4.5: Not Found
	SRB_HTTP_STATUS_NOT_ALLOWED		= 405,	// "405"  ; Section 10.4.6: Method Not Allowed
	SRB_HTTP_STATUS_NOT_ACCEPTABLE		= 406,	// "406"  ; Section 10.4.7: Not Acceptable
	SRB_HTTP_STATUS_PROXYAUTH_REQ		= 407,	// "407"  ; Section 10.4.8: Proxy Authentication Required
	SRB_HTTP_STATUS_REQTIMEOUT		= 408,	// "408"  ; Section 10.4.9: Request Time-out
	SRB_HTTP_STATUS_CONFLICT		= 409,	// "409"  ; Section 10.4.10: Conflict
	SRB_HTTP_STATUS_GONE			= 410,	// "410"  ; Section 10.4.11: Gone
	SRB_HTTP_STATUS_LENGTH_REQ		= 410,	// "411"  ; Section 10.4.12: Length Required
	SRB_HTTP_STATUS_PRECOND_FAILED		= 412,	// "412"  ; Section 10.4.13: Precondition Failed
	SRB_HTTP_STATUS_ENTITY_TOOLARGE	= 413,	// "413"  ; Section 10.4.14: Request Entity Too Large
	SRB_HTTP_STATUS_URI_TOOLARGE		= 414,	// "414"  ; Section 10.4.15: Request-URI Too Large
	SRB_HTTP_STATUS_UNSUP_MEDIA		= 415,	// "415"  ; Section 10.4.16: Unsupported Media Type
	SRB_HTTP_STATUS_BADRANGE		= 416,	// "416"  ; Section 10.4.17: Requested range not satisfiable
	SRB_HTTP_STATUS_EXPECT_FAILED		= 417,	// "417"  ; Section 10.4.18: Expectation Failed
	SRB_HTTP_STATUS_INTERNAL_ERROR		= 500,	// "500"  ; Section 10.5.1: Internal Server Error
	SRB_HTTP_STATUS_NOTIMPL		= 501,	// "501"  ; Section 10.5.2: Not Implemented
	SRB_HTTP_STATUS_BAD_GW			= 502,	// "502"  ; Section 10.5.3: Bad Gateway
	SRB_HTTP_STATUS_SERVICE_UNAVAIL	= 503,	// "503"  ; Section 10.5.4: Service Unavailable
	SRB_HTTP_STATUS_GW_TIMEOUT		= 504,	// "504"  ; Section 10.5.5: Gateway Time-out
	SRB_HTTP_STATUS_VERSION_NOTSUPP	= 505,	// "505"  ; Section 10.5.6: HTTP Version not supported
	SRB_HTTP_STATUS_EXTENSION		= 0	//	; extension-code
};

int srb_http_check_response_complete(char *buff, int len);
int srb_http_mkhead(char *buff, int len, char *host, char *page);
int srb_http_mkrange(char *cmd, char *buff, int len, char *host, char *page,
		uint64_t start, uint64_t end);

int srb_http_mkcreate(char *buff, int len, char *host, char *page);
int srb_http_mktruncate(char *buff, int len, char *host, char *page,
			unsigned long long size);
int srb_http_mkdelete(char *buff, int len, char *host, char *page);
int srb_http_header_get_uint64(char *buff, int len, char *key,
			uint64_t *value);
int srb_http_skipheader(char **buff, int *len);
int srb_http_mkmetadata(char *buff, int len, char *host, char *page);

int srb_http_get_status(char *buf, int len, enum srb_http_statuscode *code);
enum srb_http_statusrange srb_http_get_status_range(enum srb_http_statuscode status);

#endif
