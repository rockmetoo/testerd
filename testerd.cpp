/*
 * watch -n 1 'netstat -an | grep :6536 | wc -l'
 * ngrep -q -d lo -W byline host localhost and port 6536
 * sudo sysctl -w net.core.somaxconn=4096
 * sudo sysctl -w net.ipv4.tcp_tw_recycle=1
 * sudo sysctl -w net.ipv4.tcp_tw_reuse=1
 * sudo ulimit -s unlimited
 * sudo ulimit -n 4096
 * sudo ulimit -u 2048
 * sudo ulimit -v unlimited
 * sudo ulimit -x unlimited
 * sudo ulimit -m unlimited
 */

#include "testerd.h"

#include <stdio.h>
#include <curl/curl.h>
#include <curl/easy.h>
#include <exception>
#include <mysql/mysql.h>
#include <mongoc.h>
#include <bson.h>
#include <sys/resource.h>

#include "log.h"
#include "helper.h"
#include "threadPool.h"
#include "iniparser.h"
#include "encrypt.h"
#include "mysqlwrap.h"
#include "hook.h"
#include "jsonlib/jansson.h"

#define BUF_SIZE					10*1024
#define ASCIILINESZ					(1024)
#define	COPYRIGHT					"©Data Trench. All rights reserved"
#define MAX_CMD_SIZE				1024

static				sigset_t		mask;

bool				bIsDaemon						= TRUE;
UWORD				gwPort							= 5555;
static char*		gpszProtocol;
char				gHostAddr[255]					= {0};
pid_t				gDaemonPid;
uid_t				gDaemonUid;
gid_t				gDaemonGid;
char*				SMITH_THREAD_RUNNER_PID_FILE	= new char[1024];
char*				SMITH_THREAD_RUNNER_CONFIG_FILE	= new char[1024];
struct threadpool*	tPoolWorker						= NULL;

int					nHttp							= 0;

int					gSTRThreadRunning				= 0;
int					gSTRThreadLeft					= 0;

int 				gListOfSmithd					= 0;
int 				gListOfThreadRunner				= 0;
int					gSTRId							= 0;
int					gNumOfPlotReportDB				= 0;

char				gConnection[256];

static				pthread_mutex_t	STR_THREAD_STAT_MUTEX;

static				pthread_mutex_t HTTP_SMITH_MUTEX[MAX_SMITH_PLOT];

void sigHUP(int nSigno);
void sigTERM(int nSigno);
void sigQUIT(int nSigno);
void httpRocket(void* data);
void httpDownloadRocket(void* data);
void httpUploadRocket(void* data);
void workerRoutine(void* arguments);
void sendSTRStatusToAllSmithd();
void sendSTRStatusToAllSmithdExcept(int smithdId);
void sendSTRThreadDoneStatusToSpecificSmithd(int nsmithdId, UWORD uNoOfThreadExecuted, UDWORD userId, char* pszPlotName, char* pszPlotType);
void incrementThreadLeft(int uNoOfThreadToRun);
void deccrementThreadLeft(int uNoOfThreadToRemove);
int parseConfigFile(char* pszFileName);

str::str(){}

str::~str(){}

BOOL str::onRecvCommand()
{
	struct timeval tvStart;
	gettimeofday(&tvStart, NULL);

	UDWORD dwPos = 0;

	m_byCmd = *(int*)&m_recvData[dwPos];
	dwPos += 4;

	switch(m_byCmd)
	{
		case	SMITH_THREAD_RUNNER_CMD_INDEX_HTTP:
			if(onRecvRunTestHttpPlot()==FALSE) return FALSE;
			break;

		case	SMITH_THREAD_RUNNER_CMD_INDEX_STATUS:
			if(onRecvReturnDaemonStatus()==FALSE) return FALSE;
			break;

		case	SMITH_THREAD_RUNNER_CMD_INDEX_HTTP_DL:
			if(onRecvRunTestHttpDownloadPlot()==FALSE) return FALSE;
			break;

		case	SMITH_THREAD_RUNNER_CMD_INDEX_HTTP_UL:
			if(onRecvRunTestHttpUploadPlot()==FALSE) return FALSE;
			break;

		default:
			logError("Unknown command detected as val: %d at line %d\n", m_byCmd, __LINE__);
			return FALSE;
	}

	struct timeval tvFinished;
	gettimeofday(&tvFinished, NULL);
	float fDiff =
	(
		(tvFinished.tv_sec * 100 + tvFinished.tv_usec / 10000)
		- (tvStart.tv_sec * 100 + tvStart.tv_usec / 10000)
	)/100.0f;

	if(fDiff >= 0.5f) logInfo("Command: %u | Spent time: %.2fsec at line %d\n", m_byCmd, fDiff, __LINE__);

	return TRUE;
}

BOOL str::onRecvReturnDaemonStatus()
{
	#define REPLY_STATUS(s, r, l, b) \
		UBYTE* m_sendData = new UBYTE[b]; \
		dwPos = 0; \
		*(int*)&m_sendData[dwPos] = s; \
		dwPos += 4; \
		*(int*)&m_sendData[dwPos] = r; \
		dwPos += 4; \
		*(int*)&m_sendData[dwPos] = l; \
		dwPos += 4; \
		zmq::message_t reply(dwPos);  \
		memcpy(reply.data(), m_sendData, dwPos); \
		m_socket->send(reply); \
		FREEP(m_sendData);

	UDWORD dwPos			= 0;

	logDebug("signal received for status\n");

	REPLY_STATUS(1, gSTRThreadRunning, gSTRThreadLeft, 1024)

	return TRUE;
}

