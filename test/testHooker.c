#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <curl/curl.h>
#include <curl/easy.h>
#include "../common.h"
#include "../log.h"
#include "../hook.h"
#include "../helper.h"


void httpRocket(void* data)
{
	struct smithTestPlotHttpResponse* chunk = (struct smithTestPlotHttpResponse*) data;

	struct smithTestPlotResponse* response = initCurl(chunk);

	if(response->nCurlStatus == CURLE_OK)
	{
		logInfo("Response size: %d\n", response->size);
	}
	else if(response->nCurlStatus == CURLE_OPERATION_TIMEDOUT)
	{
		// TODO:
	}
	else
	{
		return;
	}

	//response->pszResponse = (char*) realloc(response->pszResponse, (response->size + 1) * sizeof(char));
	//response->pszResponse[response->size] = '\0';
	//free(response->pszResponse);
	free(response);

	//response = NULL;

	return;
}

int main()
{
	UDWORD uNoOfThreadToRun = 50;
	int nJ;
	int nI;
	int ret;
	pthread_attr_t scopeAttr;
	void* statusp;
	HOOKER hooker;

	strcpy(g_szLogFileName, "/var/log/testHooker.log");

	if((hooker = newHooker(uNoOfThreadToRun, uNoOfThreadToRun, FALSE)) == NULL)
	{
		printf("Failed to create a thread pool struct\n");
		return TRUE;
	}

	struct	smithTestPlotHttpResponse* HTTP_STR_SPECIFIC_TEST;

	HTTP_STR_SPECIFIC_TEST = (struct smithTestPlotHttpResponse*) malloc(uNoOfThreadToRun * sizeof(struct smithTestPlotHttpResponse));

	for(nI=0; nI<uNoOfThreadToRun; nI++)
	{
		HTTP_STR_SPECIFIC_TEST[nI].size						= 0;
		HTTP_STR_SPECIFIC_TEST[nI].nType					= 1;
		HTTP_STR_SPECIFIC_TEST[nI].nMethod					= 1;
		HTTP_STR_SPECIFIC_TEST[nI].nContentType				= 1;
		HTTP_STR_SPECIFIC_TEST[nI].nAccept					= 1;
		HTTP_STR_SPECIFIC_TEST[nI].nCharset					= 1;
		HTTP_STR_SPECIFIC_TEST[nI].pszBaseAddress			= strdup("simpleso.jp");
		HTTP_STR_SPECIFIC_TEST[nI].pszQueryData				= NULL;
		HTTP_STR_SPECIFIC_TEST[nI].nResponseTimeoutLimit	= 5;
		HTTP_STR_SPECIFIC_TEST[nI].nAuthType				= 0;
		HTTP_STR_SPECIFIC_TEST[nI].pszAuthUser				= strdup("admin");
		HTTP_STR_SPECIFIC_TEST[nI].pszAuthPassword			= strdup("money#+Admin");
		HTTP_STR_SPECIFIC_TEST[nI].pszConsumerKey			= NULL;
		HTTP_STR_SPECIFIC_TEST[nI].pszConsumerSecret		= NULL;
		HTTP_STR_SPECIFIC_TEST[nI].pszToken					= NULL;
		HTTP_STR_SPECIFIC_TEST[nI].pszTokenSecret			= NULL;

		// set HTTP header
		char* pszHeader = formattedString(
			"%s; charset = %s\r\n%s\r\n",
			SMITH_CONTENT_TYPE[HTTP_STR_SPECIFIC_TEST[nI].nContentType], SMITH_CHARSET[HTTP_STR_SPECIFIC_TEST[0].nCharset], SMITH_ACCEPT[HTTP_STR_SPECIFIC_TEST[0].nAccept]
		);

		HTTP_STR_SPECIFIC_TEST[nI].pszHeader = strdup(pszHeader);
		FREEP(pszHeader);
	}

	/**
	 * for each concurrent user, spawn a thread and
	 * loop until condition or pthread_cancel from the
	 * handler thread.
	*/
	pthread_attr_init(&scopeAttr);
	pthread_attr_setscope(&scopeAttr, PTHREAD_SCOPE_SYSTEM);

	for(nJ=0; nJ<uNoOfThreadToRun && hookerGetShutdown(hooker)!=TRUE; nJ++)
	{
		ret = hookerAdd(hooker, httpRocket, &HTTP_STR_SPECIFIC_TEST[nJ]);

		if(ret == FALSE)
		{
			logError(
				"An error had occurred while adding a task thread[%d]\n"
				"Unable to spawn additional threads; you may need to\nupgrade your libraries"
				"or tune your system in order\nto exceed %d users\n", nJ, uNoOfThreadToRun
			);
		}
	}

	hookerJoin(hooker, TRUE, &statusp);

	/**
	* collect all the data from all the threads that
	* were spawned by the run.
	*/
	printf("Start checking is pool is empty or not\n");

	for(nJ=0; nJ<((hookerGetTotal(hooker) > uNoOfThreadToRun || hookerGetTotal(hooker)==0) ? uNoOfThreadToRun : hookerGetTotal(hooker)); nJ++)
	{
		// TODO:
		printf("Collecting response data\n");
	}

	// XXX: IMPORTANT - cleanup hooker
	hookerDestroy(hooker);

	// XXX: IMPORTANT - take a short nap  for cosmetic effect this does NOT affect performance stats.
	uSleep(10000);

	return 0;
}
