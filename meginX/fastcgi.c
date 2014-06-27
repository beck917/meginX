/* 
 * File:   fastcgi.c
 * Author: Beck Xu <beck917@gmail.com>
 *
 * Created on 2014年6月25日, 下午3:57
 */

#include <stdio.h>
#include <stdlib.h>
#include "fastcgi.h"
#include "server.h"

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

int fcgiCreateEnv(fastcgiClient *fc, size_t request_id)
{
    FCGI_BeginRequestRecord beginRecord;
    FCGI_Header header;
    
    /* send FCGI_BEGIN_REQUEST */
    fcgi_header(&(beginRecord.header), FCGI_BEGIN_REQUEST, request_id, sizeof(beginRecord.body), 0);
    beginRecord.body.roleB0 = FCGI_RESPONDER;
    beginRecord.body.roleB1 = 0;
    beginRecord.body.flags = 0;
    memset(beginRecord.body.reserved, 0, sizeof(beginRecord.body.reserved));
    
    buffer_copy_memory(fc->buf, (const char *)&beginRecord, sizeof(beginRecord));
}

