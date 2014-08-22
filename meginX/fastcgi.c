/* 
 * File:   fastcgi.c
 * Author: Beck Xu <beck917@gmail.com>
 *
 * Created on 2014年6月25日, 下午3:57
 */

#include <stdio.h>
#include <stdlib.h>
#include "server.h"

struct fcgiParams {
	char *name;
	char *value;
} fcgi_params[] = {
	"GATEWAY_INTERFACE", "FastCGI/1.0",
	"REQUEST_METHOD"   , "POST",
        "QUERY_STRING"     , "input={\"t\":\"test\"}",
	"SCRIPT_NAME"      , "/index.php",
	"REQUEST_URI"      , "/index.php",
        "DOCUMENT_ROOT"    , "/var/www/origin/www",
        "DOCUMENT_URI"     , "/index.php",
	"SERVER_SOFTWARE"  , "pillX/0.1",
	"REMOTE_ADDR"      , "127.0.0.1",
	"REMOTE_PORT"      , "6389",
	"SERVER_ADDR"      , "127.0.0.1",
	"SERVER_PORT"      , "80",
	"SERVER_NAME"      , "localhost",
	"SERVER_PROTOCOL"  , "HTTP/1.1",
        "CONTENT_TYPE"     , "application/x-www-form-urlencoded", 
};

typedef struct {
	buffer  *b;
	size_t   len;
	int      type;
	int      padding;
	size_t   request_id;
} fastcgi_response_packet;

static int fastcgi_get_packet(fastcgiResponse *fr, fastcgi_response_packet *packet) {
	size_t offset;
	size_t toread;
	FCGI_Header *header;

	packet->b = buffer_init();
	packet->len = 0;
	packet->type = 0;
	packet->padding = 0;
	packet->request_id = 0;

	toread = 8;
        buffer_append_string_len(packet->b, fr->buf->ptr + fr->offset, toread);

	if ((packet->b->used == 0) ||
	    (packet->b->used - 1 < sizeof(FCGI_Header))) {
		/* no header */
		buffer_free(packet->b);

		return -1;
	}

	/* we have at least a header, now check how much me have to fetch */
	header = (FCGI_Header *)(packet->b->ptr);

	packet->len = (header->contentLengthB0 | (header->contentLengthB1 << 8)) + header->paddingLength;
	packet->request_id = (header->requestIdB0 | (header->requestIdB1 << 8));
	packet->type = header->type;
	packet->padding = header->paddingLength; 

	/* ->b should only be the content */
	buffer_copy_string_len(packet->b, CONST_STR_LEN("")); /* used == 1 */

	if (packet->len) {
		/* copy the content */
		buffer_append_string_len(packet->b, fr->buf->ptr + fr->offset + toread, packet->len);

		if (packet->b->used < packet->len + 1) {
			/* we didn't get the full packet */

			buffer_free(packet->b);
			return -1;
		}

		packet->b->used -= packet->padding;
		packet->b->ptr[packet->b->used - 1] = '\0';
	}

	/* tag the chunks as read */
	toread = packet->len + sizeof(FCGI_Header);
	fr->offset += toread;

	return 0;
}

int fcgi_demux_response(fastcgiResponse *fr) {
    int fin = 0;
    while (fin == 0) {
            fastcgi_response_packet packet;

            /* check if we have at least one packet */
            if (0 != fastcgi_get_packet(fr, &packet)) {
                    /* no full packet */
                    break;
            }

            switch(packet.type) {
                case FCGI_STDOUT:
                        if (packet.len == 0) break;
                        char *c;
                        size_t blen;
                        if (NULL != (c = buffer_search_string_len(packet.b, CONST_STR_LEN("\r\n\r\n")))) {
                                blen = packet.b->used - (c - packet.b->ptr) - 4;
                                //packet.b->used = (c - packet.b->ptr) + 3;
                                c += 4; /* point the the start of the response */
                        }
                        buffer_append_string_len(fr->format_buf, c, blen);
                        break;
                case FCGI_STDERR:
                        if (packet.len == 0) break;

                        break;
                case FCGI_END_REQUEST:

                        fin = 1;
                        break;
                default:
                        break;
            }
            buffer_free(packet.b);
    }

    return fin;
}

static int fcgi_header(FCGI_Header * header, unsigned char type, size_t request_id, int contentLength, unsigned char paddingLength) {
    header->version = FCGI_VERSION_1;
    header->type = type;
    header->requestIdB0 = request_id & 0xff;
    header->requestIdB1 = (request_id >> 8) & 0xff;
    header->contentLengthB0 = contentLength & 0xff;
    header->contentLengthB1 = (contentLength >> 8) & 0xff;
    header->paddingLength = paddingLength;
    header->reserved = 0;

    return 0;
}

