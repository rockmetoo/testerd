/**
 *
 */

#include "helper.h"
#include "iniparser.h"
#include "oauth/oauth.h"

#include <curl/curl.h>
#include <curl/easy.h>

static int CURL_DEBUG = 0;

int commandExecute(const char* cmdstring)
{
	pid_t				pid;
	int					status;
	struct sigaction	ignore, saveintr, savequit;
	sigset_t			chldmask, savemask;

	// always a command processor with UNIX
	if(cmdstring == NULL) return(1);

	// ignore SIGINT and SIGQUIT
	ignore.sa_handler = SIG_IGN;
	sigemptyset(&ignore.sa_mask);
	ignore.sa_flags = 0;
	if(sigaction(SIGINT, &ignore, &saveintr) < 0) return(-1);
	if(sigaction(SIGQUIT, &ignore, &savequit) < 0) return(-1);
	// now block SIGCHLD
	sigemptyset(&chldmask);
	sigaddset(&chldmask, SIGCHLD);
	if(sigprocmask(SIG_BLOCK, &chldmask, &savemask) < 0) return(-1);

	if((pid = fork()) < 0)
	{
		// probably out of processes
		status = -1;
	}
	else if(pid == 0)
	{
		// child
		// restore previous signal actions & reset signal mask
		sigaction(SIGINT, &saveintr, NULL);
		sigaction(SIGQUIT, &savequit, NULL);
		sigprocmask(SIG_SETMASK, &savemask, NULL);

		execl("/bin/sh", "sh", "-c", cmdstring,(char *)0);
		// exec error
		_exit(127);
	}
	else
	{
		// parent
		while(waitpid(pid, &status, 0) < 0)
			if(errno != EINTR)
			{
				// error other than EINTR from waitpid()
				status = -1;
				break;
			}
	}

	// restore previous signal actions & reset signal mask
	if(sigaction(SIGINT, &saveintr, NULL) < 0) return(-1);
	if(sigaction(SIGQUIT, &savequit, NULL) < 0) return(-1);
	if(sigprocmask(SIG_SETMASK, &savemask, NULL) < 0) return(-1);
	return(status);
}

size_t writeCallback(void* dataPtr, size_t size, size_t nmemb, void* stream)
{
	size_t realsize						= size * nmemb;
	struct smithTestPlotResponse* mem	=(struct smithTestPlotResponse*) stream;

	// XXX: IMPORTANT - we are not storing response result becoz sometimes the size is too big
	// to allocate in memory
	// unused
	(void)dataPtr;
	(void)stream;

	/*mem->pszResponse = (char*) realloc(mem->pszResponse, (mem->size + realsize) * sizeof(char));
	if(mem->pszResponse == NULL)
	{
		// out of memory!
		logError("writeCallback not enough memory(realloc returned NULL)\n");
		return 0;
	}
	memcpy(&(mem->pszResponse[mem->size]), dataPtr, realsize);
	*/

	// XXX: IMPORTANT - mem->size initialized by 0 from caller, so don't worry
	mem->size += realsize;

	// mem->pszResponse[mem->size] = 0;

	return realsize;
}

struct smithTestPlotResponse* initCurl(struct smithTestPlotHttpResponse* chunk)
{
	struct smithTestPlotResponse* response = (struct smithTestPlotResponse*) malloc(sizeof(struct smithTestPlotResponse));

	// XXX: IMPORTANT - must initialize
	response->pszResponse			= NULL;
	response->size					= 0;
	response->dNameLookupTime		= 0.0;
	response->dConnectTime			= 0.0;
	response->dProcessRequiredTime	= 0.0;
	response->dAppConnectTime		= 0.0;
	response->dPreTransferTime		= 0.0;
	response->dStartTransferTime	= 0.0;

	// XXX: IMPORTANT initialize 'nCurlStatus' as negative value means curl error
	response->nCurlStatus = -1;

