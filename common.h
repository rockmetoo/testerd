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
#define NUM_ELEM(x)		(sizeof(x) / sizeof(*(x)))

#define ASSERT(e) do { if(!(e)) { logCritical("AssertException: " #e \
        " at %s:%d\naborting..\n", __FILE__, __LINE__); abort(); } } while(0)

#define MAX_SMITH_PLOT	128

#define	PLOT_KEY(x, secname, key)	sprintf(x, "%s:%s", secname, key);

#define	COMMAND_KEY		"40complex#$!11JY"

#define TIMEOUT_SECS    5
#define MAX_HOPS		64
#define UPLOAD_FILE_DIR	"/var/tmp/smithcp/"



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

struct smithTestPlotResponse
{
	char*	pszResponse;
	size_t	size;
	double	dProcessRequiredTime;
	int		nCurlStatus;
	int		lHttpCode;
	double	dNameLookupTime;
	double	dConnectTime;
	double	dBytesDownloaded;
	double	dTotalDownloadTime;
	double	dAvgDownloadSpeed;
	double	dAvgUploadSpeed;
	double	dAppConnectTime;
	double	dPreTransferTime;
	double	dStartTransferTime;
};

struct smithTestPlotHttpResponse
{
	UDWORD	dwUsedId;
	char*	pszPlotName;
	size_t	size;
	char*	pszHeader;
	int		nType;
	int		nMethod;
	int		nContentType;
	int		nAccept;
	int		nCharset;
	char*	pszBaseAddress;
	char*	pszQueryData;
	int		nResponseTimeoutLimit;
	int		nIsDownloadCheck;
	int		nAuthType;
	char*	pszAuthUser;
	char*	pszAuthPassword;
	char*	pszConsumerKey;
	char*	pszConsumerSecret;
	char*	pszToken;
	char*	pszTokenSecret;
	int		nPlotReportDbIndex;
};

struct smithTestPlotHttpDownloadResponse
{
	UDWORD	dwUsedId;
	char*	pszPlotName;
	size_t	size;
	char*	pszHeader;
	int		nType;
	char*	pszBaseAddress;
	int		nResponseTimeoutLimit;
	int		nIsDownloadCheck;
	int		nPlotReportDbIndex;
};

struct smithTestPlotHttpUploadResponse
{
	UDWORD	dwUsedId;
	char*	pszPlotName;
	size_t	size;
	char*	pszHeader;
	int		nType;
	char*	pszBaseAddress;
	char*	pszUploadFileAttribute;
	char*	pszUploadFile;
	char*	pszQueryData;
	int		nResponseTimeoutLimit;
	int		nIsUploadCheck;
	int		nPlotReportDbIndex;
};

// TODO: Remove this variable
struct	smithTestPlotResponse HTTP_SMITH_CHUNK[MAX_SMITH_PLOT];

struct httpPlotStructure
{
	char data[256];
};

struct urlStructure
{
	char*	pszProtocol;
	char*	pszUsername;
	char*	pszPassword;
	int		nIpv6Host;
	char*	pszHost;
	char*	pszPort;
	char*	pszPath;
};
typedef struct urlStructure urlst;

char	MYSQL_HOST_ACCESS[1024] 	= {0};
char	MYSQL_USER_ACCESS[1024] 	= {0};
char	MYSQL_PASS_ACCESS[1024] 	= {0};
char	MYSQL_PORT_ACCESS[1024]		= {0};
char	MYSQL_DB_ACCESS[1024]		= {0};

char	MONGO_HOST_ACCESS[1024] 	= {0};
char	MONGO_USER_ACCESS[1024] 	= {0};
char	MONGO_PASS_ACCESS[1024] 	= {0};
int		MONGO_PORT_ACCESS			= 27017;
char 	MONGO_DB_ACCESS[1024]		= {0};

#define	SMITH_WORKER_THREAD						2
#define	SMITH_THREAD_RUNNER_WORKER_THREAD		2
#define WORKER_STACK_SIZE						(32*1024)

char	APPLICATION_ENV[32]			= {0};

// XXX: IMPORTANT - hold all strd server host address from smith.conf
char* szListOfThreadRunner[2048];

// XXX: IMPORTANT - hold all smithd server host address from str.conf
char* szListOfSmithd[256];


// XXX: IMPORTANT - hold all plot report DB server host(mongo) from str.conf/smith.conf
char* szPlotReportDb[256];

enum SMITH_THREAD_RUNNER_CMD_INDEX
{
	SMITH_THREAD_RUNNER_CMD_INDEX_HTTP		= 1,
	SMITH_THREAD_RUNNER_CMD_INDEX_STATUS	= 2,
	SMITH_THREAD_RUNNER_CMD_INDEX_HTTP_DL	= 49,
	SMITH_THREAD_RUNNER_CMD_INDEX_HTTP_UL	= 50
};

enum SMITH_CMD_INDEX
{
	SMITH_CMD_INDEX_RUN_SPECIFIC_PLOT		= 1,
	SMITH_CMD_INDEX_UPDATE_STR_STATUS		= 2,
	SMITH_CMD_INDEX_STR_THREAD_DONE			= 3,
	SMITH_CMD_INDEX_RUN_SPECIFIC_DL_PLOT	= 49,
	SMITH_CMD_INDEX_RUN_SPECIFIC_UL_PLOT	= 50
};

char* SMITH_CONTENT_TYPE[] =
{
	"",
	"Content-Type: text/html",
	"Content-Type: application/json"
};

char* SMITH_ACCEPT[] =
{
	"",
	"Content-Type: text/html",
	"Content-Type: application/json"
};

char* SMITH_CHARSET[] =
{
	"",
	"utf-8",
};

#endif