BOOL str::onRecvRunTestHttpPlot()
{
	#define REPLY_RUN_HTTP_PLOT(s, r, l, b) \
		UBYTE* m_sendData = new UBYTE[b]; \
		dwPos = 0; \
		*(int*)&m_sendData[dwPos] = s; \
		dwPos += 4; \
		*(int*)&m_sendData[dwPos] = r; \
		dwPos += 4; \
		*(int*)&m_sendData[dwPos] = l; \
		dwPos += 4; \
		zmq::message_t reply(dwPos);  \
		memcpy(reply.data(), m_sendData, dwPos); \
		m_socket->send(reply); \
		FREEP(m_sendData);

	int status				= -1;
	UDWORD dwPos			= 0;
	char szPlotName[255+1]	= {0};

	// XXX: IMPORTANT - command parameter skiped start
	dwPos += 4;
	// XXX: IMPORTANT - command parameter skiped end

	// Data Recv Start
	char* pszUserId = (char*)&m_recvData[dwPos];
	dwPos += strlen((char*)&m_recvData[dwPos]) + 1;
	UDWORD dwUsedId = (UDWORD) strtoul(pszUserId, NULL, 10);

	char* pszPlotName = (char*)&m_recvData[dwPos];
	dwPos += strlen((char*)&m_recvData[dwPos]) + 1;
	strcpy(szPlotName, pszPlotName);

	UDWORD uNoOfThreadToRun = *(UDWORD*)&m_recvData[dwPos];
	dwPos += 4;

	int nPlotReportDbIndex = *(int*)&m_recvData[dwPos];
	dwPos += 4;

	logDebug("HERERRER: %d nPlotReportDbIndex: %d  uNoOfThreadToRun: %u\n", __LINE__, nPlotReportDbIndex, uNoOfThreadToRun);

	int nWhichSmithdRequested = *(int*)&m_recvData[dwPos];
	dwPos += 4;
	// Data Recv End

	if(dwUsedId && pszPlotName)
	{
		Database db(MYSQL_HOST_ACCESS, MYSQL_USER_ACCESS, MYSQL_PASS_ACCESS, MYSQL_DB_ACCESS);
		Query q(db);

		// XXX: IMPORTANT - move the test plot in 'testing' mode
		q.DoQuery(1024, "UPDATE `smith`.`httpTestPlot` SET plotStatus=2 WHERE userId='%u' AND plotName='%s'", dwUsedId, pszPlotName);

		logDebug("Plot name %s is going to test for user %lu\n", pszPlotName, dwUsedId);

		// XXX: IMPORTANT - engage some test job and that's why we need to decrement the gSTRThreadLeft
		deccrementThreadLeft(uNoOfThreadToRun);

		// XXX: IMPORTANT - successful command with status
		REPLY_RUN_HTTP_PLOT(1, gSTRThreadRunning, gSTRThreadLeft, 1024)

		// XXX: IMPORTANT - update the status to all smithd except 'nWhichSmithdRequested'
		sendSTRStatusToAllSmithdExcept(nWhichSmithdRequested);

		// XXX: IMPORTANT - start doing actual test job here
		HOOKER hooker;

		// Create a hooker of 'uNoOfThreadToRun' thread workers
		if((hooker = newHooker(uNoOfThreadToRun, uNoOfThreadToRun, FALSE)) == NULL)
		{
			logError("Failed to create a thread pool struct\n");
			return TRUE;
		}

		struct smithTestPlotHttpResponse HTTP_STR_SPECIFIC_TEST;

		char	szQuery[1024]	= {0};

		sprintf(szQuery, "SELECT * FROM `smith`.`httpTestPlot` WHERE userId='%u' AND plotName='%s'", dwUsedId, pszPlotName);
		q.get_result(szQuery);

		if(q.GetErrno())
		{
			logInfo("SELECT * FROM `smith`.`httpTestPlot` SQL Error at %d\n", __LINE__);
			/**
			 * TODO: task is not completed properly so we need to store pszUserId, pszPlotName, pszPlotType
			 * in a separate table called 'unfinishedTestJob' and another process will check it and do the task
			 */
			// XXX: IMPORTANT - so far error happen we need to give back the 'gSTRThreadLeft' status into previous stage
			incrementThreadLeft(uNoOfThreadToRun);

			return TRUE;
		}
		else
		{
			if(q.num_rows() < 1)
			{
				logError("data not found in httpTestPlot table for user '%u' and plotName '%s' [%d]\n", dwUsedId, pszPlotName, __LINE__);
				return TRUE;
			}

			while(q.fetch_row())
			{
				HTTP_STR_SPECIFIC_TEST.dwUsedId					= dwUsedId;
				HTTP_STR_SPECIFIC_TEST.pszPlotName				= strdup(pszPlotName);
				HTTP_STR_SPECIFIC_TEST.size						= 0;
				HTTP_STR_SPECIFIC_TEST.nType					= (int) q.getval("type");
				HTTP_STR_SPECIFIC_TEST.nMethod					= (int) q.getval("method");
				HTTP_STR_SPECIFIC_TEST.nContentType				= (int) q.getval("contentType");
				HTTP_STR_SPECIFIC_TEST.nAccept					= (int) q.getval("accept");
				HTTP_STR_SPECIFIC_TEST.nCharset					= (int) q.getval("charset");
				HTTP_STR_SPECIFIC_TEST.pszBaseAddress			= strdup(q.getstr("baseAddress"));
				HTTP_STR_SPECIFIC_TEST.pszQueryData				= strdup(q.getstr("queryData"));
				HTTP_STR_SPECIFIC_TEST.nResponseTimeoutLimit	= (int) q.getval("responseTimeoutLimit");
				HTTP_STR_SPECIFIC_TEST.nAuthType				= (int) q.getval("authType");
				HTTP_STR_SPECIFIC_TEST.pszAuthUser				= strdup(q.getstr("authUser"));
				HTTP_STR_SPECIFIC_TEST.pszAuthPassword			= strdup(q.getstr("authPassword"));
				HTTP_STR_SPECIFIC_TEST.pszConsumerKey			= strdup(q.getstr("consumerKey"));
				HTTP_STR_SPECIFIC_TEST.pszConsumerSecret		= strdup(q.getstr("consumerSecret"));
				HTTP_STR_SPECIFIC_TEST.pszToken					= strdup(q.getstr("token"));
				HTTP_STR_SPECIFIC_TEST.pszTokenSecret			= strdup(q.getstr("tokenSecret"));
				HTTP_STR_SPECIFIC_TEST.nIsDownloadCheck			= 1;
				HTTP_STR_SPECIFIC_TEST.nPlotReportDbIndex		= nPlotReportDbIndex;

				// set HTTP header
				char* pszHeader = formattedString(
					"%s; charset = %s\r\n%s\r\n",
					SMITH_CONTENT_TYPE[HTTP_STR_SPECIFIC_TEST.nContentType], SMITH_CHARSET[HTTP_STR_SPECIFIC_TEST.nCharset], SMITH_ACCEPT[HTTP_STR_SPECIFIC_TEST.nAccept]
				);

				HTTP_STR_SPECIFIC_TEST.pszHeader = strdup(pszHeader);
				FREEP(pszHeader);
			}
		}

		q.free_result();

		int nJ, ret;
		pthread_attr_t scopeAttr;
		void* statusp;

		/**
		 * for each concurrent user, spawn a thread and
		 * loop until condition or pthread_cancel from the
		 * handler thread.
		*/
		pthread_attr_init(&scopeAttr);
		pthread_attr_setscope(&scopeAttr, PTHREAD_SCOPE_SYSTEM);

		for(nJ=0; nJ<uNoOfThreadToRun && hookerGetShutdown(hooker)!=TRUE; nJ++)
		{
			ret = hookerAdd(hooker, httpRocket, &HTTP_STR_SPECIFIC_TEST);

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
		for(nJ=0; nJ<((hookerGetTotal(hooker) > uNoOfThreadToRun || hookerGetTotal(hooker)==0) ? uNoOfThreadToRun : hookerGetTotal(hooker)); nJ++)
		{
			// TODO: cleanup
		}

		// XXX: IMPORTANT - so far test job done then we need to give back the 'gSTRThreadLeft' status into previous stage
		incrementThreadLeft(uNoOfThreadToRun);

		// XXX: IMPORTANT - send status to specific smithd that it's done so that it combine and change the status to done
		sendSTRThreadDoneStatusToSpecificSmithd(nWhichSmithdRequested, uNoOfThreadToRun, dwUsedId, pszPlotName, "http");

		// XXX: IMPORTANT - update the status to all smithd except 'nWhichSmithdRequested'
		sendSTRStatusToAllSmithdExcept(nWhichSmithdRequested);

		// XXX: IMPORTANT - cleanup hooker
		hookerDestroy(hooker);

		// XXX: IMPORTANT - take a short nap  for cosmetic effect this does NOT affect performance stats.
		uSleep(10000);
	}
	else
	{
		// unsuccessful command with status
		REPLY_RUN_HTTP_PLOT(0, gSTRThreadRunning, gSTRThreadLeft, 1024)
		logDebug("Http test plot Input data is invalid!!! [%d]\n", __LINE__);
	}

	return TRUE;
}

BOOL str::onRecvRunTestHttpDownloadPlot()
{
	#define REPLY_RUN_HTTP_DL_PLOT(s, r, l, b) \
		UBYTE* m_sendData = new UBYTE[b]; \
		dwPos = 0; \
		*(int*)&m_sendData[dwPos] = s; \
		dwPos += 4; \
		*(int*)&m_sendData[dwPos] = r; \
		dwPos += 4; \
		*(int*)&m_sendData[dwPos] = l; \
		dwPos += 4; \
		zmq::message_t reply(dwPos);  \
		memcpy(reply.data(), m_sendData, dwPos); \
		m_socket->send(reply); \
		FREEP(m_sendData);

	int status				= -1;
	UDWORD dwPos			= 0;
	char szPlotName[255+1]	= {0};

	// XXX: IMPORTANT - command parameter skiped start
	dwPos += 4;
	// XXX: IMPORTANT - command parameter skiped end

	// Data Recv Start
	char* pszUserId = (char*)&m_recvData[dwPos];
	dwPos += strlen((char*)&m_recvData[dwPos]) + 1;
	UDWORD dwUsedId = (UDWORD) strtoul(pszUserId, NULL, 10);

	char* pszPlotName = (char*)&m_recvData[dwPos];
	dwPos += strlen((char*)&m_recvData[dwPos]) + 1;
	strcpy(szPlotName, pszPlotName);

	UDWORD uNoOfThreadToRun = *(UDWORD*)&m_recvData[dwPos];
	dwPos += 4;

	int nPlotReportDbIndex = *(int*)&m_recvData[dwPos];
	dwPos += 4;

	logDebug("HERERRER: %d nPlotReportDbIndex: %d  uNoOfThreadToRun: %u\n", __LINE__, nPlotReportDbIndex, uNoOfThreadToRun);

	int nWhichSmithdRequested = *(int*)&m_recvData[dwPos];
	dwPos += 4;
	// Data Recv End

	if(dwUsedId && pszPlotName)
	{
		Database db(MYSQL_HOST_ACCESS, MYSQL_USER_ACCESS, MYSQL_PASS_ACCESS, MYSQL_DB_ACCESS);
		Query q(db);

		// XXX: IMPORTANT - move the test plot in 'testing' mode
		q.DoQuery(1024, "UPDATE `smith`.`httpDownloadTestPlot` SET plotStatus=2 WHERE userId='%u' AND plotName='%s'", dwUsedId, pszPlotName);

		logDebug("Plot name %s is going to test for user %lu\n", pszPlotName, dwUsedId);

		// XXX: IMPORTANT - engage some test job and that's why we need to decrement the gSTRThreadLeft
		deccrementThreadLeft(uNoOfThreadToRun);

		// XXX: IMPORTANT - successful command with status
		REPLY_RUN_HTTP_DL_PLOT(1, gSTRThreadRunning, gSTRThreadLeft, 1024)

		// XXX: IMPORTANT - update the status to all smithd except 'nWhichSmithdRequested'
		sendSTRStatusToAllSmithdExcept(nWhichSmithdRequested);

		// XXX: IMPORTANT - start doing actual test job here
		HOOKER hooker;

		// Create a hooker of 'uNoOfThreadToRun' thread workers
		if((hooker = newHooker(uNoOfThreadToRun, uNoOfThreadToRun, FALSE)) == NULL)
		{
			logError("Failed to create a thread pool struct\n");
			return TRUE;
		}

		struct	smithTestPlotHttpDownloadResponse HTTP_STR_SPECIFIC_TEST;

		char	szQuery[1024]	= {0};

		sprintf(szQuery, "SELECT * FROM `smith`.`httpDownloadTestPlot` WHERE userId='%u' AND plotName='%s'", dwUsedId, pszPlotName);
		q.get_result(szQuery);

		if(q.GetErrno())
		{
			logInfo("SELECT * FROM `smith`.`httpDownloadTestPlot` SQL Error at %d\n", __LINE__);
			/**
			 * TODO: task is not completed properly so we need to store pszUserId, pszPlotName, pszPlotType
			 * in a separate table called 'unfinishedTestJob' and another process will check it and do the task
			 */
			// XXX: IMPORTANT - so far error happen we need to give back the 'gSTRThreadLeft' status into previous stage
			incrementThreadLeft(uNoOfThreadToRun);

			return TRUE;
		}
		else
		{
			if(q.num_rows() < 1)
			{
				logError("data not found in httpDownloadTestPlot table for user '%u' and plotName '%s' [%d]\n", dwUsedId, pszPlotName, __LINE__);
				return TRUE;
			}

			while(q.fetch_row())
			{
				HTTP_STR_SPECIFIC_TEST.dwUsedId					= dwUsedId;
				HTTP_STR_SPECIFIC_TEST.pszPlotName				= strdup(pszPlotName);
				HTTP_STR_SPECIFIC_TEST.size						= 0;
				HTTP_STR_SPECIFIC_TEST.nType					= (int) q.getval("type");
				HTTP_STR_SPECIFIC_TEST.pszBaseAddress			= strdup(q.getstr("baseAddress"));
				HTTP_STR_SPECIFIC_TEST.nResponseTimeoutLimit	= (int) q.getval("responseTimeoutLimit");
				HTTP_STR_SPECIFIC_TEST.nIsDownloadCheck			= 1;
				HTTP_STR_SPECIFIC_TEST.nPlotReportDbIndex		= nPlotReportDbIndex;
			}
		}

		q.free_result();

		int nJ, ret;
		pthread_attr_t scopeAttr;
		void* statusp;

		/**
		 * for each concurrent user, spawn a thread and
		 * loop until condition or pthread_cancel from the
		 * handler thread.
		*/
		pthread_attr_init(&scopeAttr);
		pthread_attr_setscope(&scopeAttr, PTHREAD_SCOPE_SYSTEM);

		for(nJ=0; nJ<uNoOfThreadToRun && hookerGetShutdown(hooker)!=TRUE; nJ++)
		{
			ret = hookerAdd(hooker, httpDownloadRocket, &HTTP_STR_SPECIFIC_TEST);

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
		for(nJ=0; nJ<((hookerGetTotal(hooker) > uNoOfThreadToRun || hookerGetTotal(hooker)==0) ? uNoOfThreadToRun : hookerGetTotal(hooker)); nJ++)
		{
			// TODO: cleanup
		}

		// XXX: IMPORTANT - so far test job done then we need to give back the 'gSTRThreadLeft' status into previous stage
		incrementThreadLeft(uNoOfThreadToRun);

		// XXX: IMPORTANT - send status to specific smithd that it's done so that it combine and change the status to done
		sendSTRThreadDoneStatusToSpecificSmithd(nWhichSmithdRequested, uNoOfThreadToRun, dwUsedId, pszPlotName, "httpdl");

		// XXX: IMPORTANT - update the status to all smithd except 'nWhichSmithdRequested'
		sendSTRStatusToAllSmithdExcept(nWhichSmithdRequested);

		// XXX: IMPORTANT - cleanup hooker
		hookerDestroy(hooker);

		// XXX: IMPORTANT - take a short nap  for cosmetic effect this does NOT affect performance stats.
		uSleep(10000);
	}
	else
	{
		// unsuccessful command with status
		REPLY_RUN_HTTP_DL_PLOT(0, gSTRThreadRunning, gSTRThreadLeft, 1024)
		logDebug("Download test plot Input data is invalid!!! [%d]\n", __LINE__);
	}

	return TRUE;
}

BOOL str::onRecvRunTestHttpUploadPlot()
{
	#define REPLY_RUN_HTTP_UL_PLOT(s, r, l, b) \
		UBYTE* m_sendData = new UBYTE[b]; \
		dwPos = 0; \
		*(int*)&m_sendData[dwPos] = s; \
		dwPos += 4; \
		*(int*)&m_sendData[dwPos] = r; \
		dwPos += 4; \
		*(int*)&m_sendData[dwPos] = l; \
		dwPos += 4; \
		zmq::message_t reply(dwPos);  \
		memcpy(reply.data(), m_sendData, dwPos); \
		m_socket->send(reply); \
		FREEP(m_sendData);

	int status				= -1;
	UDWORD dwPos			= 0;
	char szPlotName[255+1]	= {0};

	// XXX: IMPORTANT - command parameter skiped start
	dwPos += 4;
	// XXX: IMPORTANT - command parameter skiped end

	// Data Recv Start
	char* pszUserId = (char*)&m_recvData[dwPos];
	dwPos += strlen((char*)&m_recvData[dwPos]) + 1;
	UDWORD dwUsedId = (UDWORD) strtoul(pszUserId, NULL, 10);

	char* pszPlotName = (char*)&m_recvData[dwPos];
	dwPos += strlen((char*)&m_recvData[dwPos]) + 1;
	strcpy(szPlotName, pszPlotName);

	UDWORD uNoOfThreadToRun = *(UDWORD*)&m_recvData[dwPos];
	dwPos += 4;

	int nPlotReportDbIndex = *(int*)&m_recvData[dwPos];
	dwPos += 4;

	logDebug("HERERRER: %d nPlotReportDbIndex: %d  uNoOfThreadToRun: %u\n", __LINE__, nPlotReportDbIndex, uNoOfThreadToRun);

	int nWhichSmithdRequested = *(int*)&m_recvData[dwPos];
	dwPos += 4;
	// Data Recv End

	if(dwUsedId && pszPlotName)
	{
		Database db(MYSQL_HOST_ACCESS, MYSQL_USER_ACCESS, MYSQL_PASS_ACCESS, MYSQL_DB_ACCESS);
		Query q(db);

		// XXX: IMPORTANT - move the test plot in 'testing' mode
		q.DoQuery(1024, "UPDATE `smith`.`httpUploadTestPlot` SET plotStatus=2 WHERE userId='%u' AND plotName='%s'", dwUsedId, pszPlotName);

		logDebug("Plot name %s is going to test for user %lu\n", pszPlotName, dwUsedId);

		// XXX: IMPORTANT - engage some test job and that's why we need to decrement the gSTRThreadLeft
		deccrementThreadLeft(uNoOfThreadToRun);

		// XXX: IMPORTANT - successful command with status
		REPLY_RUN_HTTP_UL_PLOT(1, gSTRThreadRunning, gSTRThreadLeft, 1024)

		// XXX: IMPORTANT - update the status to all smithd except 'nWhichSmithdRequested'
		sendSTRStatusToAllSmithdExcept(nWhichSmithdRequested);

		// XXX: IMPORTANT - start doing actual test job here
		HOOKER hooker;

		// Create a hooker of 'uNoOfThreadToRun' thread workers
		if((hooker = newHooker(uNoOfThreadToRun, uNoOfThreadToRun, FALSE)) == NULL)
		{
			logError("Failed to create a thread pool struct [%d]\n", __LINE__);
			return TRUE;
		}

		struct smithTestPlotHttpUploadResponse HTTP_STR_SPECIFIC_TEST;

		char	szQuery[1024]	= {0};

		sprintf(szQuery, "SELECT * FROM `smith`.`httpUploadTestPlot` WHERE userId='%u' AND plotName='%s'", dwUsedId, pszPlotName);
		q.get_result(szQuery);

		if(q.GetErrno())
		{
			logInfo("SELECT * FROM `smith`.`httpUploadTestPlot` SQL Error [%d]\n", __LINE__);
			/**
			 * TODO: task is not completed properly so we need to store pszUserId, pszPlotName, pszPlotType
			 * in a separate table called 'unfinishedTestJob' and another process will check it and do the task
			 */
			// XXX: IMPORTANT - so far error happen we need to give back the 'gSTRThreadLeft' status into previous stage
			incrementThreadLeft(uNoOfThreadToRun);

			return TRUE;
		}
		else
		{
			if(q.num_rows() < 1)
			{
				logError("data not found in httpUploadTestPlot table for user '%u' and plotName '%s' [%d]\n", dwUsedId, pszPlotName, __LINE__);
				return TRUE;
			}

			while(q.fetch_row())
			{
				HTTP_STR_SPECIFIC_TEST.dwUsedId					= dwUsedId;
				HTTP_STR_SPECIFIC_TEST.pszPlotName				= strdup(pszPlotName);
				HTTP_STR_SPECIFIC_TEST.size						= 0;
				HTTP_STR_SPECIFIC_TEST.nType					= (int) q.getval("type");
				HTTP_STR_SPECIFIC_TEST.pszBaseAddress			= strdup(q.getstr("baseAddress"));
				char szUploadedFile[1024]						= {0};

				sprintf(szUploadedFile, "%s%s", UPLOAD_FILE_DIR, strdup(q.getstr("File1")));

				HTTP_STR_SPECIFIC_TEST.pszUploadFile			= szUploadedFile;
				HTTP_STR_SPECIFIC_TEST.pszQueryData				= strdup(q.getstr("queryData"));

				HTTP_STR_SPECIFIC_TEST.nResponseTimeoutLimit	= (int) q.getval("responseTimeoutLimit");
				HTTP_STR_SPECIFIC_TEST.nIsUploadCheck			= 1;
				HTTP_STR_SPECIFIC_TEST.nPlotReportDbIndex		= nPlotReportDbIndex;
			}
		}

		q.free_result();

		int nJ, ret;
		pthread_attr_t scopeAttr;
		void* statusp;

		/**
		 * for each concurrent user, spawn a thread and
		 * loop until condition or pthread_cancel from the
		 * handler thread.
		*/
		pthread_attr_init(&scopeAttr);
		pthread_attr_setscope(&scopeAttr, PTHREAD_SCOPE_SYSTEM);

		for(nJ=0; nJ<uNoOfThreadToRun && hookerGetShutdown(hooker)!=TRUE; nJ++)
		{
			ret = hookerAdd(hooker, httpUploadRocket, &HTTP_STR_SPECIFIC_TEST);

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
		for(nJ=0; nJ<((hookerGetTotal(hooker) > uNoOfThreadToRun || hookerGetTotal(hooker)==0) ? uNoOfThreadToRun : hookerGetTotal(hooker)); nJ++)
		{
			// TODO: cleanup
		}

		// XXX: IMPORTANT - so far test job done then we need to give back the 'gSTRThreadLeft' status into previous stage
		incrementThreadLeft(uNoOfThreadToRun);

		// XXX: IMPORTANT - send status to specific smithd that it's done so that it combine and change the status to done
		sendSTRThreadDoneStatusToSpecificSmithd(nWhichSmithdRequested, uNoOfThreadToRun, dwUsedId, pszPlotName, "httpul");

		// XXX: IMPORTANT - update the status to all smithd except 'nWhichSmithdRequested'
		sendSTRStatusToAllSmithdExcept(nWhichSmithdRequested);

		// XXX: IMPORTANT - cleanup hooker
		hookerDestroy(hooker);

		// XXX: IMPORTANT - take a short nap  for cosmetic effect this does NOT affect performance stats.
		uSleep(10000);
	}
	else
	{
		// unsuccessful command with status
		REPLY_RUN_HTTP_UL_PLOT(0, gSTRThreadRunning, gSTRThreadLeft, 1024)
		logDebug("Upload test plot Input data is invalid!!! [%d]\n", __LINE__);
	}

	return TRUE;
}

/**
 * daemon daemonize()
 */
static int daemoNize(void)
{
	int nFd;
	int nI;
	pid_t pid;
	struct rlimit srR1;
	struct sigaction srSa;
	// clear file creation mask
	umask(0);
	// get maximum number of file descriptors
	if(getrlimit(RLIMIT_NOFILE, &srR1) < 0)
	{
		logError("smith daemon failed to maximum no. of file descriptor in %s [%d]\n", __FILE__, __LINE__);
		return (-1);
	}
	// become a session leader to lose controlling TTY
	if((pid = fork()) < 0)
	{
		logError("smith daemon failed to become session leader in %s [%d]\n", __FILE__, __LINE__);
		return (-1);
	}
	else if(pid != 0)
	{//parent
		_exit(0);
	}

	setsid();

	// ensure future opens won't allocate controlling TTYs
	srSa.sa_handler = SIG_IGN;
	sigemptyset(&srSa.sa_mask);
	srSa.sa_flags = 0;
	if(sigaction(SIGHUP, &srSa, NULL) < 0)
	{
		//TODO: LOG something
		return (-1);
	}
	if((pid = fork()) < 0)
	{
		//TODO: LOG something
		return (-1);
	}
	else if(pid != 0)
	{//parent
		_exit(0);
	}

	int status = chdir("/");

	// close all open file descriptors
	if(srR1.rlim_max == RLIM_INFINITY) srR1.rlim_max = 1024;
	for(nI=0; nI<srR1.rlim_max; nI++) close(nI);
	if((nFd = open(PATH_DEVNULL, O_RDWR)) == -1)
	{
		logError("smith daemon failed to open /dev/null in %s [%d]\n", __FILE__, __LINE__);
		return (-1);
	}
	(void)dup2(nFd, STDIN_FILENO);
	(void)dup2(nFd, STDOUT_FILENO);
	(void)dup2(nFd, STDERR_FILENO);
	return (0);
}

/**
 * Perform any termination cleanup and exit the server with the exit status
 * This function does not return.
 */
static void daemonExit(int nCode)
{
	struct stat st;
	if(stat((const char*)SMITH_THREAD_RUNNER_PID_FILE, &st) == 0)
	{
		logNotice("strd unlinking PID file %s in %s [%d]\n", SMITH_THREAD_RUNNER_PID_FILE, __FILE__, __LINE__);

		if(unlink((const char*)SMITH_THREAD_RUNNER_PID_FILE) == -1)
		{
			logCritical("strd failed to unlink PID file %s in %s [%d]\n", SMITH_THREAD_RUNNER_PID_FILE, __FILE__, __LINE__);
		}
	}

	threadpoolFree(tPoolWorker, 0);
	pthread_mutex_destroy(&STR_THREAD_STAT_MUTEX);
	logNotice("strd exiting... at %s [%d]\n", __FILE__, __LINE__);
	exit(nCode);
}

/**
 * send STR status to all smithd
 */
void sendSTRStatusToAllSmithd()
{
	int nI;
	UDWORD dwPos;
	char szControlCode[128] = {0};
	char key[17]			= {0};

	// encrypt the command
	sprintf(szControlCode, "%d", SMITH_CMD_INDEX_UPDATE_STR_STATUS);
	strcpy(key, COMMAND_KEY);
	plainlen = strlen(szControlCode);
	ascii_encrypt128(szControlCode, key);

	for(nI=0; nI<gListOfSmithd; nI++)
	{
		zmq::context_t	smithdContext(1);
		zmq::socket_t	smithdSocket(smithdContext, ZMQ_REQ);
		int linger = 0;
		smithdSocket.connect(szListOfSmithd[nI]);
		smithdSocket.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));

		UBYTE*			smithdRepData	= new UBYTE[1024];
		UBYTE*			smithdReqData	= new UBYTE[1024];

		dwPos = 8;

		strcpy((char*)&smithdReqData[dwPos], ascCipherText);
		dwPos += strlen((char*)&smithdReqData[dwPos]) + 1;

		// STR id
		*(int*)&smithdReqData[dwPos] = gSTRId;
		dwPos += 4;

		// STR status
		*(int*)&smithdReqData[dwPos] = 1;
		dwPos += 4;

		*(int*)&smithdReqData[dwPos] = gSTRThreadRunning;
		dwPos += 4;

		*(int*)&smithdReqData[dwPos] = gSTRThreadLeft;
		dwPos += 4;

		zmq::message_t req(dwPos);
		memcpy(req.data(), smithdReqData, dwPos);
		smithdSocket.send(req);

		// Get the reply from smithd
		zmq::message_t reply;
		smithdSocket.recv(&reply);
		smithdRepData = (UBYTE*) reply.data();

		dwPos = 0;
		int nSmithdStatus			= *(int*)&smithdRepData[dwPos];
		dwPos += 4;

		if(!nSmithdStatus) logError("smithd-%d not updated properly by strd status [%d]\n", nI, __LINE__);

		smithdSocket.close();
		smithdRepData 	= NULL;
		smithdReqData	= NULL;
	}
}

/**
 * send STR status to all smithd except 'smithdId'
 */
void sendSTRStatusToAllSmithdExcept(int smithdId)
{
	int nI;
	UDWORD dwPos;
	char szControlCode[128] = {0};
	char key[17]			= {0};

	// encrypt the command
	sprintf(szControlCode, "%d", SMITH_CMD_INDEX_UPDATE_STR_STATUS);
	strcpy(key, COMMAND_KEY);
	plainlen = strlen(szControlCode);
	ascii_encrypt128(szControlCode, key);

	for(nI=0; nI<gListOfSmithd; nI++)
	{
		if(smithdId == nI) continue;

		zmq::context_t	smithdContext(1);
		zmq::socket_t	smithdSocket(smithdContext, ZMQ_REQ);
		int linger = 0;
		smithdSocket.connect(szListOfSmithd[nI]);
		smithdSocket.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));

		UBYTE*			smithdRepData	= new UBYTE[1024];
		UBYTE*			smithdReqData	= new UBYTE[1024];

		dwPos = 8;

		strcpy((char*)&smithdReqData[dwPos], ascCipherText);
		dwPos += strlen((char*)&smithdReqData[dwPos]) + 1;

		// STR id
		*(int*)&smithdReqData[dwPos] = gSTRId;
		dwPos += 4;

		// STR status
		*(int*)&smithdReqData[dwPos] = 1;
		dwPos += 4;

		*(int*)&smithdReqData[dwPos] = gSTRThreadRunning;
		dwPos += 4;

		*(int*)&smithdReqData[dwPos] = gSTRThreadLeft;
		dwPos += 4;

		zmq::message_t req(dwPos);
		memcpy(req.data(), smithdReqData, dwPos);
		smithdSocket.send(req);

		// Get the reply from smithd
		zmq::message_t reply;
		smithdSocket.recv(&reply);
		smithdRepData = (UBYTE*) reply.data();

		dwPos = 0;
		int nSmithdStatus			= *(int*)&smithdRepData[dwPos];
		dwPos += 4;

		if(!nSmithdStatus) logError("smithd-%d not updated properly by strd status [%d]\n", nI, __LINE__);

		smithdSocket.close();
		smithdRepData 	= NULL;
		smithdReqData	= NULL;
	}
}