	//XXX: IMPORTANT - OAUTH request
	if(chunk->nAuthType == 2)
	{
		char*	pszReqUrl			= NULL;
		char*	pszPostarg			= NULL;
		char*	pszPostString		=(char*) malloc(2048);
		char*	pszReply			= NULL;

		// post method if 2
		if(chunk->nMethod == 2)
		{
			// add data in URI
			sprintf(pszPostString, "%s?%s", chunk->pszBaseAddress, chunk->pszQueryData);
			pszReqUrl = oauth_sign_url2(
				pszPostString, &pszPostarg, OA_HMAC, "POST",
				chunk->pszConsumerKey, chunk->pszConsumerSecret, chunk->pszToken, chunk->pszTokenSecret
			);

			pszReply = oauth_http_post2(pszReqUrl, pszPostarg, chunk->pszHeader, chunk->nResponseTimeoutLimit);
		}
		else
		{
			sprintf(pszPostString, "%s?%s", chunk->pszBaseAddress, chunk->pszQueryData);
			pszReqUrl = oauth_sign_url2(
				pszPostString, NULL, OA_HMAC, NULL, chunk->pszConsumerKey, chunk->pszConsumerSecret,
				chunk->pszToken, chunk->pszTokenSecret
			);

			pszReply = oauth_http_get2(pszReqUrl, NULL, chunk->pszHeader);
		}

		if(strcmp(pszReply, "") == 0)
		{
			logError("Error: initCurl - Possible server error!");
			if(pszReqUrl)	FREE(pszReqUrl);
			if(pszPostarg)	FREE(pszPostarg);
		}
		else
		{
#ifdef DEBUG
			logInfo("query:'%s'\n", pszReqUrl);
			logInfo("reply:'%s'\n", pszReply);
#endif

			if(pszReply) FREE(pszReply);
			if(pszReqUrl) FREE(pszReqUrl);
			if(pszPostarg) FREE(pszPostarg);
		}

		FREE(pszPostString);

		return response;
	}
	else
	{
		// XXX: IMPORTANT - plain URL request
		CURL* ch =  curl_easy_init();
		if(!ch) return response;

		struct curl_slist* headers = NULL;

		headers = curl_slist_append(headers, chunk->pszHeader);
		
		curl_easy_setopt(ch, CURLOPT_URL, chunk->pszBaseAddress);

		// XXX: IMPORTANT - if HTTPS required
		if(chunk->nType == 2)
		{
			// TODO: at this point we are not serving peer verification
			curl_easy_setopt(ch, CURLOPT_SSL_VERIFYPEER, 0L);

			curl_easy_setopt(ch, CURLOPT_SSL_VERIFYHOST, 1L);
		}


		// TODO: Proxy not supported
		// libcurl can be built to use: Basic, Digest, NTLM, Negotiate, GSS-Negotiate and SPNEGO
		// For convenience, you can use the 'CURLAUTH_ANY' define(instead of a list with specific types) which allows libcurl to use whatever method it wants
		if(chunk->nAuthType == 1)
		{
			char szUnamePwd[1024];
			char szCookieFileName[512];
			sprintf(szCookieFileName, "%u%s.txt", chunk->dwUsedId, chunk->pszPlotName);
			sprintf(szUnamePwd, "%s:%s", chunk->pszAuthUser, chunk->pszAuthPassword);
			curl_easy_setopt(ch, CURLOPT_USERPWD, szUnamePwd);
			curl_easy_setopt(ch, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
			curl_easy_setopt(ch, CURLOPT_COOKIEJAR, szCookieFileName);
		}

		curl_easy_setopt(ch, CURLOPT_FOLLOWLOCATION, 1);

		// XXX: IMPORTANT - post method if 2
		if(chunk->nMethod == 2)
		{
			curl_easy_setopt(ch, CURLOPT_POST, 1);
			curl_easy_setopt(ch, CURLOPT_POSTFIELDSIZE ,(long)strlen(chunk->pszQueryData));
			curl_easy_setopt(ch, CURLOPT_POSTFIELDS, chunk->pszQueryData);
		}

		curl_easy_setopt(ch, CURLOPT_HTTPHEADER, headers);

		curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, writeCallback);
		curl_easy_setopt(ch, CURLOPT_WRITEDATA, response);

		curl_easy_setopt(ch, CURLOPT_USERAGENT, "smith/1.0.0");
		curl_easy_setopt(ch, CURLOPT_TIMEOUT, chunk->nResponseTimeoutLimit);
		curl_easy_setopt(ch, CURLOPT_VERBOSE, CURL_DEBUG);

		// XXX: IMPORTANT - libcurl bug, if you not set as follows then you may face "longjmp causes uninitialized stack frame"
		curl_easy_setopt(ch, CURLOPT_NOSIGNAL, 1);
		response->nCurlStatus  = curl_easy_perform(ch);

		long lHttpCode = 0;
		curl_easy_getinfo(ch, CURLINFO_RESPONSE_CODE, &lHttpCode);

		response->lHttpCode				= lHttpCode;

		// XXX: to find out what kind of curl error happen, we used if-else block here
		if(response->nCurlStatus != CURLE_OK)
		{
			double		dVal;

			//logError("CURL error(from initCurl): %d\n", response->nCurlStatus);

			curl_easy_getinfo(ch, CURLINFO_TOTAL_TIME, &dVal);

			if(dVal > 0)
			{
				response->dProcessRequiredTime = dVal;
			}

			curl_easy_cleanup(ch);
			curl_slist_free_all(headers);

			return response;
		}
		else
		{
			double		dVal;

			// XXX: IMPORTANT - check for name resolution time
			curl_easy_getinfo(ch, CURLINFO_NAMELOOKUP_TIME, &dVal);

			if(dVal > 0)
			{
				response->dNameLookupTime = dVal;
			}

			// XXX: IMPORTANT - check for connect time
			curl_easy_getinfo(ch, CURLINFO_CONNECT_TIME, &dVal);

			if(dVal > 0)
			{
				response->dConnectTime = dVal;
			}

			// XXX: IMPORTANT - check for total time required for one request; Total Time = (dNameLookupTime + dConnectTime + process Time in Server)
			// in case of response->nIsDownloadCheck we can use this as total downloadTime
			curl_easy_getinfo(ch, CURLINFO_TOTAL_TIME, &dVal);

			if(dVal > 0)
			{
				response->dProcessRequiredTime = dVal;
			}

			// XXX: IMPORTANT - it took from the start until the file transfer is just about to begin
			curl_easy_getinfo(ch, CURLINFO_PRETRANSFER_TIME, &dVal);

			if(dVal > 0)
			{
				response->dPreTransferTime = dVal;
			}

			// XXX: IMPORTANT - it took from the start until the file transfer is just about to begin
			curl_easy_getinfo(ch, CURLINFO_STARTTRANSFER_TIME, &dVal);

			if(dVal > 0)
			{
				response->dStartTransferTime = dVal;
			}

			// XXX: set 'nIsDownloadCheck' from str httpRocket
			if(chunk->nIsDownloadCheck)
			{
				// XXX: IMPORTANT - check for bytes downloaded
				curl_easy_getinfo(ch, CURLINFO_SIZE_DOWNLOAD, &dVal);

				if(dVal > 0)
				{
					response->dBytesDownloaded = dVal;
				}

				// XXX: IMPORTANT - check for average download speed (measure in kbytes)
				curl_easy_getinfo(ch, CURLINFO_SPEED_DOWNLOAD, &dVal);

				if(dVal > 0)
				{
					response->dAvgDownloadSpeed = dVal / 1024;
				}
			}

			if(chunk->nType == 2)
			{
				// XXX: IMPORTANT - count required time to SSL/SSH connect/handshake to the remote host
				curl_easy_getinfo(ch, CURLINFO_APPCONNECT_TIME, &dVal);

				if(dVal > 0)
				{
					response->dAppConnectTime = dVal;
				}
			}

			curl_easy_cleanup(ch);
			curl_slist_free_all(headers);

			return response;
		}
	}
}

