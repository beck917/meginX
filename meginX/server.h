/* 
 * File:   server.h
 * Author: Beck Xu
 *
 * Created on 2014年5月14日, 下午2:44
 */

#ifndef SERVER_H
#define	SERVER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <syslog.h>
#include <netinet/in.h>
#include <signal.h>

#include "ae.h"      /* Event driven programming library */
#include "sds.h"     /* Dynamic safe strings */
#include "adlist.h"  /* Linked lists */
#include "zmalloc.h" /* total memory usage aware version of malloc/free */
#include "anet.h"    /* Networking the easy way */
#include "buffer.h"
#include "fastcgi.h"

/* Error codes */
#define REDIS_OK                0
#define REDIS_ERR               -1

/* Static server configuration */
#define REDIS_MAX_LOGMSG_LEN    1024 /* Default maximum length of syslog messages */
#define REDIS_SERVERPORT        6389    /* TCP port */
#define REDIS_TCP_BACKLOG       511     /* TCP listen backlog */
#define REDIS_DEFAULT_UNIX_SOCKET_PERM 0
#define REDIS_DEFAULT_TCP_KEEPALIVE 0
#define REDIS_IP_STR_LEN INET6_ADDRSTRLEN
#define REDIS_PEER_ID_LEN (REDIS_IP_STR_LEN+32) /* Must be enough for ip:port */
#define REDIS_DEFAULT_LOGFILE ""
#define REDIS_DEFAULT_SYSLOG_ENABLED 0
#define REDIS_MAX_CLIENTS 10000
#define REDIS_BINDADDR_MAX 16
#define REDIS_MIN_RESERVED_FDS 32
/* When configuring the Redis eventloop, we setup it so that the total number
 * of file descriptors we can handle are server.maxclients + RESERVED_FDS + FDSET_INCR
 * that is our safety margin. */
#define REDIS_EVENTLOOP_FDSET_INCR (REDIS_MIN_RESERVED_FDS+96)
/* Log levels */
#define REDIS_DEBUG 0
#define REDIS_VERBOSE 1
#define REDIS_NOTICE 2
#define REDIS_WARNING 3
#define REDIS_LOG_RAW (1<<10) /* Modifier to log without timestamp */
#define REDIS_DEFAULT_VERBOSITY REDIS_NOTICE

/* Protocol and I/O related defines */
#define REDIS_MAX_QUERYBUF_LEN  (1024*1024*1024) /* 1GB max query buffer. */
#define REDIS_IOBUF_LEN         (1024*16)  /* Generic I/O buffer size */
#define REDIS_REPLY_CHUNK_BYTES (16*1024) /* 16k output buffer */

/* With multiplexing we need to take per-client state.
 * Clients are taken in a liked list. */
typedef struct meginxClient {
    int fd;
    sds querybuf;
    int connected;
	size_t querybuf_peak;   /* Recent (100ms or more) peak of querybuf size */
    char reply_buf[REDIS_REPLY_CHUNK_BYTES];
    char format_buf[REDIS_IOBUF_LEN];
    size_t reply_len;
} meginxClient;

/*-----------------------------------------------------------------------------
 * Global server state
 *----------------------------------------------------------------------------*/
struct meginxServer {
	aeEventLoop *el;
    /* Networking */
    int port;                   /* TCP listening port */
    int tcp_backlog;            /* TCP listen() backlog */
    char *bindaddr[REDIS_BINDADDR_MAX]; /* Addresses we should bind to */
    int bindaddr_count;         /* Number of addresses in server.bindaddr[] */
    char *unixsocket;           /* UNIX socket path */
    mode_t unixsocketperm;      /* UNIX socket permission */
    int ipfd[REDIS_BINDADDR_MAX]; /* TCP socket file descriptors */
    int ipfd_count;             /* Used slots in ipfd[] */
    int sofd;                   /* Unix socket file descriptor */
    list *clients;              /* List of active clients */
    list *clients_to_close;     /* Clients to close asynchronously */
    meginxClient *current_client; /* Current client, only used on crash report */
    //redisClient *current_client; /* Current client, only used on crash report */
    char neterr[ANET_ERR_LEN];  /* Error buffer for anet.c */
    /* Limits */
    int maxclients;                 /* Max number of simultaneous clients */
    unsigned long long maxmemory;   /* Max number of memory bytes to use */
    /* Configuration */
    int verbosity;                  /* Loglevel in redis.conf */
    int tcpkeepalive;               /* Set SO_KEEPALIVE if non-zero. */
    /* Logging */
    char *logfile;                  /* Path of log file */
    int syslog_enabled;             /* Is syslog enabled? */
};

/*-----------------------------------------------------------------------------
 * Extern declarations
 *----------------------------------------------------------------------------*/

extern struct meginxServer server;

/* networking.c -- Networking and Client related operations */
meginxClient *createClient(int fd);
void readQueryFromClient(aeEventLoop *el, int fd, void *privdata, int mask);
void acceptTcpHandler(aeEventLoop *el, int fd, void *privdata, int mask);
/* Anti-warning macro... */
#define REDIS_NOTUSED(V) ((void) V)

#endif	/* SERVER_H */