/**
 * send STR status to specific smithd
 */
void sendSTRThreadDoneStatusToSpecificSmithd(int nsmithdId, UWORD uNoOfThreadExecuted, UDWORD userId, char* pszPlotName, char* pszPlotType)
{
	UDWORD dwPos;
	char szControlCode[128] = {0};
	char key[17]			= {0};

	// encrypt the command
	sprintf(szControlCode, "%d", SMITH_CMD_INDEX_STR_THREAD_DONE);
	strcpy(key, COMMAND_KEY);
	plainlen = strlen(szControlCode);
	ascii_encrypt128(szControlCode, key);

	zmq::context_t	smithdContext(1);
	zmq::socket_t	smithdSocket(smithdContext, ZMQ_REQ);
	int linger = 0;
	smithdSocket.connect(szListOfSmithd[nsmithdId]);
	smithdSocket.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));

	UBYTE*			smithdRepData	= new UBYTE[1024];
	UBYTE*			smithdReqData	= new UBYTE[1024];

	dwPos = 8;

	strcpy((char*)&smithdReqData[dwPos], ascCipherText);
	dwPos += strlen((char*)&smithdReqData[dwPos]) + 1;

	// STR id
	*(int*)&smithdReqData[dwPos] = gSTRId;
	dwPos += 4;

	// STR status
	*(int*)&smithdReqData[dwPos] = 1;
	dwPos += 4;

	*(int*)&smithdReqData[dwPos] = gSTRThreadRunning;
	dwPos += 4;

	*(int*)&smithdReqData[dwPos] = gSTRThreadLeft;
	dwPos += 4;

	*(int*)&smithdReqData[dwPos] = uNoOfThreadExecuted;
	dwPos += 4;

	*(UDWORD*)&smithdReqData[dwPos] = userId;
	dwPos += 4;

	strcpy((char*)&smithdReqData[dwPos], pszPlotName);
	dwPos += strlen((char*)&smithdReqData[dwPos]) + 1;

	strcpy((char*)&smithdReqData[dwPos], pszPlotType);
	dwPos += strlen((char*)&smithdReqData[dwPos]) + 1;


	logDebug("plotName: %s and plotType: %s\n", pszPlotName, pszPlotType);

	zmq::message_t req(dwPos);
	memcpy(req.data(), smithdReqData, dwPos);
	smithdSocket.send(req);

	// Get the reply from smithd
	zmq::message_t reply;
	smithdSocket.recv(&reply);
	smithdRepData = (UBYTE*) reply.data();

	dwPos = 0;
	int nSmithdStatus			= *(int*)&smithdRepData[dwPos];
	dwPos += 4;

	if(!nSmithdStatus) logError("smithd-%d not updated properly by strd status [%d]\n", nsmithdId, __LINE__);

	smithdSocket.close();
	smithdRepData 	= NULL;
	smithdReqData	= NULL;
}