struct smithTestPlotResponse* initDownloadCurl(struct smithTestPlotHttpDownloadResponse* chunk)
{
	struct smithTestPlotResponse* response = (struct smithTestPlotResponse*) malloc(sizeof(struct smithTestPlotResponse));

	// XXX: IMPORTANT - must initialize
	response->pszResponse			= NULL;
	response->size					= 0;
	response->dNameLookupTime		= 0.0;
	response->dConnectTime			= 0.0;
	response->dProcessRequiredTime	= 0.0;
	response->dAppConnectTime		= 0.0;
	response->dPreTransferTime		= 0.0;
	response->dStartTransferTime	= 0.0;

	// XXX: IMPORTANT initialize 'nCurlStatus' as negative value means curl error
	response->nCurlStatus = -1;

	// XXX: IMPORTANT - plain URL request
	CURL* ch =  curl_easy_init();
	if(!ch) return response;

	curl_easy_setopt(ch, CURLOPT_URL, chunk->pszBaseAddress);

	// XXX: IMPORTANT - if HTTPS required
	if(chunk->nType == 2)
	{
		// TODO: at this point we are not serving peer verification
		curl_easy_setopt(ch, CURLOPT_SSL_VERIFYPEER, 0L);

		curl_easy_setopt(ch, CURLOPT_SSL_VERIFYHOST, 1L);
	}

	curl_easy_setopt(ch, CURLOPT_FOLLOWLOCATION, 1);

	curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, writeCallback);
	curl_easy_setopt(ch, CURLOPT_WRITEDATA, response);

	curl_easy_setopt(ch, CURLOPT_USERAGENT, "smith/1.0.0");
	curl_easy_setopt(ch, CURLOPT_TIMEOUT, chunk->nResponseTimeoutLimit);
	curl_easy_setopt(ch, CURLOPT_VERBOSE, CURL_DEBUG);

	// XXX: IMPORTANT - libcurl bug, if you not set as follows then you may face "longjmp causes uninitialized stack frame"
	curl_easy_setopt(ch, CURLOPT_NOSIGNAL, 1);
	response->nCurlStatus  = curl_easy_perform(ch);

	long lHttpCode = 0;
	curl_easy_getinfo(ch, CURLINFO_RESPONSE_CODE, &lHttpCode);

	response->lHttpCode				= lHttpCode;

	// XXX: to find out what kind of curl error happen, we used if-else block here
	if(response->nCurlStatus != CURLE_OK)
	{
		double		dVal;

		//logError("CURL error(from initCurl): %d\n", response->nCurlStatus);

		curl_easy_getinfo(ch, CURLINFO_TOTAL_TIME, &dVal);

		if(dVal > 0)
		{
			response->dProcessRequiredTime = dVal;
		}

		curl_easy_cleanup(ch);
		return response;
	}
	else
	{
		double		dVal;

		// XXX: IMPORTANT - check for name resolution time
		curl_easy_getinfo(ch, CURLINFO_NAMELOOKUP_TIME, &dVal);

		if(dVal > 0)
		{
			response->dNameLookupTime = dVal;
		}

		// XXX: IMPORTANT - check for connect time
		curl_easy_getinfo(ch, CURLINFO_CONNECT_TIME, &dVal);

		if(dVal > 0)
		{
			response->dConnectTime = dVal;
		}

		// XXX: IMPORTANT - check for total time required for one request; Total Time = (dNameLookupTime + dConnectTime + process Time in Server)
		// in case of response->nIsDownloadCheck we can use this as total downloadTime
		curl_easy_getinfo(ch, CURLINFO_TOTAL_TIME, &dVal);

		if(dVal > 0)
		{
			response->dProcessRequiredTime = dVal;
		}

		// XXX: IMPORTANT - it took from the start until the file transfer is just about to begin
		curl_easy_getinfo(ch, CURLINFO_PRETRANSFER_TIME, &dVal);

		if(dVal > 0)
		{
			response->dPreTransferTime = dVal;
		}

		// XXX: IMPORTANT - it took from the start until the file transfer is just about to begin
		curl_easy_getinfo(ch, CURLINFO_STARTTRANSFER_TIME, &dVal);

		if(dVal > 0)
		{
			response->dStartTransferTime = dVal;
		}

		// XXX: set 'nIsDownloadCheck' from str httpDownloadRocket
		if(chunk->nIsDownloadCheck)
		{
			// XXX: IMPORTANT - check for bytes downloaded
			curl_easy_getinfo(ch, CURLINFO_SIZE_DOWNLOAD, &dVal);

			if(dVal > 0)
			{
				response->dBytesDownloaded = dVal;
			}

			// XXX: IMPORTANT - check for average download speed (measure in kbytes)
			curl_easy_getinfo(ch, CURLINFO_SPEED_DOWNLOAD, &dVal);

			if(dVal > 0)
			{
				response->dAvgDownloadSpeed = dVal / 1024;
			}
		}

		if(chunk->nType == 2)
		{
			// XXX: IMPORTANT - count required time to SSL/SSH connect/handshake to the remote host
			curl_easy_getinfo(ch, CURLINFO_APPCONNECT_TIME, &dVal);

			if(dVal > 0)
			{
				response->dAppConnectTime = dVal;
			}
		}

		curl_easy_cleanup(ch);
		return response;
	}
}

