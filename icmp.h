/**
 *
 */

#ifndef __icmp_h__
#define __icmp_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include <sys/socket.h>

struct stTraceRoute
{
	char	szHost[255+1];
	int		nHops;
	UDWORD	dwUsedId;
	char	szPlotName[255+1];
	char	szTablename[32+1];
};

void				ping(struct sockaddr*, int, uint16_t);
struct sockaddr_in	pong(int sock);

#ifdef __cplusplus
}
#endif
#endif