static int fcgi_env_add(buffer *env, const char *key, size_t key_len, const char *val, size_t val_len) {
	size_t len;

	if (!key || !val) return -1;

	len = key_len + val_len;

	len += key_len > 127 ? 4 : 1;
	len += val_len > 127 ? 4 : 1;

	if (env->used + len >= FCGI_MAX_LENGTH) {
		/**
		 * we can't append more headers, ignore it
		 */
		return -1;
	}

	/**
	 * field length can be 31bit max
	 *
	 * HINT: this can't happen as FCGI_MAX_LENGTH is only 16bit
	 */
	if (key_len > 0x7fffffff) key_len = 0x7fffffff;
	if (val_len > 0x7fffffff) val_len = 0x7fffffff;

	buffer_prepare_append(env, len);
        
	if (key_len > 127) {
		env->ptr[env->used++] = ((key_len >> 24) & 0xff) | 0x80;
		env->ptr[env->used++] = (key_len >> 16) & 0xff;
		env->ptr[env->used++] = (key_len >> 8) & 0xff;
		env->ptr[env->used++] = (key_len >> 0) & 0xff;
	} else {
		env->ptr[env->used++] = (key_len >> 0) & 0xff;
	}

	if (val_len > 127) {
		env->ptr[env->used++] = ((val_len >> 24) & 0xff) | 0x80;
		env->ptr[env->used++] = (val_len >> 16) & 0xff;
		env->ptr[env->used++] = (val_len >> 8) & 0xff;
		env->ptr[env->used++] = (val_len >> 0) & 0xff;
	} else {
		env->ptr[env->used++] = (val_len >> 0) & 0xff;
	}

	memcpy(env->ptr + env->used, key, key_len);
	env->used += key_len;
	memcpy(env->ptr + env->used, val, val_len);
	env->used += val_len;

	return 0;
}

int fcgiCreateEnv(buffer *fc_buf, buffer *fc_fbuf, size_t request_id)
{
    FCGI_BeginRequestRecord beginRecord;
    FCGI_Header header;
    buffer *fcgi_env_buf;
    //char buf[32];
    
    /* send FCGI_BEGIN_REQUEST */
    fcgi_header(&(beginRecord.header), FCGI_BEGIN_REQUEST, request_id, sizeof(beginRecord.body), 0);
    beginRecord.body.roleB0 = FCGI_RESPONDER;
    beginRecord.body.roleB1 = 0;
    beginRecord.body.flags = 0;
    memset(beginRecord.body.reserved, 0, sizeof(beginRecord.body.reserved));
    
    buffer_copy_memory(fc_buf, (const char *)&beginRecord, sizeof(beginRecord));
    
    /* send FCGI_PARAMS */
    fcgi_env_buf = buffer_init();
    buffer_prepare_copy(fcgi_env_buf, 1024);
    int i;
    int arrlen;
    GET_ARRAY_LEN(fcgi_params, arrlen);
    
    char *c, *cp, lenstr[32];
    size_t blen;
    buffer *tmp_buf = buffer_init();
    for (i = 0; i < 2; i++) {
        if (NULL != (c = buffer_search_string_len_skip(fc_fbuf, CONST_STR_LEN("\n"), i))) {
            switch(i) {
                case 0:
                    blen = c - fc_fbuf->ptr;
                    buffer_append_string_len(tmp_buf, "/var/www/origin/www", strlen("/var/www/origin/www"));
                    buffer_append_string_len(tmp_buf, fc_fbuf->ptr, blen);
                    fcgi_env_add(fcgi_env_buf, "SCRIPT_FILENAME", strlen("SCRIPT_FILENAME"), tmp_buf->ptr, strlen(tmp_buf->ptr));
                    cp = fc_fbuf->ptr + blen;
                    break;
                case 1:
                    buffer_reset(tmp_buf);
                    blen = c - cp;
                    cp += 1;
                    buffer_append_string_len(tmp_buf, cp, blen - 1);
                    fcgi_env_add(fcgi_env_buf, "PATH_INFO", strlen("PATH_INFO"), tmp_buf->ptr, strlen(tmp_buf->ptr));
                    
                    buffer_reset(tmp_buf);
                    blen = fc_fbuf->used - (c - fc_fbuf->ptr) - 1;
                    c += 1;
                    buffer_append_string_len(tmp_buf, c, blen);
                    LI_ltostr(lenstr, blen);
                    fcgi_env_add(fcgi_env_buf, "CONTENT_LENGTH", strlen("CONTENT_LENGTH"), lenstr, strlen(lenstr));
                    break;
                default:
                    break;
            }
        } else {
            return 0;
        }
    }
    cp = NULL;c = NULL;
    
    for (i = 0; i < arrlen; i++) {
        fcgi_env_add(fcgi_env_buf, fcgi_params[i].name, strlen(fcgi_params[i].name), fcgi_params[i].value, strlen(fcgi_params[i].value));
    }
    redisLog(REDIS_NOTICE, "%s", fcgi_env_buf->ptr);
    fcgi_header(&(header), FCGI_PARAMS, request_id, fcgi_env_buf->used, 0);
    buffer_append_memory(fc_buf, (const char *)&header, sizeof(header));
    buffer_append_memory(fc_buf, (const char *)fcgi_env_buf->ptr, fcgi_env_buf->used);

    fcgi_header(&(header), FCGI_PARAMS, request_id, 0, 0);
    buffer_append_memory(fc_buf, (const char *)&header, sizeof(header));

    //fc_buf->used++; /* add virtual \0 */
    
    /* start STDIN input post */
    fcgi_header(&(header), FCGI_STDIN, request_id, tmp_buf->used, 0);
    buffer_append_memory(fc_buf, (const char *)&header, sizeof(header));
    buffer_append_memory(fc_buf, tmp_buf->ptr, tmp_buf->used);
    
    /* terminate STDIN */
    fcgi_header(&(header), FCGI_STDIN, request_id, 0, 0);
    buffer_append_memory(fc_buf, (const char *)&header, sizeof(header));
    //fc_buf->used++; /* add virtual \0 */
    
    buffer_free(fcgi_env_buf);
    
    return 1;
}

