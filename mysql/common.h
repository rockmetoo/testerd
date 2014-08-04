#ifndef __common_h__
#define __common_h__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <assert.h>
#include <unistd.h>
#include <getopt.h>
#include <fcntl.h>
#include <pthread.h>
#include <syslog.h>
#include <malloc.h>
#include <errno.h>
#include <sys/select.h>
#include <stddef.h>
#include <signal.h>
#include <sys/wait.h>
#include <execinfo.h>

#include <sys/stat.h>


#define	TRUE			1
#define	FALSE			0
#define	DEBUG			TRUE

#define	SMITH_VERSION	"1.0.0"
#define	PROG_NAME		"smith"
#define	PROG_NAME_VER	"smith 1.0.0"

#define PATH_DEVNULL	"/dev/null"
#define LOCKMODE		(S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)

#define LOCK(mutex) do { pthread_mutex_t *_yymutex = &(mutex); \
        assert(pthread_mutex_lock(_yymutex)==0);

#define END_LOCK 		assert(pthread_mutex_unlock(_yymutex)==0); } while (0)
#define STRERROR		strerror(errno)


#define MICRO_SEC		1000000.00
#define MILLI_SEC		1000.00

#define EVILPTR(a)		(!(a) || (*(a) == '\0'))
#define FREEP(p)		if(!EVILPTR(p)) free(p)
#define FREE(p)			((void)(free(p), (p)= 0))
#define NEW(p)			((p)= xCalloc(1, (long)sizeof *(p)))

#define ASSERT(e) do { if(!(e)) { logCritical("AssertException: " #e \
        " at %s:%d\naborting..\n", __FILE__, __LINE__); abort(); } } while(0)

#define MAX_SMITH_PLOT	128

#define	PLOT_KEY(x, secname, key)	sprintf(x, "%s:%s", secname, key);

typedef uint8_t			UBYTE;
typedef int8_t			SBYTE;
typedef uint8_t    		BOOL;
typedef uint16_t    	UWORD;
typedef int16_t			SWORD;
typedef uint32_t    	UDWORD;
typedef int32_t			SDWORD;
typedef uint64_t    	UQWORD;
typedef int64_t			SQWORD;
// 32bits 4bytes, 64bits 8bytes
typedef unsigned long	ULONG;

#endif