struct smithTestPlotResponse* initUploadCurl(struct smithTestPlotHttpUploadResponse* chunk)
{
	struct	smithTestPlotResponse* response = (struct smithTestPlotResponse*) malloc(sizeof(struct smithTestPlotResponse));

	struct	curl_httppost* formpost	= NULL;
	struct	curl_httppost* lastptr	= NULL;
	int		nI;
	int		numItems;
	long	lHttpCode				= 0;

	// XXX: IMPORTANT - must initialize
	response->pszResponse			= NULL;
	response->size					= 0;
	response->dNameLookupTime		= 0.0;
	response->dConnectTime			= 0.0;
	response->dProcessRequiredTime	= 0.0;
	response->dAppConnectTime		= 0.0;
	response->dPreTransferTime		= 0.0;
	response->dStartTransferTime	= 0.0;

	// XXX: IMPORTANT initialize 'nCurlStatus' as negative value means curl error
	response->nCurlStatus = -1;

	// XXX: IMPORTANT - plain URL request
	CURL* ch =  curl_easy_init();
	if(!ch) return response;

	char** pszReturn = explodeString(chunk->pszQueryData, "&", &numItems);

	if(pszReturn != NULL)
	{
		for(nI=0; nI<numItems; nI++)
		{
			int		nElem;
			char**	pszQueryElem = explodeString(pszReturn[nI], "=", &nElem);

			if(strcmp(pszQueryElem[1], "File1") == 0)
			{
				curl_formadd(&formpost, &lastptr, CURLFORM_COPYNAME, pszQueryElem[0], CURLFORM_FILE, chunk->pszUploadFile, CURLFORM_END);
			}
			else
			{
				curl_formadd(&formpost, &lastptr, CURLFORM_COPYNAME, pszQueryElem[0], CURLFORM_COPYCONTENTS, pszQueryElem[1], CURLFORM_END);
			}

			freeExplodedString(pszQueryElem);
		}
	}
	else
	{
		// XXX: IMPORTANT - if only file upload (file=File1)
		pszReturn = explodeString(chunk->pszQueryData, "=", &numItems);

		if(!pszReturn)
		{
			logError("initUploadCurl - file not found in database for upload [%d]\n", __LINE__);
			return response;
		}

		if(strcmp(pszReturn[1], "File1") == 0)
		{
			curl_formadd(&formpost, &lastptr, CURLFORM_COPYNAME, pszReturn[0], CURLFORM_FILE, chunk->pszUploadFile, CURLFORM_END);
		}
		else
		{
			logError("initUploadCurl - file not found in database for upload [%d]\n", __LINE__);
			return response;
		}
	}

	freeExplodedString(pszReturn);

	curl_easy_setopt(ch, CURLOPT_URL, chunk->pszBaseAddress);

	// XXX: IMPORTANT - if HTTPS required
	if(chunk->nType == 2)
	{
		// TODO: at this point we are not serving peer verification
		curl_easy_setopt(ch, CURLOPT_SSL_VERIFYPEER, 0L);

		curl_easy_setopt(ch, CURLOPT_SSL_VERIFYHOST, 1L);
	}

	curl_easy_setopt(ch, CURLOPT_FOLLOWLOCATION, 1);

	curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, writeCallback);
	curl_easy_setopt(ch, CURLOPT_WRITEDATA, response);

	curl_easy_setopt(ch, CURLOPT_USERAGENT, "smith/1.0.0");
	curl_easy_setopt(ch, CURLOPT_TIMEOUT, chunk->nResponseTimeoutLimit);
	curl_easy_setopt(ch, CURLOPT_VERBOSE, CURL_DEBUG);

	// XXX: IMPORTANT - libcurl bug, if you not set as follows then you may face "longjmp causes uninitialized stack frame"
	curl_easy_setopt(ch, CURLOPT_NOSIGNAL, 1);

	curl_easy_setopt(ch, CURLOPT_HTTPPOST, formpost);

	response->nCurlStatus	= curl_easy_perform(ch);

	curl_easy_getinfo(ch, CURLINFO_RESPONSE_CODE, &lHttpCode);

	response->lHttpCode		= lHttpCode;

	// XXX: to find out what kind of curl error happen, we used if-else block here
	if(response->nCurlStatus != CURLE_OK)
	{
		double		dVal;

		//logError("CURL error(from initCurl): %d\n", response->nCurlStatus);

		curl_easy_getinfo(ch, CURLINFO_TOTAL_TIME, &dVal);

		if(dVal > 0)
		{
			response->dProcessRequiredTime = dVal;
		}

		curl_easy_cleanup(ch);
		curl_formfree(formpost);
		return response;
	}
	else
	{
		double		dVal;

		// XXX: IMPORTANT - check for name resolution time
		curl_easy_getinfo(ch, CURLINFO_NAMELOOKUP_TIME, &dVal);

		if(dVal > 0)
		{
			response->dNameLookupTime = dVal;
		}

		// XXX: IMPORTANT - check for connect time
		curl_easy_getinfo(ch, CURLINFO_CONNECT_TIME, &dVal);

		if(dVal > 0)
		{
			response->dConnectTime = dVal;
		}

		// XXX: IMPORTANT - check for total time required for one request; Total Time = (dNameLookupTime + dConnectTime + process Time in Server)
		// in case of response->nIsDownloadCheck we can use this as total downloadTime
		curl_easy_getinfo(ch, CURLINFO_TOTAL_TIME, &dVal);

		if(dVal > 0)
		{
			response->dProcessRequiredTime = dVal;
		}

		// XXX: IMPORTANT - it took from the start until the file transfer is just about to begin
		curl_easy_getinfo(ch, CURLINFO_PRETRANSFER_TIME, &dVal);

		if(dVal > 0)
		{
			response->dPreTransferTime = dVal;
		}

		// XXX: IMPORTANT - it took from the start until the file transfer is just about to begin
		curl_easy_getinfo(ch, CURLINFO_STARTTRANSFER_TIME, &dVal);

		if(dVal > 0)
		{
			response->dStartTransferTime = dVal;
		}

		// XXX: set 'nIsUploadCheck' from str httpUploadRocket
		if(chunk->nIsUploadCheck)
		{
			// XXX: IMPORTANT - check for average upload speed (measure in kbytes)
			curl_easy_getinfo(ch, CURLINFO_SPEED_UPLOAD, &dVal);

			if(dVal > 0)
			{
				response->dAvgUploadSpeed = dVal / 1024;
			}
		}

		if(chunk->nType == 2)
		{
			// XXX: IMPORTANT - count required time to SSL/SSH connect/handshake to the remote host
			curl_easy_getinfo(ch, CURLINFO_APPCONNECT_TIME, &dVal);

			if(dVal > 0)
			{
				response->dAppConnectTime = dVal;
			}
		}

		curl_easy_cleanup(ch);
		curl_formfree(formpost);
		return response;
	}
}

/**
 * POSIX level write lock
 */
int writeLockFile(int nFd)
{
	struct flock stF1;
	stF1.l_type = F_WRLCK;
	stF1.l_start = 0;
	stF1.l_whence = SEEK_SET;
	stF1.l_len = 0;
	return(fcntl(nFd, F_SETLK, &stF1));
}

/**
 * int nFd = safeOpen("test.dat", O_CREAT | O_RDWR, 0640);
 */
int safeOpen(const char* pszPath, int nFlags, mode_t mtMode)
{
	int nFd;
	nFd = open(pszPath, nFlags, mtMode);
	if(nFd == -1)
	{
		logError("safeOpen: open %s failed in %s at %d\n", pszPath, __FILE__, __LINE__);
	}
	return nFd;
}

ssize_t safeBwrite(int nFd, const uint8_t* byBuff, size_t stCount)
{
	ssize_t sstRetval;
	sstRetval = write(nFd, byBuff, stCount);
	if(sstRetval == -1)
	{
		logError("safeBwrite: write %l bytes to fd %d failed in %s at %d\n", stCount, nFd, __FILE__, __LINE__);
	}
	return sstRetval;
}

/**
 * Like strdup but make sure the resulting string perfectly duplicated.
 */
char* safeStrdup(const char* pszString)
{
	char* pszRetval;
	pszRetval = strdup(pszString);
	if(!pszRetval)
	{
		logError("safeStrdup: dup %s failed in %s at %d\n", pszString, __FILE__, __LINE__);
		return(NULL);
	}
	return pszRetval;
}

/**
 * Function: thread-safe implementation of sleep
 * Purpose: generate a delay mechanism in second
 */
unsigned sSleep(unsigned int nSecs)
{
	int nI;
	unsigned uSlept;
	time_t ttStart, ttEnd;
	struct timeval tv;
	tv.tv_sec = nSecs;
	tv.tv_usec = 0;
	time(&ttStart);
	nI = select(0, NULL, NULL, NULL, &tv);
	if(nI == 0) return(0);
	time(&ttEnd);
	uSlept = ttEnd - ttStart;
	if(uSlept >= nSecs) return(0);
	return(nSecs - uSlept);
}

/**
 * generate a delay mechanism in milisecond
 */
void mSleep(int nMsecs)
{
	struct timeval tv;
	tv.tv_sec =(int)(nMsecs / 1000);
	tv.tv_usec =(nMsecs % 1000) * 1000;
	select(0, NULL, NULL, NULL, &tv);
}

/**
 * generate a delay mechanism in microsecond
 */
void uSleep(unsigned int nUsecs)
{
	struct timeval	tv;
	tv.tv_sec = nUsecs / 1000000;
	tv.tv_usec = nUsecs % 1000000;
	select(0, NULL, NULL, NULL, &tv);
}

/**
 * calculate microsecond
 */
double getMicroSecond()
{
	struct timeval	tvCurrent;

	//Get current.
	if(gettimeofday(&tvCurrent, NULL) == 0)
	{
		//Calculate microsecond.
		return(double)(tvCurrent.tv_usec / MICRO_SEC);
	}
	//if gettimeofday is failed then
	return(0.0);
}

