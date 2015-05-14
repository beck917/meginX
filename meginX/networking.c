/* 
 * File:   networking.c
 * Author: Beck Xu
 * Created on 2014年5月12日, 下午5:11
 * base on redis networking
 */
#include "server.h"
#include <sys/uio.h>
#include <math.h>
#include "websocket.h"

/* resetClient prepare the client to process the next command */
void resetClient(meginxClient *c) {
    memset(c->format_buf, '\0', REDIS_IOBUF_LEN);
    memset(c->reply_buf, '\0', REDIS_IOBUF_LEN);
    sdsfree(c->querybuf);
    c->querybuf = sdsempty();
}

void freeFcgiClient(fastcgiResponse *fr)
{
    buffer_free(fr->buf);
    buffer_free(fr->format_buf);
    fr->buf = NULL;
    fr->format_buf = NULL;
    if (fr->fd != -1) {
        aeDeleteFileEvent(server.el,fr->fd,AE_READABLE);
        aeDeleteFileEvent(server.el,fr->fd,AE_WRITABLE);
        close(fr->fd);
    }
    zfree(fr);
}

void freeClient(meginxClient *c) {
    /* Free the query buffer */
    sdsfree(c->querybuf);
    c->querybuf = NULL;
    /* If this is marked as current client unset it */
    if (server.current_client == c) server.current_client = NULL;
    
    /* Close socket, unregister events, and remove list of replies and
     * accumulated arguments. */
    redisLog(REDIS_NOTICE, "C %d", c->fd);
    if (c->fr != NULL) {
        freeFcgiClient(c->fr);
        c->fr = NULL;
    }
    if (c->fd != -1) {
        aeDeleteFileEvent(server.el,c->fd,AE_READABLE);
        aeDeleteFileEvent(server.el,c->fd,AE_WRITABLE);
        close(c->fd);
    }
    zfree(c);
}

void sendSubClinets(meginxClient *c, sds *buf) {
    c->reply_len = WEBSOCKET_set_content(buf, sdslen(buf), c->reply_buf, REDIS_IOBUF_LEN);

    write(c->fd, c->reply_buf, c->reply_len);
    
    resetClient(c);
}

void testSubFunction(meginxClient *c) {
    robj *channel = createObject(REDIS_STRING, sdsnew("new.1"));
    pubsubSubscribeChannel(c, channel);
}

void testPubFunction(meginxClient *c) {
    robj *channel = createObject(REDIS_STRING, sdsnew("new.1"));
    pubsubPublishMessage(channel, sdsnew("test.new.1"));
}

void readQueryFromFcgi(aeEventLoop *el, int fd, void *privdata, int mask) 
{
    meginxClient *c = privdata;
    REDIS_NOTUSED(el);
    REDIS_NOTUSED(mask);
    int nread, readlen;
    
    if (c->fr == NULL) {
        c->fr = zmalloc(sizeof(fastcgiResponse));
        c->fr->buf = buffer_init();
        c->fr->format_buf = buffer_init();
    }
    c->fr->fd = fd;
    c->fr->offset = 0;
    
    readlen = REDIS_IOBUF_LEN;
    buffer_prepare_copy(c->fr->buf, readlen);
    nread = read(fd, c->fr->buf->ptr, readlen);
    
    c->fr->buf->used = nread + 1; /* one extra for the fake \0 */
    c->fr->buf->ptr[c->fr->buf->used - 1] = '\0';
    redisLog(REDIS_NOTICE, "%d", nread);

    if (nread == -1) {
        if (errno == EAGAIN) {
            nread = 0;
        } else {
            redisLog(REDIS_NOTICE, "error %d", errno);
            redisLog(REDIS_NOTICE, "Reading from client: %s",strerror(errno));
            return;
        }
    } else if (nread == 0) {
        redisLog(REDIS_NOTICE, "fcgi Client closed connection");
        freeFcgiClient(c->fr);
        return;
    }
    
    testSubFunction(c);
    testPubFunction(c);
    
    fcgi_demux_response(c->fr);
    c->reply_len = WEBSOCKET_set_content( c->fr->format_buf->ptr, c->fr->format_buf->used, c->reply_buf, REDIS_IOBUF_LEN );
    freeFcgiClient(c->fr);
    c->fr = NULL;
    if (aeCreateFileEvent(server.el, c->fd, AE_WRITABLE,sendReplyToClient, c) == AE_ERR) return;
}

