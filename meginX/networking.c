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
#include "fastcgi.h"


void readQueryFromFcgi(aeEventLoop *el, int fd, void *privdata, int mask) 
{
    REDIS_NOTUSED(el);
    REDIS_NOTUSED(privdata);
    REDIS_NOTUSED(mask);
    char *buf;
    int readlen;
    readlen = REDIS_IOBUF_LEN;
    read(fd, buf, readlen);
    
    redisLog(REDIS_NOTICE, buf);
    aeDeleteFileEvent(server.el, fd, AE_READABLE);
}

void sendRequest(aeEventLoop *el, int fd, void *privdata, int mask)
{
    REDIS_NOTUSED(el);
    REDIS_NOTUSED(privdata);
    REDIS_NOTUSED(mask);
    
    buffer *env;
    
    fastcgiClient *fc = zmalloc(sizeof(fastcgiClient));
    fc->fd = fd;
    fc->buf = buffer_init();

    fcgiCreateEnv(fc, 1);
    
    write(fd, fc->buf->ptr, fc->buf->used);
    aeDeleteFileEvent(server.el, fd, AE_WRITABLE);
    aeCreateFileEvent(server.el, fd, AE_READABLE, readQueryFromFcgi, NULL);
}

void connectFastcgi(void)
{
    int fd;

    fd = anetTcpNonBlockConnect(NULL, '192.179.1.79', '80');
    if (fd == -1) {
        redisLog(REDIS_WARNING,"Unable to connect to fastcgi service: %s",
            strerror(errno));
        return REDIS_ERR;
    }
    
    if (aeCreateFileEvent(server.el,fd,AE_WRITABLE,sendRequest,NULL) ==
            AE_ERR)
    {
        close(fd);
        redisLog(REDIS_WARNING,"Can't create readable event for SYNC");
        return REDIS_ERR;
    }
    
    return REDIS_OK;
}

/* resetClient prepare the client to process the next command */
void resetClient(meginxClient *c) {
    memset(c->format_buf, '\0', REDIS_IOBUF_LEN);
    memset(c->reply_buf, '\0', REDIS_IOBUF_LEN);
    sdsfree(c->querybuf);
    c->querybuf = sdsempty();
}

void freeClient(meginxClient *c) {
    /* Free the query buffer */
    sdsfree(c->querybuf);
    c->querybuf = NULL;
    
    /* Close socket, unregister events, and remove list of replies and
     * accumulated arguments. */
    if (c->fd != -1) {
        aeDeleteFileEvent(server.el,c->fd,AE_READABLE);
        aeDeleteFileEvent(server.el,c->fd,AE_WRITABLE);
        close(c->fd);
    }
}

void sendReplyToClient(aeEventLoop *el, int fd, void *privdata, int mask) {
    meginxClient *c = privdata;
    int nwritten = 0;
    
    REDIS_NOTUSED(el);
    REDIS_NOTUSED(mask);
    redisLog(REDIS_NOTICE, "ae s");
    //char *str = "*1\r\n$1\r\n1\r\n";
    //nwritten = write(fd, str, strlen(str));
    if (c->reply_len == 0) {
        c->reply_len = strlen(c->reply_buf);
    }
    nwritten = write(fd, c->reply_buf, c->reply_len);
    
    redisLog(REDIS_NOTICE, "%d", nwritten);
    redisLog(REDIS_NOTICE, "ae d");

    resetClient(c);
    
    aeDeleteFileEvent(server.el,c->fd,AE_WRITABLE);
    //redisLog(REDIS_NOTICE,c->format_buf);
}

void readQueryFromClient(aeEventLoop *el, int fd, void *privdata, int mask) {
    meginxClient *c = (meginxClient*) privdata;
    int nread, readlen;
    REDIS_NOTUSED(el);
    REDIS_NOTUSED(mask);

    server.current_client = c;
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
        redisLog(REDIS_NOTICE, "test1");
        server.current_client = NULL;
        return;
    }
    if (c->connected == 0) {
        WEBSOCKET_generate_handshake(c->querybuf, c->reply_buf, REDIS_IOBUF_LEN);
        c->reply_len = 0;
        c->connected = 1;
    } else {
        int result = WEBSOCKET_get_content(c->querybuf, nread, c->format_buf, REDIS_IOBUF_LEN);
        c->reply_len = WEBSOCKET_set_content( c->format_buf, strlen(c->format_buf), c->reply_buf, REDIS_IOBUF_LEN );
    }
    redisLog(REDIS_NOTICE, "ae g");
    if (aeCreateFileEvent(server.el, c->fd, AE_WRITABLE,sendReplyToClient, c) == AE_ERR) return;
    server.current_client = NULL;
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