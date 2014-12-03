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
#include "dict.h"

/* Error codes */
#define REDIS_OK                0
#define REDIS_ERR               -1

/* Static server configuration */
#define REDIS_MAX_LOGMSG_LEN    1024 /* Default maximum length of syslog messages */
#define REDIS_SERVERPORT        6389    /* TCP port */
#define PUB_PORT 6390
#define REDIS_TCP_BACKLOG       511     /* TCP listen backlog */
#define REDIS_DEFAULT_UNIX_SOCKET_PERM 0
#define REDIS_DEFAULT_TCP_KEEPALIVE 60
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

#define REDIS_SHARED_SELECT_CMDS 10
#define REDIS_SHARED_INTEGERS 10000
#define REDIS_SHARED_BULKHDR_LEN 32

/* Protocol and I/O related defines */
#define REDIS_MAX_QUERYBUF_LEN  (1024*1024*1024) /* 1GB max query buffer. */
#define REDIS_IOBUF_LEN         (1024*16)  /* Generic I/O buffer size */
#define REDIS_REPLY_CHUNK_BYTES (16*1024) /* 16k output buffer */

/* We can print the stacktrace, so our assert is defined this way: */
#define redisAssertWithInfo(_c,_o,_e) ((_e)?(void)0 : (_redisAssertWithInfo(_c,_o,#_e,__FILE__,__LINE__),_exit(1)))
#define redisAssert(_e) ((_e)?(void)0 : (_redisAssert(#_e,__FILE__,__LINE__),_exit(1)))
#define redisPanic(_e) _redisPanic(#_e,__FILE__,__LINE__),_exit(1)

/*-----------------------------------------------------------------------------
 * Data types
 *----------------------------------------------------------------------------*/

typedef long long mstime_t; /* millisecond time type. */

/* A redis object, that is a type able to hold a string / list / set */

/* The actual Redis Object */
#define REDIS_LRU_BITS 24
#define REDIS_LRU_CLOCK_MAX ((1<<REDIS_LRU_BITS)-1) /* Max value of obj->lru */
#define REDIS_LRU_CLOCK_RESOLUTION 1 /* LRU clock resolution in seconds */
typedef struct redisObject {
    unsigned type:4;
    unsigned encoding:4;
    unsigned lru:REDIS_LRU_BITS; /* lru time (relative to server.lruclock) */
    int refcount;
    void *ptr;
} robj;

/* With multiplexing we need to take per-client state.
 * Clients are taken in a liked list. */
typedef struct meginxClient {
    int fd;
    sds querybuf;
    int connected;
	size_t querybuf_peak;   /* Recent (100ms or more) peak of querybuf size */
    unsigned char reply_buf[REDIS_REPLY_CHUNK_BYTES];
	char handshake_buf[1024];
    unsigned char format_buf[REDIS_IOBUF_LEN];
    size_t reply_len;
	fastcgiResponse *fr;
	int argc;
    robj **argv;
    dict *pubsub_channels;  /* channels a client is interested in (SUBSCRIBE) */
    list *pubsub_patterns;  /* patterns a client is interested in (SUBSCRIBE) */
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
    /* Pubsub */
    dict *pubsub_channels;  /* Map channels to list of subscribed clients */
    list *pubsub_patterns;  /* A list of pubsub_patterns */
	unsigned lruclock:REDIS_LRU_BITS; /* Clock for LRU eviction */
	int bug_report_start; /* True if bug report header was already logged. */
};

struct sharedObjectsStruct {
    robj *crlf, *ok, *err, *emptybulk, *czero, *cone, *cnegone, *pong, *space,
    *colon, *nullbulk, *nullmultibulk, *queued,
    *emptymultibulk, *wrongtypeerr, *nokeyerr, *syntaxerr, *sameobjecterr,
    *outofrangeerr, *noscripterr, *loadingerr, *slowscripterr, *bgsaveerr,
    *masterdownerr, *roslaveerr, *execaborterr, *noautherr, *noreplicaserr,
    *oomerr, *plus, *messagebulk, *pmessagebulk, *subscribebulk,
    *unsubscribebulk, *psubscribebulk, *punsubscribebulk, *del, *rpop, *lpop,
    *lpush, *emptyscan, *minstring, *maxstring,
    *select[REDIS_SHARED_SELECT_CMDS],
    *integers[REDIS_SHARED_INTEGERS],
    *mbulkhdr[REDIS_SHARED_BULKHDR_LEN], /* "*<value>\r\n" */
    *bulkhdr[REDIS_SHARED_BULKHDR_LEN];  /* "$<value>\r\n" */
};

/* Object types */
#define REDIS_STRING 0
#define REDIS_LIST 1
#define REDIS_SET 2
#define REDIS_ZSET 3
#define REDIS_HASH 4

/* Objects encoding. Some kind of objects like Strings and Hashes can be
 * internally represented in multiple ways. The 'encoding' field of the object
 * is set to one of this fields for this object. */
#define REDIS_ENCODING_RAW 0     /* Raw representation */
#define REDIS_ENCODING_INT 1     /* Encoded as integer */
#define REDIS_ENCODING_HT 2      /* Encoded as hash table */
#define REDIS_ENCODING_ZIPMAP 3  /* Encoded as zipmap */
#define REDIS_ENCODING_LINKEDLIST 4 /* Encoded as regular linked list */
#define REDIS_ENCODING_ZIPLIST 5 /* Encoded as ziplist */
#define REDIS_ENCODING_INTSET 6  /* Encoded as intset */
#define REDIS_ENCODING_SKIPLIST 7  /* Encoded as skiplist */

/*-----------------------------------------------------------------------------
 * Extern declarations
 *----------------------------------------------------------------------------*/

extern struct meginxServer server;
extern dictType setDictType;

/* networking.c -- Networking and Client related operations */
meginxClient *createClient(int fd);
void readQueryFromClient(aeEventLoop *el, int fd, void *privdata, int mask);
void acceptTcpHandler(aeEventLoop *el, int fd, void *privdata, int mask);
/* Anti-warning macro... */
#define REDIS_NOTUSED(V) ((void) V)

typedef struct pubsubPattern {
    meginxClient *client;
    robj *pattern;
} pubsubPattern;

/* Redis object implementation */
void decrRefCount(robj *o);
void incrRefCount(robj *o);
void freeStringObject(robj *o);
robj *createObject(int type, void *ptr);
robj *getDecodedObject(robj *o);
robj *createStringObject(char *ptr, size_t len);

int pubsubSubscribeChannel(meginxClient *c, robj *channel);
int pubsubPublishMessage(robj *channel, sds *message);

/* Debugging stuff */
void _redisAssert(char *estr, char *file, int line);
void _redisPanic(char *msg, char *file, int line);
#endif	/* SERVER_H */