void incrementThreadLeft(int uNoOfThreadToRun)
{
	pthread_mutex_lock(&STR_THREAD_STAT_MUTEX);
	gSTRThreadRunning = gSTRThreadRunning - uNoOfThreadToRun;
	gSTRThreadLeft += uNoOfThreadToRun;
	pthread_mutex_unlock(&STR_THREAD_STAT_MUTEX);
}

void deccrementThreadLeft(int uNoOfThreadToRemove)
{
	pthread_mutex_lock(&STR_THREAD_STAT_MUTEX);
	gSTRThreadRunning += uNoOfThreadToRemove;
	gSTRThreadLeft = gSTRThreadLeft - uNoOfThreadToRemove;
	pthread_mutex_unlock(&STR_THREAD_STAT_MUTEX);
}

int parseConfigFile(char* pszFileName)
{
    dictionary* ini;

    int             nI;
    char*			pszSecname = NULL;
    char			szKeym[ASCIILINESZ + 1];

    ini = iniparser_load(pszFileName);

    if(ini == NULL)
    {
    	logError("Cannot parse strd config file %s [%d]\n", pszFileName, __LINE__);
        daemonExit(1);
    }

    int nSec = iniparser_getnsec(ini);

    for(nI=0 ; nI<nSec ; nI++)
    {
    	json_t*			root;
    	json_error_t	error;

    	pszSecname = iniparser_getsecname(ini, nI);

		PLOT_KEY(szKeym, pszSecname, "smithdServers")
		char* SMITHD_SERVERS = new char[1024];
		strcpy(SMITHD_SERVERS, iniparser_getstring(ini, szKeym, NULL));

		root = json_loads(SMITHD_SERVERS, 0, &error);

		if(!root)
		{
			logError("Parsing the JSON array from 'smithdServers' failed in %s [%d]\n", error.text, error.line);
			root = NULL;
			json_decref(root);
			daemonExit(1);
		}

		gListOfSmithd = json_array_size(root);

		if(gListOfSmithd < 1)
		{
			logError("no smithd configuration in strd config file %s [%d]\n", pszFileName, __LINE__);
			daemonExit(1);
		}

		for(int nII=0; nII<gListOfSmithd; nII++)
		{
			json_t* smithdServers;

			smithdServers = json_array_get(root, nII);

			szListOfSmithd[nII] = new char[256];
			strcpy(szListOfSmithd[nII], json_string_value(smithdServers));

			smithdServers = NULL;
		}

		PLOT_KEY(szKeym, pszSecname, "strdServers")
		char* STRD_SERVERS = new char[1024];
		strcpy(STRD_SERVERS, iniparser_getstring(ini, szKeym, NULL));

		root = json_loads(STRD_SERVERS, 0, &error);

		if(!root)
		{
			logError("Parsing the JSON array from 'strdServers' failed in %s [%d]\n", error.text, error.line);
			root = NULL;
			json_decref(root);
			daemonExit(1);
		}

		gListOfThreadRunner = json_array_size(root);

		if(gListOfThreadRunner < 1)
		{
			logError("no strd configuration in strd config file %s [%d]\n", pszFileName, __LINE__);
			daemonExit(1);
		}

		for(int nII=0; nII<gListOfThreadRunner; nII++)
		{
			json_t* strdServers;

			strdServers = json_array_get(root, nII);

			szListOfThreadRunner[nII] = new char[256];
			strcpy(szListOfThreadRunner[nII], json_string_value(strdServers));

			strdServers = NULL;
		}

		PLOT_KEY(szKeym, pszSecname, "plotReportDB")
		char* REPORT_DB_SERVERS = new char[1024];
		strcpy(REPORT_DB_SERVERS, iniparser_getstring(ini, szKeym, NULL));

		root = json_loads(REPORT_DB_SERVERS, 0, &error);

		if(!root)
		{
			logError("Parsing the JSON array from 'plotReportDB' failed in %s [%d]\n", error.text, error.line);
			root = NULL;
			json_decref(root);
			daemonExit(1);
		}

		gNumOfPlotReportDB = json_array_size(root);

		if(gNumOfPlotReportDB < 1)
		{
			logError("no plotReportDB configuration in strd config file %s [%d]\n", pszFileName, __LINE__);
			daemonExit(1);
		}

		// XXX: IMPORTANT - set NULL in 0 index. we choose random number from 1
		for(int nII=1; nII<gNumOfPlotReportDB; nII++)
		{
			json_t* plotReportDB;

			plotReportDB = json_array_get(root, nII);

			szPlotReportDb[nII] = new char[256];
			strcpy(szPlotReportDb[nII], json_string_value(plotReportDB));

			plotReportDB = NULL;
		}

		FREEP(SMITHD_SERVERS);
		FREEP(STRD_SERVERS);
		FREEP(REPORT_DB_SERVERS);

		json_decref(root);
    }

    iniparser_freedict(ini);
    return 1;
}