/**
 * calculate microsecond
 */
double getMilliSecond()
{
	struct timeval	tvCurrent;

	//Get current.
	if(gettimeofday(&tvCurrent, NULL) == 0)
	{
		//Calculate microsecond.
		return(double)(tvCurrent.tv_usec / MILLI_SEC);
	}
	//if gettimeofday is failed then
	return(0.0);
}

/**
 * Check if the file exist on the system without open it
 * @pszFilename A path to the file to check
 * @return TRUE if file exist otherwise FALSE
 */
int fileExist(char* pszFilename)
{
	struct stat buff;
	ASSERT(pszFilename);
	return(stat(pszFilename, &buff) == 0);
}

/**
 * Check if the file is a regular file
 * @param pszFilename A path to the file to check
 * @return TRUE if file exist and is a regular file, otherwise FALSE
 */
int isFile(char* pszFilename)
{
	struct stat buff;
	ASSERT(pszFilename);
	return(stat(pszFilename, &buff) == 0 && S_ISREG(buff.st_mode));
}

/**
 * Check if this is a directory.
 * @param pszDirName An absolute  directory path
 * @return TRUE if directory exist and is a regular directory, otherwise
 * FALSE
 */
int isDirectory(char* pszDirName)
{
	struct stat buff;
	ASSERT(pszDirName);
	return(stat(pszDirName, &buff) == 0 && S_ISDIR(buff.st_mode));
}

/**
 * Replace all occurrences of the sub-string old in the string src
 * with the sub-string new. The method is case sensitive for the
 * sub-strings new and old. The string parameter src must be an
 * allocated string, not a character array.
 * @param pszHayStack An allocated string reference(e.g. &string)
 * @param pszSearch The old sub-string
 * @param pszReplace The new sub-string
 * @return pszHayStack where all occurrences of the old sub-string are
 * replaced with the new sub-string.
 * MUST FREE in caller side and MUST place pszReplace with some data.
 */
char* strReplace(const char* pszSearch, const char* pszReplace, const char* pszHayStack)
{
	char* tok		= NULL;
	char* newstr	= NULL;
	char* oldstr	= NULL;
	char* head		= NULL;

	/* if either pszSearch or pszReplace is NULL, duplicate string a let caller handle it */
	if(pszSearch == NULL || pszReplace == NULL) return strdup(pszHayStack);

	newstr = strdup(pszHayStack);
	head = newstr;
	while((tok = strstr(head, pszSearch)))
	{
		oldstr = newstr;
		newstr = xMalloc(strlen(oldstr) - strlen(pszSearch) + strlen(pszReplace) + 1);
		/*failed to alloc mem, free old string and return NULL */
		if(newstr == NULL)
		{
			FREE(oldstr);
			return NULL;
		}
		memcpy(newstr, oldstr, tok - oldstr);
		memcpy(newstr +(tok - oldstr), pszReplace, strlen(pszReplace));
		memcpy(newstr +(tok - oldstr) + strlen(pszReplace), tok + strlen(pszSearch), strlen(oldstr) - strlen(pszSearch) -(tok - oldstr));
		memset(newstr + strlen(oldstr) - strlen(pszSearch) + strlen(pszReplace) , 0, 1);
		/* move back head right after the last pszReplace */
		head = newstr +(tok - oldstr) + strlen(pszReplace);
		FREE(oldstr);
	}

	return newstr;
}

int readTheWholeFile(char* pszFname, char* pszArray)
{
	FILE* 	fp;
	char	szTemp[515] = {0};
	int		nSize = 0;
	// Can we open the file?
	if((fp = fopen(pszFname, "r")) == NULL)
	{
		return(-1);
	}

	// Start reading the file
	while((fgets(szTemp, 512, fp) !=(char*)NULL))
	{
		nSize += sprintf(&pszArray[nSize], "%s", szTemp);
	}

	fclose(fp);
	return nSize;
}

void* xMalloc(int nI)
{
	void* p;
    p =(void*) malloc(nI);
    //Some malloc's don't return a valid pointer if you malloc(0), so check for that only if necessary.
#if ! HAVE_MALLOC
		if(nI == 0)
		{
			logError("xMalloc passed a broken malloc 0 in %s at %d", __FILE__, __LINE__);
			return NULL;
		}
#endif
    if(p == NULL)
    {
    	logError("xMalloc Failed to initialize in %s at %d", __FILE__, __LINE__);
    	return NULL;
    }
    return p;
}

uint32_t string2Int(uint8_t* byString, size_t stStringSize)
{
	uint32_t	dwRetval;
	uint32_t	dwI;
	for(dwI = 0, dwRetval = 0; dwI < stStringSize; dwI++, byString++)
		dwRetval =(dwRetval << 8) | *byString;
	return dwRetval;
}

/**
 * Explode a string
 * @param - pszString source string to be expl@ded
 * @param - pszSep seperator string
 * @param - nItems it's just a counter to contain num. of exploded element(s)
 * @return - character pointer array contain NULL if error or no match pszSep
 * otherwise array of string.
 *
	char* pszTestString = "HELLO boy I am Fine boy thats great boy how about a boy and girl";
	int num_items;
	char** pszReturn = explodeString(pszTestString, "boy", &num_items);
	printf("Explode Elem: %d\n", num_items);
	if(pszReturn != NULL){
		for(nI=0; nI< num_items; nI++){
			printf("Result: %s\n", pszReturn[nI]);
		}
	}
	freeExplodedString(pszReturn);
 */
