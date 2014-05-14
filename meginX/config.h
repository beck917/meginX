/* 
 * File:   config.h
 * Author: Beck Xu
 *
 * Created on 2014年5月14日, 上午11:43
 */

#ifndef CONFIG_H
#define	CONFIG_H

#ifdef __sun
#include <sys/feature_tests.h>
#ifdef _DTRACE_VERSION
#define HAVE_EVPORT 1
#endif
#endif

/* Test for polling API */
#ifdef __linux__
#define HAVE_EPOLL 1
#endif

#if (defined(__APPLE__) && defined(MAC_OS_X_VERSION_10_6)) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined (__NetBSD__)
#define HAVE_KQUEUE 1
#endif

#endif	/* CONFIG_H */