void workerRoutine(void* arguments)
{
	try
	{
    	zmq::context_t* context = (zmq::context_t*) arguments;
		zmq::socket_t socket(*context, ZMQ_REP);

		int64_t hwm;
		size_t hwm_size = sizeof(hwm);
		// Set 2 message queue per connection. ZMQ automatically dispatch more queue
		// to other thread if more than 2 message per connection
		int rc = zmq_setsockopt(socket, 2, &hwm, hwm_size);
		assert(rc);

		// Connect to the dispatcher (queue) running in the main thread.
		socket.connect("inproc://workers");
		str* smithThreadR		= new str();
		smithThreadR->m_context	= context;
		smithThreadR->m_socket	= &socket;

		while(true)
		{
			// Get a request from the dispatcher.
			zmq::message_t request;
			socket.recv(&request);

			smithThreadR->m_request		= &request;
			smithThreadR->m_recvData	= (UBYTE*)request.data();

			smithThreadR->onRecvCommand();
		}
    }
	catch(const zmq::error_t& ze)
	{
    	logCritical("Exception - %s\n", ze.what());
    }
    return;
}

/**
 * daemonWritePid(void)
 * Write the process ID of the server into the file specified in <path>.
 * The file is automatically deleted on a call to daemonExit().
 */
static int daemonWritePid(void){

	int nFd;
	char szBuff[16] = {0};
	nFd = safeOpen((const char*)SMITH_THREAD_RUNNER_PID_FILE, O_RDWR | O_CREAT, LOCKMODE);
	if(nFd < 0)
	{
		logError("smithd failed to open \"%s\" in %s at %d\n", SMITH_THREAD_RUNNER_PID_FILE, __FILE__, __LINE__);
		return (-1);
	}

	if(writeLockFile(nFd) < 0){
		if(errno == EACCES || errno == EAGAIN)
		{
			close(nFd);
			return (1);
		}
		logCritical("smithd can't lock %s in %s at %d\n", SMITH_THREAD_RUNNER_PID_FILE, __FILE__, __LINE__);
		exit(1);
	}

	int status = ftruncate(nFd, 0);
	gDaemonPid = getpid();
	sprintf(szBuff, "%ld", (long)gDaemonPid);
	safeBwrite(nFd, (uint8_t*)szBuff, strlen(szBuff)+1);
	return (0);
}