void sendFcgiRequest(aeEventLoop *el, int fd, void *privdata, int mask)
{
    meginxClient *c = privdata;
    REDIS_NOTUSED(el);
    REDIS_NOTUSED(mask);
    
    int fce_ret;
    redisLog(REDIS_NOTICE, "ae2");
    buffer *buf = buffer_init();
    buffer *fbuf = buffer_init();
    
    buffer_append_memory(fbuf, (const char *)c->format_buf, strlen(c->format_buf));
    fce_ret = fcgiCreateEnv(buf, fbuf, 1200);
    
    if (fce_ret < 1) {
        buffer_free(buf);
        buffer_free(fbuf);
        aeDeleteFileEvent(server.el, fd, AE_WRITABLE);
        close(fd);
        return;
    }
    
    redisLog(REDIS_NOTICE, "%d",buf->size);
    redisLog(REDIS_NOTICE, "%d",buf->used);

    FILE *fp = fopen("myfile.bin","wb");
    fwrite(buf->ptr, buf->used, 1, fp);
    fclose(fp);
    
    aeCreateFileEvent(server.el, fd, AE_READABLE, readQueryFromFcgi, c);
    write(fd, buf->ptr, buf->used);
    
    buffer_free(buf);
    buffer_free(fbuf);
    aeDeleteFileEvent(server.el, fd, AE_WRITABLE);
}

int connectFastcgi(meginxClient *c)
{
    int fd;
    //redisLog(REDIS_NOTICE, "ae1");
    //fd = anetTcpNonBlockConnect(NULL, "127.0.0.1", 9000);
    fd = anetUnixGenericConnect(NULL, "/tmp/php-cgi.sock", 1);
    if (fd == -1) {
        redisLog(REDIS_WARNING,"Unable to connect to fastcgi service: %s",
            strerror(errno));
        close(fd);
        return REDIS_ERR;
    }
    
    if (aeCreateFileEvent(server.el,fd,AE_WRITABLE,sendFcgiRequest,c) ==
            AE_ERR)
    {
        close(fd);
        redisLog(REDIS_WARNING,"Can't create readable event for SYNC");
        return REDIS_ERR;
    }
    
    return REDIS_OK;
}

void sendReplyToClient(aeEventLoop *el, int fd, void *privdata, int mask) {
    meginxClient *c = privdata;
    int nwritten = 0;
    
    REDIS_NOTUSED(el);
    REDIS_NOTUSED(mask);

    //char *str = "*1\r\n$1\r\n1\r\n";
    //nwritten = write(fd, str, strlen(str));
    if (c->reply_len == 0) {
        c->reply_len = strlen(c->handshake_buf);
        nwritten = write(fd, c->handshake_buf, c->reply_len);
    } else {
        nwritten = write(fd, c->reply_buf, c->reply_len);
    }

    resetClient(c);
    
    aeDeleteFileEvent(server.el,c->fd,AE_WRITABLE);
}

