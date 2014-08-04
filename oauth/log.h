//=============================================================================
//	File name: log.h
//	Description: gcc -O3 -J4 -c -o log.o log.c
//  Created on: October 27, 2010
//  Author: rockmetoo
//-----------------------------------------------------------------------------
#ifndef __log_h__
#define __log_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "../common.h"

#define TIMEFORMAT				"%Z %b %e %T"
#define LOGMASK					0112
char	g_szProgName[64]		= {"cockpitD"};
char	g_szLogFileName[256+1]	= {"/var/log/smithd/smithd.log"};
int		g_nIsSyslog 			= 0;
int		g_nFacility 			= 3;
int		g_nDebug				= 0;

//=============================================================================
//Public decleration
//-----------------------------------------------------------------------------
int					logInit();
void				logEmergency(const char* pszString, ...);
void				logAlert(const char* pszString, ...);
void				logCritical(const char* pszString, ...);
void				logError(const char* pszString, ...);
void				logWarning(const char* pszString, ...);
void				logNotice(const char* pszString, ...);
void				logInfo(const char* pszString, ...);
void				logDebug(const char* pszString, ...);
void				logClose();

#ifdef __cplusplus
}
#endif

#endif
//=============================================================================
//End of program
//-----------------------------------------------------------------------------