/**
 * Drop server privileges by changing our effective group and user IDs to
 * the ones specified.
 * Returns 0 on success, or -1 on failure.
 */
static int daemonPrivDrop(void)
{
	 // Start by the group, otherwise we can't call setegid() if we've
	 // already dropped user privileges.
	if((getegid() != gDaemonGid) && (setegid(gDaemonGid) == -1))
	{
		logError("strd failed to set group ID to %u in %s [%d]\n", gDaemonGid, __FILE__, __LINE__);
		return (-1);
	}

	if((geteuid() != gDaemonUid) && (seteuid(gDaemonUid) == -1))
	{
		logError("strd failed to set user ID to %u in %s [%d]\n", gDaemonUid, __FILE__, __LINE__);
		return (-1);
	}

	logInfo("strd dropped privileges to %u:%u in %s [%d]\n" , gDaemonUid, gDaemonGid, __FILE__, __LINE__);
	return (0);
}

/**
 * Deal with signals
 * LOCKING: acquires and releases configlock
 */
static void* signalThread(void* arg)
{
	int	nError, nSigno;

	nError = sigwait(&mask, &nSigno);

	if(nError != 0)
	{
		logCritical("signalThread - strd sigwait failed: %s, in %s [%d]\n", strerror(nError), __FILE__, __LINE__);
		daemonExit(1);
	}

	switch(nSigno)
	{
		case SIGHUP:
			sigHUP(nSigno);
			break;

		case SIGTERM:
			sigTERM(nSigno);
			break;

		case SIGQUIT:
			sigQUIT(nSigno);
			break;

		default:
			logInfo("signalThread - unexpected signal %d\n", nSigno);
			break;
	}

	return (void*)1;
}

void sigHUP(int nSigno)
{
	logInfo("signalThread - strd receive SIGHUP signal %s\n", strsignal(nSigno));
	//TODO: Schedule to re-read the configuration file.
	//reReadConf();
}

void sigTERM(int nSigno)
{
	logInfo("signalThread - strd terminate with signal %s\n", strsignal(nSigno));
	daemonExit(1);
}

void sigQUIT(int nSigno)
{
	logInfo("signalThread - strd terminate with signal %s\n", strsignal(nSigno));
	daemonExit(1);
}

void httpRocket(void* data)
{
	struct smithTestPlotHttpResponse* chunk = (struct smithTestPlotHttpResponse*) data;

	try
	{
		struct smithTestPlotResponse* response = initCurl(chunk);

		mongoc_client_t*		client;
		mongoc_collection_t*	collection;
		const char*				uristr			= "mongodb://127.0.0.1:27017/";
		const char*				collectionName	= "httpTestPlot";

		client = mongoc_client_new(uristr);

		if(!client)
		{
			logError("Failed to initialize mongo uristr [%d]\n", __LINE__);
			return;
		}

		collection = mongoc_client_get_collection(client, szPlotReportDb[chunk->nPlotReportDbIndex], collectionName);

		bson_context_t* context;
		bson_t			b;
		bson_oid_t		oid;
		bool			mongoRet;
		bson_error_t	error;

		context = bson_context_new(BSON_CONTEXT_NONE);

		bson_init(&b);
		bson_oid_init(&oid, context);
		bson_append_oid(&b, "_id", 3, &oid);
		bson_append_int64(&b, "userId", -1, chunk->dwUsedId);
		bson_append_utf8(&b, "plotName", 8, chunk->pszPlotName, strlen(chunk->pszPlotName));
		bson_append_double(&b, "nameResolutionTime", -1, response->dNameLookupTime);
		bson_append_double(&b, "connectTime", -1, response->dConnectTime);
		bson_append_double(&b, "processDelta", -1, response->dProcessRequiredTime);

		// XXX: IMPORTANT - count required time to SSL/SSH connect/handshake to the remote host
		if(chunk->nType == 2) bson_append_double(&b, "appConnectTime", -1, response->dAppConnectTime);

		bson_append_double(&b, "preTransferTime", -1, response->dPreTransferTime);
		bson_append_double(&b, "startTransferTime", -1, response->dStartTransferTime);

		if(chunk->nIsDownloadCheck)
		{
			bson_append_double(&b, "bytesDownloaded", -1, response->dBytesDownloaded);
			bson_append_double(&b, "avgDownloadSpeed", -1, response->dAvgDownloadSpeed);
		}

		bson_append_int32(&b, "curlStatus", -1, response->nCurlStatus);
		bson_append_int32(&b, "httpCode", -1, response->lHttpCode);
		bson_append_int64(&b, "responseSize", -1, response->size);

		mongoRet = mongoc_collection_insert(collection, MONGOC_INSERT_NONE, &b, NULL, &error);

		if(!mongoRet)
		{
			logError("mongo insert error %s [httpRocket - %d]\n", error.message, __LINE__);
		}

		bson_destroy(&b);
		mongoc_collection_destroy(collection);
		mongoc_client_destroy(client);

		// XXX: IMPORTANT - must free here
		//response->pszResponse = (char*) realloc(response->pszResponse, (response->size + 1) * sizeof(char));
		//response->pszResponse[response->size] = '\0';
		//free(response->pszResponse);
		free(response);
	}
	catch(int e)
	{
		return;
	}

	return;
}