void readQueryFromClient(aeEventLoop *el, int fd, void *privdata, int mask) {
    meginxClient *c = (meginxClient*) privdata;
    int nread, readlen;
    REDIS_NOTUSED(el);
    REDIS_NOTUSED(mask);

    readlen = REDIS_IOBUF_LEN; 
    
    c->querybuf = sdsMakeRoomFor(c->querybuf, readlen);
    nread = read(fd, c->querybuf, readlen);
    if (nread == -1) {
        if (errno == EAGAIN) {
            nread = 0;
        } else {
            redisLog(REDIS_NOTICE, "Reading from client: %s",strerror(errno));
            freeClient(c);
            return;
        }
    } else if (nread == 0) {
        redisLog(REDIS_NOTICE, "Client closed connection");
        freeClient(c);
        return;
    }
    
    if (nread) {
        sdsIncrLen(c->querybuf,nread);
    } else {
        return;
    }

    if (c->connected == 0) {
        WEBSOCKET_generate_handshake(c->querybuf, c->handshake_buf, REDIS_IOBUF_LEN);
        c->reply_len = 0;
        if (aeCreateFileEvent(server.el, c->fd, AE_WRITABLE,sendReplyToClient, c) == AE_ERR) return;
        c->connected = 1;
    } else {
        int result = WEBSOCKET_get_content(c->querybuf, nread, c->format_buf, REDIS_IOBUF_LEN);
        if (result < 0) {
            //disconnect
            redisLog(REDIS_NOTICE, "Client closed connection 1");
            freeClient(c);
            return;
        }
        connectFastcgi(c);
        //c->reply_len = WEBSOCKET_set_content( c->format_buf, strlen(c->format_buf), c->reply_buf, REDIS_IOBUF_LEN );
        //if (aeCreateFileEvent(server.el, c->fd, AE_WRITABLE,sendReplyToClient, c) == AE_ERR) return;
    }
}

meginxClient *createClient(int fd) {
    meginxClient *c = zmalloc(sizeof(meginxClient));

    /* passing -1 as fd it is possible to create a non connected client.
     * This is useful since all the Redis commands needs to be executed
     * in the context of a client. When commands are executed in other
     * contexts (for instance a Lua script) we need a non connected client. */
    if (fd != -1) {
        anetNonBlock(NULL,fd);
        anetEnableTcpNoDelay(NULL,fd);
        if (server.tcpkeepalive)
            anetKeepAlive(NULL,fd,server.tcpkeepalive);
        if (aeCreateFileEvent(server.el,fd,AE_READABLE,
            readQueryFromClient, c) == AE_ERR)
        {
            close(fd);
            zfree(c);
            return NULL;
        }
    }

    c->fd = fd;
    c->querybuf = sdsempty();
    c->querybuf_peak = 0;
    c->connected = 0;
    c->reply_len = 0;
    memset(c->format_buf, '\0', REDIS_IOBUF_LEN);
    memset(c->reply_buf, '\0', REDIS_IOBUF_LEN);
    memset(c->handshake_buf, '\0', 1024);
    c->fr = zmalloc(sizeof(fastcgiResponse));
    //c->fr->fd = -1;
    c->fr->buf = buffer_init();
    c->fr->offset = 0;
    c->fr->format_buf = buffer_init();
    c->pubsub_channels = dictCreate(&setDictType,NULL);
    c->pubsub_patterns = listCreate();
    return c;
}

static void acceptCommonHandler(int fd, int flags) {
    meginxClient *c;
    if ((c = createClient(fd)) == NULL) {
        redisLog(REDIS_WARNING,
            "Error registering fd event for the new client: %s (fd=%d)",
            strerror(errno),fd);
        close(fd); /* May be already closed, just ignore errors */
        return;
    }
}

void acceptTcpHandler(aeEventLoop *el, int fd, void *privdata, int mask) {
    int cport, cfd;
    char cip[REDIS_IP_STR_LEN];
    REDIS_NOTUSED(el);
    REDIS_NOTUSED(mask);
    REDIS_NOTUSED(privdata);

    cfd = anetTcpAccept(server.neterr, fd, cip, sizeof(cip), &cport);
    if (cfd == ANET_ERR) {
        redisLog(REDIS_WARNING,"Accepting client connection: %s", server.neterr);
        return;
    }
    redisLog(REDIS_VERBOSE,"Accepted %s:%d", cip, cport);
    acceptCommonHandler(cfd,0);
}