char** explodeString(char* pszString, char* pszSep, int* nItems)
{
    int nI;
    int nNumItems;
    char** pszRetArray;
    char* pszPtr;
    char* pszPtr1;
    char* pszPtr2;

    if(nItems != NULL) *nItems = 0;
    if(!pszString || !pszString[0]) return NULL;
    //calculate number of items
    pszPtr = pszString;
    nI = 1;
    int bIsSepExist = FALSE;
    while((pszPtr = strstr(pszPtr, pszSep)))
    {
    	bIsSepExist = TRUE;
        while(strchr(pszSep, pszPtr[0]) != NULL)
            pszPtr++;
        nI++;
    }
    if(!bIsSepExist)
    {
    	*nItems = 0;
    	return NULL;
    }
    nNumItems = nI;
    pszRetArray =(char**) xMalloc((nNumItems + 1) * sizeof(char *));
    if(NULL == pszRetArray)
    {
    	*nItems = 0;
    	logError("Unable to allocate memory at %d in %s\n", __LINE__, __FILE__);
    	return NULL;
    }
    pszPtr1 = pszString;
    pszPtr2 = pszString;
    for(nI = 0; nI<nNumItems; nI++)
    {
        while(strchr(pszSep, pszPtr1[0]) != NULL) pszPtr1++;
        if(nI ==(nNumItems - 1) ||(pszPtr2 = strstr(pszPtr1, pszSep)) == NULL)
            if((pszPtr2 = strchr(pszPtr1, '\r')) == NULL)
                if((pszPtr2 = strchr(pszPtr1, '\n')) == NULL)
                    pszPtr2 = strchr(pszPtr1, '\0');
        if((pszPtr1 == NULL) ||(pszPtr2 == NULL))
        {
            pszRetArray[nI] = NULL;
        }
        else
        {
            if(pszPtr2 - pszPtr1 > 0)
            {
                pszRetArray[nI] =(char *) xMalloc((pszPtr2 - pszPtr1 + 1) * sizeof(char));
                pszRetArray[nI] = strncpy(pszRetArray[nI], pszPtr1, pszPtr2 - pszPtr1);
                pszRetArray[nI][pszPtr2 - pszPtr1] = '\0';
                pszPtr1 = ++pszPtr2;
            }
            else
            {
                pszRetArray[nI] = NULL;
            }
        }
    }
    pszRetArray[nI] = NULL;
    if(nItems != NULL) *nItems = nI;

    return pszRetArray;
}

/**
 * free_exploded_string: free an exploded string
 */
void freeExplodedString(char** pszString)
{
    int nI;
    if(pszString)
    {
    	for(nI=0; pszString[nI]; nI++) FREE(pszString[nI]);
        FREE(pszString);
    }
}

// XXX: IMPORTANT - must FREEP the 'space' in caller side
char* formattedString(const char* fmt, ...)
{
    va_list	args;
    size_t	len;
    char*	pszSpace;

    va_start(args, fmt);
    len = vsnprintf(0, 0, fmt, args);
    va_end(args);
    if((pszSpace = malloc(len + 1)) != 0)
    {
         va_start(args, fmt);
         vsnprintf(pszSpace, len+1, fmt, args);
         va_end(args);
         return pszSpace;
         //free(space);
    }
    else
    {
    	// malloc failed
    	return NULL;
    }
}

/**
 * a new random number will be produced every time
 */
int randomNumber(int minNum, int maxNum)
{
	int result	= 0;
	int nLowNum	= 0;
	int nHiNum	= 0;

	if(minNum < maxNum)
	{
		nLowNum	= minNum;
		// this is done to include maxNum in output.
		nHiNum	= maxNum + 1;
	}
	else
	{
		// this is done to include maxNum in output.
		nLowNum	= maxNum + 1;
		nHiNum	= minNum;
	}

	srand(time(NULL));
	result =(rand()%(nHiNum-nLowNum)) + nLowNum;

	return result;
}

/**
 * Split a URL into its respective parts
 * @param url The URL to split
 */
urlst*	parseUrl(const char* url)
{
	char *tmp1, *tmp2, *tmp3, *tmp4;
	char *end;
	char *protocol;
	char *username, *password;
	char *host, *port;
	char *path;

	urlst* result;

	result = calloc(1, sizeof(urlst));

	if(!result) return NULL;

	if(strstrsplit(url, "://", &protocol, &tmp1))
	{
		protocol	= strdup("");
		tmp1		= strdup(url);
	}

	if(strchrsplit(tmp1, '/', &tmp2, &path))
	{
		tmp2 = strdup(tmp1);
		path = strdup("");
	}

	if(strchrsplit(tmp2, '@', &tmp3, &tmp4))
	{
		tmp3 = strdup("");
		tmp4 = strdup(tmp2);
	}

	if(strchrsplit(tmp3, ':', &username, &password))
	{
		username = strdup(tmp3);
		password = strdup("");
	}

	// Parse IPv4 and IPv6 host+port fields differently
	if(tmp4[0] == '[')
	{
		result->nIpv6Host = 1;

		end = strchr(tmp4 + 1, ']');

		if(end)
		{
			if(strpchrsplit(tmp4, end, ':', &host, &port))
			{
				host = strdup(tmp4);
				port = strdup("");
			}

			memmove(host, host + 1, end - tmp4 - 1);
			host[end - tmp4 - 1] = '\0';
		}
		else
		{
			host = strdup(tmp4 + 1);
			port = strdup("");
		}
	}
	else
	{
		result->nIpv6Host = 0;

		if(strrchrsplit(tmp4, ':', &host, &port))
		{
			host = strdup(tmp4);
			port = strdup("");
		}
	}

	free(tmp1);
	free(tmp2);
	free(tmp3);
	free(tmp4);

	result->pszProtocol = protocol;
	result->pszUsername = username;
	result->pszPassword = password;
	result->pszHost		= host;
	result->pszPort		= port;
	result->pszPath		= path;

	return result;
}