void httpDownloadRocket(void* data)
{
	struct smithTestPlotHttpDownloadResponse* chunk = (struct smithTestPlotHttpDownloadResponse*) data;

	try
	{
		struct smithTestPlotResponse* response = initDownloadCurl(chunk);

		mongoc_client_t*		client;
		mongoc_collection_t*	collection;
		const char*				uristr			= "mongodb://127.0.0.1:27017/";
		const char*				collectionName	= "httpDownloadTestPlot";

		client = mongoc_client_new(uristr);

		if(!client)
		{
			logError("Failed to initialize mongo uristr [%d]\n", __LINE__);
			return;
		}

		collection = mongoc_client_get_collection(client, szPlotReportDb[chunk->nPlotReportDbIndex], collectionName);

		bson_context_t* context;
		bson_t			b;
		bson_oid_t		oid;
		bool			mongoRet;
		bson_error_t	error;

		context = bson_context_new(BSON_CONTEXT_NONE);

		bson_init(&b);
		bson_oid_init(&oid, context);
		bson_append_oid(&b, "_id", 3, &oid);
		bson_append_int64(&b, "userId", -1, chunk->dwUsedId);
		bson_append_utf8(&b, "plotName", 8, chunk->pszPlotName, strlen(chunk->pszPlotName));
		bson_append_double(&b, "nameResolutionTime", -1, response->dNameLookupTime);
		bson_append_double(&b, "connectTime", -1, response->dConnectTime);
		bson_append_double(&b, "processDelta", -1, response->dProcessRequiredTime);

		// XXX: IMPORTANT - count required time to SSL/SSH connect/handshake to the remote host
		if(chunk->nType == 2) bson_append_double(&b, "appConnectTime", -1, response->dAppConnectTime);

		bson_append_double(&b, "preTransferTime", -1, response->dPreTransferTime);
		bson_append_double(&b, "startTransferTime", -1, response->dStartTransferTime);

		if(chunk->nIsDownloadCheck)
		{
			bson_append_double(&b, "bytesDownloaded", -1, response->dBytesDownloaded);
			bson_append_double(&b, "avgDownloadSpeed", -1, response->dAvgDownloadSpeed);
		}

		bson_append_int32(&b, "curlStatus", -1, response->nCurlStatus);
		bson_append_int32(&b, "httpCode", -1, response->lHttpCode);
		bson_append_int64(&b, "responseSize", -1, response->size);

		mongoRet = mongoc_collection_insert(collection, MONGOC_INSERT_NONE, &b, NULL, &error);

		if(!mongoRet)
		{
			logError("mongo insert error %s [httpDownloadRocket - %d]\n", error.message, __LINE__);
		}

		bson_destroy(&b);
		mongoc_collection_destroy(collection);
		mongoc_client_destroy(client);

		// XXX: IMPORTANT - must free here
		//response->pszResponse = (char*) realloc(response->pszResponse, (response->size + 1) * sizeof(char));
		//response->pszResponse[response->size] = '\0';
		//free(response->pszResponse);
		free(response);
	}
	catch(int e)
	{
		return;
	}

	return;
}

void httpUploadRocket(void* data)
{
	struct smithTestPlotHttpUploadResponse* chunk = (struct smithTestPlotHttpUploadResponse*) data;

	try
	{
		struct smithTestPlotResponse* response = initUploadCurl(chunk);

		mongoc_client_t*		client;
		mongoc_collection_t*	collection;
		const char*				uristr			= "mongodb://127.0.0.1:27017/";
		const char*				collectionName	= "httpUploadTestPlot";

		client = mongoc_client_new(uristr);

		if(!client)
		{
			logError("Failed to initialize mongo uristr [%d]\n", __LINE__);
			return;
		}

		collection = mongoc_client_get_collection(client, szPlotReportDb[chunk->nPlotReportDbIndex], collectionName);

		bson_context_t* context;
		bson_t			b;
		bson_oid_t		oid;
		bool			mongoRet;
		bson_error_t	error;

		context = bson_context_new(BSON_CONTEXT_NONE);

		bson_init(&b);
		bson_oid_init(&oid, context);
		bson_append_oid(&b, "_id", 3, &oid);
		bson_append_int64(&b, "userId", -1, chunk->dwUsedId);
		bson_append_utf8(&b, "plotName", 8, chunk->pszPlotName, strlen(chunk->pszPlotName));
		bson_append_double(&b, "nameResolutionTime", -1, response->dNameLookupTime);
		bson_append_double(&b, "connectTime", -1, response->dConnectTime);
		bson_append_double(&b, "processDelta", -1, response->dProcessRequiredTime);

		// XXX: IMPORTANT - count required time to SSL/SSH connect/handshake to the remote host
		if(chunk->nType == 2) bson_append_double(&b, "appConnectTime", -1, response->dAppConnectTime);

		bson_append_double(&b, "preTransferTime", -1, response->dPreTransferTime);
		bson_append_double(&b, "startTransferTime", -1, response->dStartTransferTime);

		if(chunk->nIsUploadCheck)
		{
			bson_append_double(&b, "avgUploadSpeed", -1, response->dAvgUploadSpeed);
		}

		bson_append_int32(&b, "curlStatus", -1, response->nCurlStatus);
		bson_append_int32(&b, "httpCode", -1, response->lHttpCode);
		bson_append_int64(&b, "responseSize", -1, response->size);

		mongoRet = mongoc_collection_insert(collection, MONGOC_INSERT_NONE, &b, NULL, &error);

		if(!mongoRet)
		{
			logError("mongo insert error %s [httpUploadRocket - %d]\n", error.message, __LINE__);
		}

		bson_destroy(&b);
		mongoc_collection_destroy(collection);
		mongoc_client_destroy(client);

		// XXX: IMPORTANT - must free here
		//response->pszResponse = (char*) realloc(response->pszResponse, (response->size + 1) * sizeof(char));
		//response->pszResponse[response->size] = '\0';
		//free(response->pszResponse);
		free(response);
	}
	catch(int e)
	{
		return;
	}

	return;
}

/**
 *
 */
void help(void)
{
	printf("str Daemon by 800cycles <info@800cycles.com>\n");
	printf("Usage: /etc/init.d/x.str start\n"
		" --help        -h -?               Print this help\n"
	);
}


/**
 *
 */
