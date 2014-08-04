/**
 * helper.h
 *
 */

#ifndef __helper_h__
#define __helper_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "log.h"
#include "jsonlib/jansson.h"


#define BUF_SIZE 10*1024


int			commandExecute(const char* cmdstring);
size_t		writeCallback(void* dataPtr, size_t size, size_t nmemb, void* stream);
struct		smithTestPlotResponse* initCurl(struct smithTestPlotHttpResponse* chunk);
struct		smithTestPlotResponse* initDownloadCurl(struct smithTestPlotHttpDownloadResponse* chunk);
struct		smithTestPlotResponse* initUploadCurl(struct smithTestPlotHttpUploadResponse* chunk);
int			safeOpen(const char* pszPath, int nFlags, mode_t mtMode);
ssize_t 	safeBwrite(int nFd, const uint8_t* byBuff, size_t stCount);
char*		safeStrdup(const char* pszString);
int			writeLockFile(int nFd);
unsigned	sSleep(unsigned int nSecs);
void 		mSleep(int nMsecs);
void 		uSleep(unsigned int nUsecs);
double		getMicroSecond();
double		getMilliSecond();
int			fileExist(char* pszFilename);
int 		isFile(char* pszFilename);
int			isDirectory(char* pszDirName);
char*		strReplace(const char* pszSearch, const char* pszReplace, const char* pszHayStack);
int			readTheWholeFile(char* pszFname, char* pszArray);
void*		xMalloc(int nI);
uint32_t	string2Int(uint8_t* byString, size_t stStringSize);
char**		explodeString(char* pszString, char* pszSep, int* nItems);
void		freeExplodedString(char** pszString);
char*		formattedString(const char* fmt, ...);
int			randomNumber(int minNum, int maxNum);
urlst*		parseUrl(const char *);
void		freeUrl(urlst*);
char*		parseQueryString(char* string, char* pszQueryString);
int			strstrsplit(const char*, const char*, char**, char**);
int			strchrsplit(const char*, const char, char**, char**);
int			strrchrsplit(const char*, const char, char**, char**);
int			strpchrsplit(const char*, const char*, const char, char**, char**);
char*		escapeShellArg(char* str);

#ifdef __cplusplus
}
#endif
#endif