void freeUrl(urlst* url)
{
	free(url->pszProtocol);
	free(url->pszUsername);
	free(url->pszPassword);
	free(url->pszHost);
	free(url->pszPort);
	free(url->pszPath);
	free(url);
}

char* parseQueryString(char* string, char* pszQueryString)
{
    if(pszQueryString)
    {
        if(strstr(pszQueryString, string))
        {
            char*			pszInitWord = NULL;
            char*			pszWord		= NULL;
            unsigned int	strtype		= TRUE;
            int				nStropt		= 0;

            strcpy(pszInitWord, pszQueryString);
            pszWord = strtok(pszInitWord, "&=");

            while(pszWord)
            {
                if(strtype == TRUE)
                {
                    if(strstr(pszWord, string))
                    {
                        nStropt = 1;
                    }

                    strtype = FALSE;
                }
                else
                {
                    if(nStropt == 1)
                    {
                        return pszWord;
                        break;
                    }

                    strtype = TRUE;
                }

                pszWord = strtok(NULL, "&=");
            }
        }
        else
        {
            return "";
        }
    }
    else
    {
        return "";
    }

    return "";
}

/**
 * Split a string by the given substring.
 * @param str The string to split.
 * @param sep The separator substring.
 * @param former_result The first part(before the separator).
 * @param latter_result The last part(after the separator).
 * @return True on error, otherwise false.
 */
int strstrsplit(const char *str, const char *sep, char **former_result, char **latter_result)
{
	char *split;
	char *former, *latter;

	split = strstr(str, sep);

	if(!split)
	{
		return 1;
	}

	former = malloc(split - str + 1);

	if(!former)
	{
		return 1;
	}

	strncpy(former, str, split - str);
	former[split - str] = '\0';

	latter = strdup(split + strlen(sep));

	*former_result = former;
	*latter_result = latter;
	return 0;
}

/**
 * Split a string by the first occurence of the given character.
 * @param str The string to split.
 * @param sep The separator character.
 * @param former_result The first part(before the separator).
 * @param latter_result The last part(after the separator).
 * @return True on error, otherwise false.
 */
int strchrsplit(const char *str, const char sep, char **former_result, char **latter_result)
{
	char *split;
	char *former, *latter;

	split = strchr(str, sep);

	if(!split)
	{
		return 1;
	}

	former = malloc(split - str + 1);
	if(!former)
	{
		return 1;
	}

	strncpy(former, str, split - str);
	former[split - str] = '\0';

	latter = strdup(split + 1);

	*former_result = former;
	*latter_result = latter;
	return 0;
}

/**
 * Split a string by the last occurence of the given character.
 * @param str The string to split.
 * @param sep The separator character.
 * @param former_result The first part(before the separator).
 * @param latter_result The last part(after the separator).
 * @return True on error, otherwise false.
 */
int strrchrsplit(const char *str, const char sep, char **former_result, char **latter_result)
{
	char *split;
	char *former, *latter;

	split = strrchr(str, sep);

	if(!split)
	{
		return 1;
	}

	former = malloc(split - str + 1);

	if(!former)
	{
		return 1;
	}

	strncpy(former, str, split - str);
	former[split - str] = '\0';

	latter = strdup(split + 1);

	*former_result = former;
	*latter_result = latter;
	return 0;
}

/**
 * Split a string by the given occurence of the given character.
 * @param str The string to split.
 * @param pos The position to search from.
 * @param sep The separator character.
 * @param former_result The first part(before the separator).
 * @param latter_result The last part(after the separator).
 * @return True on error, otherwise false.
 */
int strpchrsplit(const char *str, const char *pos, const char sep, char **former_result, char **latter_result)
{
	char *split;
	char *former, *latter;

	split = strchr(pos, sep);

	if(!split)
	{
		return 1;
	}

	former = malloc(split - str + 1);

	if(!former)
	{
		return 1;
	}

	strncpy(former, str, split - str);
	former[split - str] = '\0';

	latter = strdup(split + 1);

	*former_result = former;
	*latter_result = latter;
	return 0;
}

char* escapeShellArg(char* str)
{
	int x;
	int y = 0;
	int l = strlen(str);
	char* cmd;
	size_t estimate = (4 * l) + 3;

	cmd			= (char*) calloc(l + 3, sizeof(char));
	cmd[y++]	= '\'';

	for(x = 0; x < l; x++)
	{
		switch(str[x])
		{
			case '\'':
				cmd[y++] = '\'';
				cmd[y++] = '\\';
				cmd[y++] = '\'';
				// fall-through

			default:
				cmd[y++] = str[x];
		}
	}

	cmd[y++] = '\'';
	cmd[y] = '\0';

	if((estimate - y) > 4096)
	{
		/* realloc if the estimate was way overill
		 * Arbitrary cutoff point of 4096 */
		cmd = (char*) realloc(cmd, y+1);
	}

	return cmd;
}