void processOptions()
{
	static const struct option longOptions[] =
	{
		{"pid",			required_argument,	NULL, 0		},
		{"env",			required_argument,	NULL, 'e'	},
		{"port",		required_argument,	NULL, 0		},
		{"log",			required_argument,	NULL, 0		},
		{"config",		required_argument,	NULL, 0		},
		{"maxthread",	required_argument,	NULL, 0		},
		{"help",	0, 						NULL, 'h'	},
		{0,			0, 						NULL, 0		}
	};

	char* pszPid		= NULL;
	char* pszPort		= NULL;
	char* pszLog		= NULL;
	char* pszConfig		= NULL;
	char* pszMaxThread	= NULL;

	// default settings
	strcpy(SMITH_THREAD_RUNNER_PID_FILE, "/var/run/str.pid");
	strcpy(APPLICATION_ENV, "local");
	strcpy(SMITH_THREAD_RUNNER_CONFIG_FILE, "/etc/str.conf");

	for(;;)
	{
		int nRes;
		int optionIndex			= 0;
		struct option* pszOpt	= 0;

		nRes = getopt_long(m_argc, m_argv, "e:p:h?", longOptions, &optionIndex);
		if(nRes == -1) break;

		switch(nRes){

			case '?' :

			case 'e' :

				if((strcmp(optarg, "local") == 0) || (strcmp(optarg, "development") == 0) || (strcmp(optarg, "production") == 0))
				{
					sprintf(APPLICATION_ENV, "%s", optarg);
				}
				else
				{
					strcpy(APPLICATION_ENV, "local");
				}
				break;

			case 'h' :

				help();
				exit(1);
				break;

			case 0 :

				pszOpt = (struct option*)&(longOptions[optionIndex]);

				if(strcmp(pszOpt->name, "pid") == 0)
				{
					if((pszPid = safeStrdup(optarg)) == NULL)
					{
						strcpy(SMITH_THREAD_RUNNER_PID_FILE, "/var/run/str.pid");
					}
					else
					{
						strcpy(SMITH_THREAD_RUNNER_PID_FILE, pszPid);
					}

					pszPid = NULL;
				}

				if(strcmp(pszOpt->name, "port") == 0)
				{
					if((pszPort = safeStrdup(optarg)) == NULL)
					{
						gwPort = (UWORD) 5555;
					}
					else
					{
						gwPort = (UWORD) strtoul(pszPort, NULL, 10);
					}

					pszPort = NULL;
				}

				if(strcmp(pszOpt->name, "log") == 0)
				{
					if((pszLog = safeStrdup(optarg)) == NULL)
					{
						strcpy(g_szLogFileName, "/var/log/testerd/testerd.log");
					}
					else
					{
						strcpy(g_szLogFileName, pszLog);
					}

					pszLog = NULL;
				}

				if(strcmp(pszOpt->name, "config") == 0)
				{
					if((pszConfig = safeStrdup(optarg)) == NULL)
					{
						strcpy(SMITH_THREAD_RUNNER_CONFIG_FILE, "/etc/str.conf");
					}
					else
					{
						strcpy(SMITH_THREAD_RUNNER_CONFIG_FILE, pszConfig);
					}

					pszConfig = NULL;
				}

				if(strcmp(pszOpt->name, "maxthread") == 0)
				{
					if((pszMaxThread = safeStrdup(optarg)) == NULL)
					{
						gSTRThreadLeft = 50000;
					}
					else
					{
						gSTRThreadLeft = (int) strtoul(pszMaxThread, NULL, 10);
					}

					pszMaxThread = NULL;
				}

				break;

			default :
				help();
				exit(1);
				break;
		}
	}
}

void init()
{
	// become a daemon and fork
	if((bIsDaemon == TRUE) && (daemoNize() == -1))
	{
		logError("Failed to become a smithd in %s at %d\n", __FILE__, __LINE__);
		exit(1);
	}

	// make sure only one copy of the daemon is running
	if(daemonWritePid())
	{
		logError("smithd already running in file %s at %d\n", __FILE__, __LINE__);
	}

	// drop privileges
	if((gDaemonUid > 0) || (gDaemonGid > 0))
	{
		if(daemonPrivDrop() == -1) daemonExit(1);
	}
	else
	{
		gDaemonGid = getgid();
		gDaemonUid = getuid();
	}
}

int main(int argc, char** argv)
{
	int			nNumberOfThreadI;
	pthread_t	ptSignalId;

	m_argc = argc;
	m_argv = argv;

	processOptions();

	if(!gwPort)
	{
		printf("strd port not initialized...\n");
		logError("strd port not initialized in %s [%d]\n", __FILE__, __LINE__);
		exit(1);
	}

	if(!gpszProtocol) gpszProtocol = (char*)"tcp";

	// initialize the daemon
	init();

	int nParseStatus = parseConfigFile(SMITH_THREAD_RUNNER_CONFIG_FILE);

	pthread_mutex_init(&STR_THREAD_STAT_MUTEX, NULL);

	char szHostname[1024] = {0};
	gethostname(szHostname, 1023);
	if(strstr(szHostname, "development"))
	{
		sprintf(MYSQL_HOST_ACCESS, "localhost");
		sprintf(MYSQL_USER_ACCESS, "root");
		sprintf(MYSQL_PASS_ACCESS, "mypassword");
		sprintf(MYSQL_PORT_ACCESS, "3306");
		sprintf(MYSQL_DB_ACCESS, "smith");

		sprintf(MONGO_HOST_ACCESS, "ec2-176-34-6-126.ap-northeast-1.compute.amazonaws.com");
		MONGO_PORT_ACCESS = 27017;
		sprintf(MONGO_DB_ACCESS, "smith");

		sprintf(gHostAddr, "*");
	}
	else if(strstr(szHostname, "local"))
	{
		sprintf(MYSQL_HOST_ACCESS, "localhost");
		sprintf(MYSQL_USER_ACCESS, "root");
		sprintf(MYSQL_PASS_ACCESS, "mypassword");
		sprintf(MYSQL_PORT_ACCESS, "3306");
		sprintf(MYSQL_DB_ACCESS, "smith");

		sprintf(MONGO_HOST_ACCESS, "localhost");
		MONGO_PORT_ACCESS = 27017;
		sprintf(MONGO_DB_ACCESS, "smith");

		sprintf(gHostAddr, "127.0.0.1");
	}
	else if(strstr(szHostname, "production"))
	{
		sprintf(MYSQL_HOST_ACCESS, "localhost");
		sprintf(MYSQL_USER_ACCESS, "root");
		sprintf(MYSQL_PASS_ACCESS, "mypassword");
		sprintf(MYSQL_PORT_ACCESS, "3306");
		sprintf(MYSQL_DB_ACCESS, "smith");

		sprintf(MONGO_HOST_ACCESS, "localhost");
		MONGO_PORT_ACCESS = 27017;
		sprintf(MONGO_DB_ACCESS, "smith");

		sprintf(gHostAddr, "*");
	}
	else
	{
		sprintf(MYSQL_HOST_ACCESS, "localhost");
		sprintf(MYSQL_USER_ACCESS, "root");
		sprintf(MYSQL_PASS_ACCESS, "mypassword");
		sprintf(MYSQL_PORT_ACCESS, "3306");
		sprintf(MYSQL_DB_ACCESS, "smith");

		sprintf(MONGO_HOST_ACCESS, "localhost");
		MONGO_PORT_ACCESS = 27017;
		sprintf(MONGO_DB_ACCESS, "www_smith_com");

		sprintf(gHostAddr, "127.0.0.1");
	}

	zmq::context_t ctx(50);
    // Create an endpoint for worker threads to connect to.
    // We are using XREQ socket so that processing of one request
    // won't block other requests.
    zmq::socket_t workers(ctx, ZMQ_XREQ);
    workers.bind("inproc://workers");

    // Create an endpoint for client applications to connect to
    // We are usign XREP socket so that processing of one request
    // won't block other requests
    zmq::socket_t clients(ctx, ZMQ_XREP);

    sprintf(gConnection, "%s://%s:%u", gpszProtocol, gHostAddr, gwPort);
    clients.bind(gConnection);

	int nI;
	// set STR id
	for(nI=0; nI<gListOfThreadRunner; nI++)
	{
		if(strcmp(szListOfThreadRunner[nI], gConnection) == 0)
		{
			gSTRId = nI;
			break;
		}
	}

    // We'll use thread pool to generate a lot of thread to support simultaneous access to phantom
    if((tPoolWorker = threadpoolInit(SMITH_THREAD_RUNNER_WORKER_THREAD)) == NULL)
	{
		logError("Failed to create a thread pool struct.\n");
		exit(1);
	}

    for(nNumberOfThreadI = 0; nNumberOfThreadI!= SMITH_THREAD_RUNNER_WORKER_THREAD; nNumberOfThreadI++)
    {
    	int ret = threadpoolAddTask(tPoolWorker, workerRoutine, (void*)&ctx, 0);

		if(ret == -1)
		{
			logError("An error had occurred while adding a task\n");
			threadpoolFree(tPoolWorker, 0);
			exit(1);
		}
    }

	// only except HUP, TERM and QUIT signal
	sigemptyset(&mask);
	sigaddset(&mask, SIGHUP);
	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGQUIT);
	pthread_sigmask(SIG_BLOCK, &mask, NULL);
	pthread_create(&ptSignalId, NULL, signalThread, NULL);


	// Connect work threads to client threads via a queue
	zmq::proxy(clients, workers, NULL);

    printf("str daemon shutting down...\n");
    threadpoolFree(tPoolWorker, 0);
    pthread_mutex_destroy(&STR_THREAD_STAT_MUTEX);
	return 0;
}
