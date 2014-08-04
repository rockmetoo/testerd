/*
 * testSignUp.c
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

#include "smithd.h"

#include <stdio.h>
#include <unistd.h>
#include <curl/curl.h>
#include <curl/easy.h>
#include <exception>
#include <mysql/mysql.h>
#include <mongoc.h>
#include <bson.h>

#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/ip_icmp.h>

#include "log.h"
#include "helper.h"
#include "threadPool.h"
#include "iniparser.h"
#include "encrypt.h"
#include "mysqlwrap.h"
#include "icmp.h"
#include "hook.h"
#include "jsonlib/jansson.h"

#define BUF_SIZE					10*1024
#define ASCIILINESZ					(1024)

#define	COPYRIGHT					"©Data Trench. All rights reserved"

#define MAX_CMD_SIZE				1024


static				sigset_t		mask;

bool				bIsDaemon			= TRUE;
UWORD				gwPort				= 5555;
static char*		gpszProtocol;
char				gHostAddr[255]		= {0};
pid_t				gDaemonPid;
uid_t				gDaemonUid;
gid_t				gDaemonGid;
char*				SMITH_PID_FILE		= new char[512];
char*				SMITH_CONFIG_FILE	= new char[512];
struct threadpool*	tPoolWorker			= NULL;

int 				gListOfSmithd		= 0;
int 				gListOfThreadRunner	= 0;
int					gsmithId			= 0;
int					gNumOfPlotReportDB	= 0;

char				gConnection[256];


// [0] = status, [1] = no. of running thread, [2] = no. of threads can handle more
int**				gSTRStatus;

struct stSTRReqValue
{
	int				nI;
	UDWORD			uNoOfThreadToRun;
	char			szUserId[20];
	char			szPlotName[255+1];
	int				nRandomReportDbIndex;
};

char*				SMITH_CLI_PATH		= new char[256];

const char* gSMITHCP_CLI_SCRIPT[]		=
{
	"/usr/bin/php createHttpTestreport.php",
	"/usr/bin/php createHttpDownloadTestreport.php",
	"/usr/bin/php createHttpUploadTestreport.php"
};


void	sigHUP(int nSigno);
void	sigTERM(int nSigno);
void	sigQUIT(int nSigno);
void	workerRoutine(void* arguments);
void	testPlotRunRoutineForSTR(void* arguments);
void	downloadTestPlotRunRoutineForSTR(void* arguments);
void	uploadTestPlotRunRoutineForSTR(void* arguments);
void	checkAllSTRStatus();
int		parseConfigFile(char* pszFileName);
static void* countMaxHops(void* arguments);

smithd::smithd(){}

smithd::~smithd(){}

BOOL smithd::onRecvCommand()
{
	struct timeval tvStart;
	gettimeofday(&tvStart, NULL);

	UDWORD dwPos			= 8;
	char* pszControlCode	= (char*)&m_recvData[dwPos];
	dwPos += strlen((char*)&m_recvData[dwPos]) + 1;
	size_t plainlen			= strlen(pszControlCode);

	char key[17];
	strcpy(key, COMMAND_KEY);

	// ascipherlen is decleared in encrypt.h
	ascipherlen		= 2 * plainlen;
	ascii_decrypt128(pszControlCode, key);

	m_byCmd			= atoi(plainText);

	//logDebug("pszControlCode: %s  plainlen: %d   m_byCmd: %d   plainText: %s\n", pszControlCode, plainlen, m_byCmd, plainText);

	pszControlCode	= NULL;

	switch(m_byCmd)
	{
		case	SMITH_CMD_INDEX_RUN_SPECIFIC_PLOT:
			if(onRecvRunSpecificPlot()==FALSE) return FALSE;
			break;

		case	SMITH_CMD_INDEX_UPDATE_STR_STATUS:
			if(onRecvRunGetSTRStatusById()==FALSE) return FALSE;
			break;

		case	SMITH_CMD_INDEX_STR_THREAD_DONE:
			if(onRecvRunSTRThreadDone()==FALSE) return FALSE;
			break;

		case	SMITH_CMD_INDEX_RUN_SPECIFIC_DL_PLOT:
			if(onRecvRunDownloadSpecificPlot()==FALSE) return FALSE;
			break;

		case	SMITH_CMD_INDEX_RUN_SPECIFIC_UL_PLOT:
			if(onRecvRunUploadSpecificPlot()==FALSE) return FALSE;
			break;

		default:
			logError("Unknown command detected as val: %d [%d]\n", m_byCmd, __LINE__);
			return FALSE;
	}

	struct timeval tvFinished;
	gettimeofday(&tvFinished, NULL);
	float fDiff =
	(
		(tvFinished.tv_sec * 100 + tvFinished.tv_usec / 10000)
		- (tvStart.tv_sec * 100 + tvStart.tv_usec / 10000)
	)/100.0f;

	if(fDiff >= 0.5f) logInfo("Command: %u | Spent time: %.2fsec [%d]\n", m_byCmd, fDiff, __LINE__);

	return TRUE;
}

/**
 * XXX: if plotStatus is 0 or 3 then the test request will come here
 */
BOOL smithd::onRecvRunSpecificPlot()
{
	#define CLEAN_UP(x, b) \
		status = x; \
		UBYTE* m_sendData = new UBYTE[b]; \
		dwPos = 0; \
		*(int*)&m_sendData[dwPos] = status; \
		dwPos += 4; \
		zmq::message_t reply(dwPos);  \
		memcpy(reply.data(), m_sendData, dwPos); \
		m_socket->send(reply); \
		FREEP(m_sendData);

	int status				= -1;
	UDWORD dwPos			= 8;
	char szPlotName[256]	= {0};

	// XXX: IMPORTANT - encrypted command parameter skiped start
	dwPos += strlen((char*)&m_recvData[dwPos]) + 1;
	// XXX: IMPORTANT - encrypted command parameter skiped end

	// Data Recv Start
	char* pszUserId = (char*)&m_recvData[dwPos];
	dwPos += strlen((char*)&m_recvData[dwPos])+1;
	UDWORD dwUsedId = (UDWORD) strtoul(pszUserId, NULL, 10);

	char* pszPlotName = (char*)&m_recvData[dwPos];
	dwPos += strlen((char*)&m_recvData[dwPos])+1;
	strcpy(szPlotName, pszPlotName);

	char* pszPlotType = (char*)&m_recvData[dwPos];
	dwPos += strlen((char*)&m_recvData[dwPos])+1;
	// Data Recv End

	if(pszUserId && pszPlotName && (strcmp(pszPlotType, "http") == 0))
	{
		Database db(MYSQL_HOST_ACCESS, MYSQL_USER_ACCESS, MYSQL_PASS_ACCESS, MYSQL_DB_ACCESS);
		Query q(db);

		// XXX: IMPORTANT - move the test plot in 'testing request accepted' mode
		q.DoQuery(1024, "UPDATE `smith`.`httpTestPlot` SET plotStatus=1 WHERE userId='%s' AND plotName='%s'", pszUserId, pszPlotName);

		// XXX: IMPORTANT - successful command and return status message to caller
		CLEAN_UP(1, 1024)
		logDebug("Plot name %s is going to test for user %s [%s]\n", pszPlotName, pszUserId, pszPlotType);

		// SELECT accessLimit from DB
		char szQuery[1024]		= {0};
		UDWORD accessLimit		= 0;
		char* pszPlotReportDb	= NULL;
		char* pszBaseAddress	= NULL;

		sprintf(szQuery, "SELECT accessLimit, plotReportDb, baseAddress FROM `smith`.`httpTestPlot` WHERE userId='%s' AND plotName='%s'", pszUserId, pszPlotName);
		q.get_result(szQuery);
		if(q.GetErrno())
		{
			logInfo("SELECT accessLimit FROM `smith`.`httpTestPlot` SQL Error at %d\n", __LINE__);
			return TRUE;
		}
		else
		{
			while(q.fetch_row())
			{
				accessLimit		= (UDWORD) strtoul(q.getstr("accessLimit"), NULL, 10);
				pszPlotReportDb	= strdup(q.getstr("plotReportDb"));
				pszBaseAddress	= strdup(q.getstr("baseAddress"));
			}
		}

		// XXX: IMPORTANT - Here we are going to check the available test server and threads and then request one/several
		// server to start a test job at same time
		if(accessLimit > 0)
		{
			int nI;

			// XXX: we are going to run a test for a http plot so update 'noOfThreadExecuted' to 0
			q.DoQuery(
				1024,
				"UPDATE `smith`.`httpTestPlot` SET noOfThreadExecuted=0 WHERE userId='%s' AND plotName='%s'",
				pszUserId, pszPlotName
			);

			// XXX: IMPORTANT - 1st check the status of all STR and update 'gSTRStatus'
			checkAllSTRStatus();

			/*
			 * XXX: IMPORTANT - [ALGORITHM] 1st check if any STR can handle all threads by checking nSTRThreadLeft then employ that one
			 * otherwise split all threads into different STR
			 * TODO: We need a job queuing system so that we hold all the test job and sync with all STR
			 * when all the STR are ready to test all jobs at same time simultaneously then we proceed to do send request
			*/

		    // XXX: IMPORTANT - 1st remove old raw response data from mongo (just in case)
		    // Though, 'createHttpTestreport.php' script will delete all raw data after analyze.
		    // 1st time there will be no DB name that's why check by 'strlen'
			if(strlen(pszPlotReportDb) > 0)
			{
				mongoc_client_t*		client;
				mongoc_collection_t*	collection;
				const char*				uristr			= "mongodb://127.0.0.1:27017/";
				const char*				collectionName	= "httpTestPlot";

				client = mongoc_client_new(uristr);

				if(!client)
				{
					logError("Failed to initialize mongo uristr [%d]\n", __LINE__);
					return TRUE;
				}

				collection = mongoc_client_get_collection(client, pszPlotReportDb, collectionName);

				bson_t			b;
				bson_bool_t		mongoRet;
				bson_error_t	error;

				bson_init(&b);
				bson_append_int64(&b, "userId", -1, dwUsedId);
				bson_append_utf8(&b, "plotName", -1, pszPlotName, strlen(pszPlotName));

				mongoRet = mongoc_collection_delete(collection, MONGOC_DELETE_NONE, &b, NULL, &error);

				if(!mongoRet)
				{
					logError("%s\n", error.message);
					return TRUE;
				}

				bson_destroy(&b);
				mongoc_collection_destroy(collection);
				mongoc_client_destroy(client);
			}

		    // XXX: IMPORTANT - choose a mongo database randomly and update 'plotReportDb'
		    short int nRandomReportDbIndex = randomNumber(1, gNumOfPlotReportDB - 1);

		    Database db(MYSQL_HOST_ACCESS, MYSQL_USER_ACCESS, MYSQL_PASS_ACCESS, MYSQL_DB_ACCESS);
			Query updateQuery(db);
			updateQuery.DoQuery(
				1024,
				"UPDATE `smith`.`httpTestPlot` SET plotReportDb='%s' WHERE userId='%s' AND plotName='%s'",
				szPlotReportDb[nRandomReportDbIndex], pszUserId, pszPlotName
			);
			updateQuery.free_result();

			// XXX: IMPORTANT - Hop count between smithd and test host
			pthread_t ptHops;
			struct stTraceRoute traceRt;
			urlst* url;
			traceRt.dwUsedId = dwUsedId;

			url = parseUrl(pszBaseAddress);
			sprintf(traceRt.szHost, "%s", url->pszHost);
			sprintf(traceRt.szPlotName, "%s", pszPlotName);
			sprintf(traceRt.szTablename, "httpTestPlot");
			pthread_create(&ptHops, NULL, countMaxHops, (void*)&traceRt);

			// XXX: IMPORTANT - must free the structure
			freeUrl(url);

			unsigned long nTotalThreadCanHandle = 0;

			// XXX: TODO: IMPORTANT - 1st check that combine all the STR, we can achieve our test otherwise 'plotQueue' and return
			for(nI=0; nI!=gListOfThreadRunner; nI++)
			{
				nTotalThreadCanHandle += gSTRStatus[nI][2];
			}

			if(nTotalThreadCanHandle < accessLimit)
			{
				// TODO: insert in plotQueue table and update plotStatus in httpTestPlot table
				q.DoQuery(
					1024,
					"UPDATE `smith`.`httpTestPlot` SET plotStatus=4 WHERE userId='%s' AND plotName='%s'",
					pszUserId, pszPlotName
				);

				q.DoQuery(
					1024,
					"INSERT INTO `smith`.`plotQueue` (userId, plotName, testType, priority, status, dateCreated) "
					"VALUES ('%s', '%s', '1', '%d', '0', '%s') ON DUPLICATE KEY UPDATE dateCreated='%s'",
					pszUserId, pszPlotName
				);

				q.free_result();

				return TRUE;
			}

		    // XXX: IMPORTANT - 2nd check that if one STR can handle all jobs then send one request and return
		    for(nI=0; nI!=gListOfThreadRunner; nI++)
			{
		    	if(gSTRStatus[nI][2] >= accessLimit)
				{
					logDebug("smithd got a STR [%d - %s] which can handle all test jobs\n", nI, szListOfThreadRunner[nI]);

					struct stSTRReqValue reqData;
					bzero(&reqData, sizeof(struct stSTRReqValue));

					reqData.nI					= (int) nI;
					reqData.uNoOfThreadToRun	= (UDWORD) accessLimit;
					sprintf(reqData.szUserId,	"%s", pszUserId);
					sprintf(reqData.szPlotName, "%s", pszPlotName);
					reqData.nRandomReportDbIndex = nRandomReportDbIndex;

					HOOKER hooker;

					// Create a hooker of 'uNoOfThreadToRun' thread workers
					if((hooker = newHooker(1, 2, FALSE)) == NULL)
					{
						logError("Failed to create a thread pool struct [%d]\n", __LINE__);
						return TRUE;
					}

					void* statusp;

					int ret = hookerAdd(hooker, testPlotRunRoutineForSTR, &reqData);

					if(ret == FALSE)
					{
						logError("An error had occurred while adding a task [%d]\n", __LINE__);
						/**
						 * TODO: task is not completed properly so we need to store pszUserId, pszPlotName, pszPlotType
						 * in a separate table called 'unfinishedTestJob' and another process will check it and do the task
						 */

						return TRUE;
					}

				    hookerJoin(hooker, TRUE, &statusp);

					// XXX: IMPORTANT - cleanup hooker
					hookerDestroy(hooker);

					q.free_result();

					return TRUE;
				}
			}

		    int nNumberOfThreadToRun = accessLimit;

		    // XXX: IMPORTANT - 3rd check that if there is no STR can handle all jobs then send multiple request to multiple STR
		    for(nI=0; nI!=gListOfThreadRunner; nI++)
		    {
		    	if(gSTRStatus[nI][2] <= nNumberOfThreadToRun) nNumberOfThreadToRun = gSTRStatus[nI][2];

		    	// XXX: IMPORTANT - just in case
		    	if(nNumberOfThreadToRun < 1) continue;

		    	// XXX: IMPORTANT - 'gSTRStatus[nI][2]' contains how many test job that STR can do at this moment
		    	// gSTRStatus is a global variable and so it can be changed by several other process
				struct stSTRReqValue reqData;
				bzero(&reqData, sizeof(struct stSTRReqValue));

				reqData.nI					= (int) nI;
				reqData.uNoOfThreadToRun	= (UDWORD) nNumberOfThreadToRun;
				sprintf(reqData.szUserId,	"%s", pszUserId);
				sprintf(reqData.szPlotName, "%s", pszPlotName);
				reqData.nRandomReportDbIndex = nRandomReportDbIndex;

				HOOKER hooker;

				// Create a hooker of 'uNoOfThreadToRun' thread workers
				if((hooker = newHooker(1, 2, FALSE)) == NULL)
				{
					logError("Failed to create a thread pool struct [%d]\n", __LINE__);
					return TRUE;
				}

				void* statusp;

				int ret = hookerAdd(hooker, testPlotRunRoutineForSTR, &reqData);

				if(ret == FALSE)
				{
					logError("An error had occurred while adding a task [%d]\n", __LINE__);
					/**
					 * TODO: task is not completed properly so we need to store pszUserId, pszPlotName, pszPlotType
					 * in a separate table called 'unfinishedTestJob' and another process will check it and do the task
					 */

					return TRUE;
				}

				accessLimit				= accessLimit - nNumberOfThreadToRun;
				nNumberOfThreadToRun	= accessLimit;

			    hookerJoin(hooker, TRUE, &statusp);

				// XXX: IMPORTANT - cleanup hooker
				hookerDestroy(hooker);
		    }

			// XXX: IMPORTANT - take a short nap  for cosmetic effect this does NOT affect performance stats.
			uSleep(10000);
		}
		else
		{
			logDebug("Http test plot accessLimit is invalid for userId=%u and plotName=%s\n", dwUsedId, pszPlotName);
			return TRUE;
		}

		q.free_result();
	}
	else
	{
		CLEAN_UP(0, 1024)
		logDebug("Http test plot Input data is invalid!!!");
		return TRUE;
	}

	return TRUE;
}

BOOL smithd::onRecvRunGetSTRStatusById()
{
	#define RETURN_STR_REPLY(x, b) \
		status = x; \
		UBYTE* m_sendData = new UBYTE[b]; \
		dwPos = 0; \
		*(int*)&m_sendData[dwPos] = status; \
		dwPos += 4; \
		zmq::message_t reply(dwPos);  \
		memcpy(reply.data(), m_sendData, dwPos); \
		m_socket->send(reply); \
		FREEP(m_sendData);

	int status				= -1;
	UDWORD dwPos			= 8;

	// encrypted command parameter skiped start
	dwPos += strlen((char*)&m_recvData[dwPos]) + 1;
	// encrypted command parameter skiped end

	// [0] = status, [1] = no. of running thread, [2] = no. of threds can handle more
	// Data Recv Start
	int nSTRId = *(int*)&m_recvData[dwPos];
	dwPos += 4;
	gSTRStatus[nSTRId][0] = *(int*)&m_recvData[dwPos];
	dwPos += 4;
	gSTRStatus[nSTRId][1] = *(int*)&m_recvData[dwPos];
	dwPos += 4;
	gSTRStatus[nSTRId][2] = *(int*)&m_recvData[dwPos];
	dwPos += 4;
	// Data Recv End

	RETURN_STR_REPLY(1, 1024)

	return TRUE;
}

BOOL smithd::onRecvRunSTRThreadDone()
{
	#define RETURN_STR_REPLY_FOR_DONE(x, b) \
		status = x; \
		UBYTE* m_sendData = new UBYTE[b]; \
		dwPos = 0; \
		*(int*)&m_sendData[dwPos] = status; \
		dwPos += 4; \
		zmq::message_t reply(dwPos);  \
		memcpy(reply.data(), m_sendData, dwPos); \
		m_socket->send(reply); \
		FREEP(m_sendData);

	int status				= -1;
	UDWORD dwPos			= 8;

	// encrypted command parameter skiped start
	dwPos += strlen((char*)&m_recvData[dwPos]) + 1;
	// encrypted command parameter skiped end

	// [0] = status, [1] = no. of running thread, [2] = no. of threads can handle more
	// Data Receive Start
	int nSTRId = *(int*)&m_recvData[dwPos];
	dwPos += 4;

	gSTRStatus[nSTRId][0] = *(int*)&m_recvData[dwPos];
	dwPos += 4;

	gSTRStatus[nSTRId][1] = *(int*)&m_recvData[dwPos];
	dwPos += 4;

	gSTRStatus[nSTRId][2] = *(int*)&m_recvData[dwPos];
	dwPos += 4;

	int nNoOfThreadExecuted = *(int*)&m_recvData[dwPos];
	dwPos += 4;

	UDWORD userId = *(UDWORD*)&m_recvData[dwPos];
	dwPos += 4;

	char* pszPlotName = (char*)&m_recvData[dwPos];
	dwPos += strlen((char*)&m_recvData[dwPos])+1;

	char* pszPlotType = (char*)&m_recvData[dwPos];
	dwPos += strlen((char*)&m_recvData[dwPos])+1;
	// Data Receive End

	RETURN_STR_REPLY_FOR_DONE(1, 1024)

	// TODO: Now update plotStatus flag and generate report and graph
	Database db(MYSQL_HOST_ACCESS, MYSQL_USER_ACCESS, MYSQL_PASS_ACCESS, MYSQL_DB_ACCESS);
	Query q(db);

	if(strcmp(pszPlotType, "http") == 0)
	{
		logDebug(
			"Received done status from str[%d], nNoOfThreadExecuted: %d  user: %u  plotName: %s   plotType: %s\n",
			nSTRId, nNoOfThreadExecuted, userId, pszPlotName, pszPlotType
		);

		// XXX: IMPORTANT - 1st update how many threads we run by 'nSTRId' str
		q.DoQuery(
			1024,
			"UPDATE `smith`.`httpTestPlot` SET noOfThreadExecuted=noOfThreadExecuted+%u WHERE userId='%u' AND plotName='%s'",
			nNoOfThreadExecuted, userId, pszPlotName
		);

		char szQuery[1024]	= {0};

		sprintf(szQuery, "SELECT accessLimit, noOfThreadExecuted FROM `smith`.`httpTestPlot` WHERE userId='%u' AND plotName='%s'", userId, pszPlotName);
		q.get_result(szQuery);

		if(q.GetErrno())
		{
			logInfo("SELECT accessLimit, noOfThreadExecuted FROM `smith`.`httpTestPlot` SQL Error at %d\n", __LINE__);
			return TRUE;
		}
		else
		{
			while(q.fetch_row())
			{
				UQWORD accessLimit			= 0;
				UQWORD noOfThreadExecuted	= 0;

				accessLimit			= strtoul(q.getstr(0), NULL, 10);
				noOfThreadExecuted	= strtoul(q.getstr(1), NULL, 10);

				if(accessLimit <= noOfThreadExecuted)
				{
					// logDebug("accessLimit: %u,  noOfThreadExecuted: %u  userId: %u  pszPlotName: %s\n", accessLimit, noOfThreadExecuted, userId, pszPlotName);

					// XXX: IMPORTANT - Now we should trigger one job which will make the report by summarizing all
					// the raw data in mongo and just create one record with summarized value. Moreover,
					// it will also delete the raw data from the mongo
					char szCommand[MAX_CMD_SIZE];

					snprintf(szCommand, MAX_CMD_SIZE, "cd %s;%s %u %s", SMITH_CLI_PATH, (char*)gSMITHCP_CLI_SCRIPT[0], userId, escapeShellArg(pszPlotName));

					// logDebug("CLI command: %s\n", szCommand);

					int status = commandExecute(szCommand);

					// successful command
					if(status == 0)
					{
						logDebug("Successfully execute cli script [onRecvRunSTRThreadDone - %d]\n", __LINE__);
					}
					else
					{
						// TODO: Mail to the geeks becoz error happen
						logError("Failed to execute cli script [onRecvRunSTRThreadDone - %d]\n", __LINE__);
					}
				}
			}
		}
	}
	else if(strcmp(pszPlotType, "httpdl") == 0)
	{
		logDebug(
			"Received done status from str[%d], nNoOfThreadExecuted: %d  user: %u  plotName: %s   plotType: %s\n",
			nSTRId, nNoOfThreadExecuted, userId, pszPlotName, pszPlotType
		);

		// XXX: IMPORTANT - 1st update how many threads we run by 'nSTRId' str
		q.DoQuery(
			1024,
			"UPDATE `smith`.`httpDownloadTestPlot` SET noOfThreadExecuted=noOfThreadExecuted+%u WHERE userId='%u' AND plotName='%s'",
			nNoOfThreadExecuted, userId, pszPlotName
		);

		char szQuery[1024]	= {0};

		sprintf(szQuery, "SELECT accessLimit, noOfThreadExecuted FROM `smith`.`httpDownloadTestPlot` WHERE userId='%u' AND plotName='%s'", userId, pszPlotName);
		q.get_result(szQuery);

		if(q.GetErrno())
		{
			logInfo("SELECT accessLimit, noOfThreadExecuted FROM `smith`.`httpDownloadTestPlot` SQL Error at %d\n", __LINE__);
			return TRUE;
		}
		else
		{
			while(q.fetch_row())
			{
				UQWORD accessLimit			= 0;
				UQWORD noOfThreadExecuted	= 0;

				accessLimit			= strtoul(q.getstr(0), NULL, 10);
				noOfThreadExecuted	= strtoul(q.getstr(1), NULL, 10);

				if(accessLimit <= noOfThreadExecuted)
				{
					// logDebug("accessLimit: %u,  noOfThreadExecuted: %u  userId: %u  pszPlotName: %s\n", accessLimit, noOfThreadExecuted, userId, pszPlotName);

					// XXX: IMPORTANT - Now we should trigger one job which will make the report by summarizing all
					// the raw data in mongo and just create one record with summarized value. Moreover,
					// it will also delete the raw data from the mongo
					char szCommand[MAX_CMD_SIZE];

					snprintf(szCommand, MAX_CMD_SIZE, "cd %s;%s %u %s", SMITH_CLI_PATH, (char*)gSMITHCP_CLI_SCRIPT[1], userId, escapeShellArg(pszPlotName));

					// logDebug("CLI command: %s\n", szCommand);

					int status = commandExecute(szCommand);

					// successful command
					if(status == 0)
					{
						logDebug("Successfully execute cli script [onRecvRunSTRThreadDone - %d]\n", __LINE__);
					}
					else
					{
						// TODO: Mail to the geeks becoz error happen
						logError("Failed to execute cli script [onRecvRunSTRThreadDone - %d]\n", __LINE__);
					}
				}
			}
		}
	}
	else if(strcmp(pszPlotType, "httpul") == 0)
	{
		logDebug(
			"Received done status from str[%d], nNoOfThreadExecuted: %d  user: %u  plotName: %s   plotType: %s\n",
			nSTRId, nNoOfThreadExecuted, userId, pszPlotName, pszPlotType
		);

		// XXX: IMPORTANT - 1st update how many threads we run by 'nSTRId' str
		q.DoQuery(
			1024,
			"UPDATE `smith`.`httpUploadTestPlot` SET noOfThreadExecuted=noOfThreadExecuted+%u WHERE userId='%u' AND plotName='%s'",
			nNoOfThreadExecuted, userId, pszPlotName
		);

		char szQuery[1024]	= {0};

		sprintf(szQuery, "SELECT accessLimit, noOfThreadExecuted FROM `smith`.`httpUploadTestPlot` WHERE userId='%u' AND plotName='%s'", userId, pszPlotName);
		q.get_result(szQuery);

		if(q.GetErrno())
		{
			logInfo("SELECT accessLimit, noOfThreadExecuted FROM `smith`.`httpUploadTestPlot` SQL Error at %d\n", __LINE__);
			return TRUE;
		}
		else
		{
			while(q.fetch_row())
			{
				UQWORD accessLimit			= 0;
				UQWORD noOfThreadExecuted	= 0;

				accessLimit			= strtoul(q.getstr(0), NULL, 10);
				noOfThreadExecuted	= strtoul(q.getstr(1), NULL, 10);

				if(accessLimit <= noOfThreadExecuted)
				{
					// logDebug("accessLimit: %u,  noOfThreadExecuted: %u  userId: %u  pszPlotName: %s\n", accessLimit, noOfThreadExecuted, userId, pszPlotName);

					// XXX: IMPORTANT - Now we should trigger one job which will make the report by summarizing all
					// the raw data in mongo and just create one record with summarized value. Moreover,
					// it will also delete the raw data from the mongo
					char szCommand[MAX_CMD_SIZE];

					snprintf(szCommand, MAX_CMD_SIZE, "cd %s;%s %u %s", SMITH_CLI_PATH, (char*)gSMITHCP_CLI_SCRIPT[2], userId, escapeShellArg(pszPlotName));

					// logDebug("CLI command: %s\n", szCommand);

					int status = commandExecute(szCommand);

					// successful command
					if(status == 0)
					{
						logDebug("Successfully execute cli script [onRecvRunSTRThreadDone - %d]\n", __LINE__);
					}
					else
					{
						// TODO: Mail to the geeks becoz error happen
						logError("Failed to execute cli script [onRecvRunSTRThreadDone - %d]\n", __LINE__);
					}
				}
			}
		}
	}

	q.free_result();

	return TRUE;
}

/**
 * XXX: if plotStatus is 0 or 3 then the test request will come here
 */
BOOL smithd::onRecvRunDownloadSpecificPlot()
{
	#define CLEAN_UP_DL(x, b) \
		status = x; \
		UBYTE* m_sendData = new UBYTE[b]; \
		dwPos = 0; \
		*(int*)&m_sendData[dwPos] = status; \
		dwPos += 4; \
		zmq::message_t reply(dwPos);  \
		memcpy(reply.data(), m_sendData, dwPos); \
		m_socket->send(reply); \
		FREEP(m_sendData);

	int status				= -1;
	UDWORD dwPos			= 8;
	char szPlotName[256]	= {0};

	// XXX: IMPORTANT - encrypted command parameter skiped start
	dwPos += strlen((char*)&m_recvData[dwPos]) + 1;
	// XXX: IMPORTANT - encrypted command parameter skiped end

	// Data Recv Start
	char* pszUserId = (char*)&m_recvData[dwPos];
	dwPos += strlen((char*)&m_recvData[dwPos])+1;
	UDWORD dwUsedId = (UDWORD) strtoul(pszUserId, NULL, 10);

	char* pszPlotName = (char*)&m_recvData[dwPos];
	dwPos += strlen((char*)&m_recvData[dwPos])+1;
	strcpy(szPlotName, pszPlotName);

	char* pszPlotType = (char*)&m_recvData[dwPos];
	dwPos += strlen((char*)&m_recvData[dwPos])+1;
	// Data Recv End

	if(pszUserId && pszPlotName && (strcmp(pszPlotType, "httpdl") == 0))
	{
		Database db(MYSQL_HOST_ACCESS, MYSQL_USER_ACCESS, MYSQL_PASS_ACCESS, MYSQL_DB_ACCESS);
		Query q(db);

		// XXX: IMPORTANT - move the test plot in 'testing request accepted' mode
		q.DoQuery(1024, "UPDATE `smith`.`httpDownloadTestPlot` SET plotStatus=1 WHERE userId='%s' AND plotName='%s'", pszUserId, pszPlotName);

		// XXX: IMPORTANT - successful command and return status message to caller
		CLEAN_UP_DL(1, 1024)
		logDebug("Plot name %s is going to test for user %s [%s]\n", pszPlotName, pszUserId, pszPlotType);

		// SELECT accessLimit from DB
		char szQuery[1024]		= {0};
		UDWORD accessLimit		= 0;
		char* pszPlotReportDb	= NULL;
		char* pszBaseAddress	= NULL;

		sprintf(szQuery, "SELECT accessLimit, plotReportDb, baseAddress FROM `smith`.`httpDownloadTestPlot` WHERE userId='%s' AND plotName='%s'", pszUserId, pszPlotName);
		q.get_result(szQuery);
		if(q.GetErrno())
		{
			logInfo("SELECT accessLimit FROM `smith`.`httpDownloadTestPlot` SQL Error at %d\n", __LINE__);
			return TRUE;
		}
		else
		{
			while(q.fetch_row())
			{
				accessLimit		= (UDWORD) strtoul(q.getstr("accessLimit"), NULL, 10);
				pszPlotReportDb	= strdup(q.getstr("plotReportDb"));
				pszBaseAddress	= strdup(q.getstr("baseAddress"));
			}
		}

		// XXX: IMPORTANT - Here we are going to check the available test server and threads and then request one/several
		// server to start a test job at same time
		if(accessLimit > 0)
		{
			int nI;

			// XXX: we are going to run a test for a http plot so update 'noOfThreadExecuted' to 0
			q.DoQuery(
				1024,
				"UPDATE `smith`.`httpDownloadTestPlot` SET noOfThreadExecuted=0 WHERE userId='%s' AND plotName='%s'",
				pszUserId, pszPlotName
			);

			// XXX: IMPORTANT - 1st check the status of all STR and update 'gSTRStatus'
			checkAllSTRStatus();

			/*
			 * XXX: IMPORTANT - [ALGORITHM] 1st check if any STR can handle all threads by checking nSTRThreadLeft then employ that one
			 * otherwise split all threads into different STR
			 * TODO: We need a job queuing system so that we hold all the test job and sync with all STR
			 * when all the STR are ready to test all jobs at same time simultaneously then we proceed to do send request
			*/

		    // XXX: IMPORTANT - 1st remove old raw response data from mongo (just in case)
		    // Though, 'createHttpDownloadTestreport.php' script will delete all raw data after analyze.
		    // 1st time there will be no DB name that's why check by 'strlen'
			if(strlen(pszPlotReportDb) > 0)
			{
				mongoc_client_t*		client;
				mongoc_collection_t*	collection;
				const char*				uristr			= "mongodb://127.0.0.1:27017/";
				const char*				collectionName	= "httpDownloadTestPlot";

				client = mongoc_client_new(uristr);

				if(!client)
				{
					logError("Failed to initialize mongo uristr [%d]\n", __LINE__);
					return TRUE;
				}

				collection = mongoc_client_get_collection(client, pszPlotReportDb, collectionName);

				bson_t			b;
				bson_bool_t		mongoRet;
				bson_error_t	error;

				bson_init(&b);
				bson_append_int64(&b, "userId", -1, dwUsedId);
				bson_append_utf8(&b, "plotName", -1, pszPlotName, strlen(pszPlotName));

				mongoRet = mongoc_collection_delete(collection, MONGOC_DELETE_NONE, &b, NULL, &error);

				if(!mongoRet)
				{
					logError("%s\n", error.message);
					return TRUE;
				}

				bson_destroy(&b);
				mongoc_collection_destroy(collection);
				mongoc_client_destroy(client);
			}

		    // XXX: IMPORTANT - choose a mongo database randomly and update 'plotReportDb'
		    short int nRandomReportDbIndex = randomNumber(1, gNumOfPlotReportDB - 1);

		    Database db(MYSQL_HOST_ACCESS, MYSQL_USER_ACCESS, MYSQL_PASS_ACCESS, MYSQL_DB_ACCESS);
			Query updateQuery(db);
			updateQuery.DoQuery(
				1024,
				"UPDATE `smith`.`httpDownloadTestPlot` SET plotReportDb='%s' WHERE userId='%s' AND plotName='%s'",
				szPlotReportDb[nRandomReportDbIndex], pszUserId, pszPlotName
			);
			updateQuery.free_result();

			// XXX: IMPORTANT - Hop count between smithd and test host
			pthread_t ptHops;
			struct stTraceRoute traceRt;
			urlst* url;
			traceRt.dwUsedId = dwUsedId;

			url = parseUrl(pszBaseAddress);
			sprintf(traceRt.szHost, "%s", url->pszHost);
			sprintf(traceRt.szPlotName, "%s", pszPlotName);
			sprintf(traceRt.szTablename, "httpDownloadTestPlot");
			pthread_create(&ptHops, NULL, countMaxHops, (void*)&traceRt);

			// XXX: IMPORTANT - must free the structure
			freeUrl(url);

		    // XXX: IMPORTANT - 1st check that if one STR can handle all jobs then send one request and return
		    for(nI=0; nI!=gListOfThreadRunner; nI++)
			{
		    	if(gSTRStatus[nI][2] >= accessLimit)
				{
					logDebug("smithd got a STR [%d - %s] which can handle all test jobs\n", nI, szListOfThreadRunner[nI]);

					struct stSTRReqValue reqData;
					bzero(&reqData, sizeof(struct stSTRReqValue));

					reqData.nI					= (int) nI;
					reqData.uNoOfThreadToRun	= (UDWORD) accessLimit;
					sprintf(reqData.szUserId,	"%s", pszUserId);
					sprintf(reqData.szPlotName, "%s", pszPlotName);
					reqData.nRandomReportDbIndex = nRandomReportDbIndex;

					HOOKER hooker;

					// Create a hooker of 'uNoOfThreadToRun' thread workers
					if((hooker = newHooker(1, 2, FALSE)) == NULL)
					{
						logError("Failed to create a thread pool struct [%d]\n", __LINE__);
						return TRUE;
					}

					void* statusp;

					int ret = hookerAdd(hooker, downloadTestPlotRunRoutineForSTR, &reqData);

					if(ret == FALSE)
					{
						logError("An error had occurred while adding a task [%d]\n", __LINE__);
						/**
						 * TODO: task is not completed properly so we need to store pszUserId, pszPlotName, pszPlotType
						 * in a separate table called 'unfinishedTestJob' and another process will check it and do the task
						 */

						return TRUE;
					}

				    hookerJoin(hooker, TRUE, &statusp);

					// XXX: IMPORTANT - cleanup hooker
					hookerDestroy(hooker);

					q.free_result();

					return TRUE;
				}
			}

		    int nNumberOfThreadToRun = accessLimit;

		    // XXX: IMPORTANT - 2nd check that if there is no STR can handle all jobs then send multiple request to multiple STR
		    for(nI=0; nI!=gListOfThreadRunner; nI++)
		    {
		    	if(gSTRStatus[nI][2] <= nNumberOfThreadToRun) nNumberOfThreadToRun = gSTRStatus[nI][2];

		    	// XXX: IMPORTANT - just in case
		    	if(nNumberOfThreadToRun < 1) continue;

		    	// XXX: IMPORTANT - 'gSTRStatus[nI][2]' contains how many test job that STR can do at this moment
		    	// gSTRStatus is a global variable and so it can be changed by several other process
				struct stSTRReqValue reqData;
				bzero(&reqData, sizeof(struct stSTRReqValue));

				reqData.nI					= (int) nI;
				reqData.uNoOfThreadToRun	= (UDWORD) nNumberOfThreadToRun;
				sprintf(reqData.szUserId,	"%s", pszUserId);
				sprintf(reqData.szPlotName, "%s", pszPlotName);
				reqData.nRandomReportDbIndex = nRandomReportDbIndex;

				HOOKER hooker;

				// Create a hooker of 'uNoOfThreadToRun' thread workers
				if((hooker = newHooker(1, 2, FALSE)) == NULL)
				{
					logError("Failed to create a thread pool struct [%d]\n", __LINE__);
					return TRUE;
				}

				void* statusp;

				int ret = hookerAdd(hooker, downloadTestPlotRunRoutineForSTR, &reqData);

				if(ret == FALSE)
				{
					logError("An error had occurred while adding a task [%d]\n", __LINE__);
					/**
					 * TODO: task is not completed properly so we need to store pszUserId, pszPlotName, pszPlotType
					 * in a separate table called 'unfinishedTestJob' and another process will check it and do the task
					 */

					return TRUE;
				}

				accessLimit				= accessLimit - nNumberOfThreadToRun;
				nNumberOfThreadToRun	= accessLimit;

			    hookerJoin(hooker, TRUE, &statusp);

				// XXX: IMPORTANT - cleanup hooker
				hookerDestroy(hooker);
		    }

			// XXX: IMPORTANT - take a short nap  for cosmetic effect this does NOT affect performance stats.
			uSleep(10000);
		}
		else
		{
			logDebug("Download test plot accessLimit is invalid for userId=%u and plotName=%s\n", dwUsedId, pszPlotName);
			return TRUE;
		}
	}
	else
	{
		CLEAN_UP_DL(0, 1024)
		logDebug("Download test plot Input data is invalid!!!");
		return TRUE;
	}

	return TRUE;
}

/**
 * XXX: if plotStatus is 0 or 3 then the test request will come here
 */
BOOL smithd::onRecvRunUploadSpecificPlot()
{
	#define CLEAN_UP_UL(x, b) \
		status = x; \
		UBYTE* m_sendData = new UBYTE[b]; \
		dwPos = 0; \
		*(int*)&m_sendData[dwPos] = status; \
		dwPos += 4; \
		zmq::message_t reply(dwPos);  \
		memcpy(reply.data(), m_sendData, dwPos); \
		m_socket->send(reply); \
		FREEP(m_sendData);

	int status				= -1;
	UDWORD dwPos			= 8;
	char szPlotName[256]	= {0};

	// XXX: IMPORTANT - encrypted command parameter skiped start
	dwPos += strlen((char*)&m_recvData[dwPos]) + 1;
	// XXX: IMPORTANT - encrypted command parameter skiped end

	// Data Recv Start
	char* pszUserId = (char*)&m_recvData[dwPos];
	dwPos += strlen((char*)&m_recvData[dwPos])+1;
	UDWORD dwUsedId = (UDWORD) strtoul(pszUserId, NULL, 10);

	char* pszPlotName = (char*)&m_recvData[dwPos];
	dwPos += strlen((char*)&m_recvData[dwPos])+1;
	strcpy(szPlotName, pszPlotName);

	char* pszPlotType = (char*)&m_recvData[dwPos];
	dwPos += strlen((char*)&m_recvData[dwPos])+1;
	// Data Recv End

	if(pszUserId && pszPlotName && (strcmp(pszPlotType, "httpul") == 0))
	{
		Database db(MYSQL_HOST_ACCESS, MYSQL_USER_ACCESS, MYSQL_PASS_ACCESS, MYSQL_DB_ACCESS);
		Query q(db);

		// XXX: IMPORTANT - move the test plot in 'testing request accepted' mode
		q.DoQuery(1024, "UPDATE `smith`.`httpUploadTestPlot` SET plotStatus=1 WHERE userId='%s' AND plotName='%s'", pszUserId, pszPlotName);

		// XXX: IMPORTANT - successful command and return status message to caller
		CLEAN_UP_UL(1, 1024)
		logDebug("Plot name %s is going to test for user %s [%s]\n", pszPlotName, pszUserId, pszPlotType);

		// SELECT accessLimit from DB
		char szQuery[1024]		= {0};
		UDWORD accessLimit		= 0;
		char* pszPlotReportDb	= NULL;
		char* pszBaseAddress	= NULL;

		sprintf(szQuery, "SELECT accessLimit, plotReportDb, baseAddress FROM `smith`.`httpUploadTestPlot` WHERE userId='%s' AND plotName='%s'", pszUserId, pszPlotName);
		q.get_result(szQuery);

		if(q.GetErrno())
		{
			logInfo("SELECT accessLimit, plotReportDb, baseAddress FROM `smith`.`httpUploadTestPlot` WHERE userId='%s' AND plotName='%s'\n", __LINE__);
			return TRUE;
		}
		else
		{
			while(q.fetch_row())
			{
				accessLimit		= (UDWORD) strtoul(q.getstr("accessLimit"), NULL, 10);
				pszPlotReportDb	= strdup(q.getstr("plotReportDb"));
				pszBaseAddress	= strdup(q.getstr("baseAddress"));
			}
		}

		// XXX: IMPORTANT - Here we are going to check the available test server and threads and then request one/several
		// server to start a test job at same time
		if(accessLimit > 0)
		{
			int nI;

			// XXX: we are going to run a test for an upload plot so update 'noOfThreadExecuted' to 0
			q.DoQuery(
				1024,
				"UPDATE `smith`.`httpUploadTestPlot` SET noOfThreadExecuted=0 WHERE userId='%s' AND plotName='%s'",
				pszUserId, pszPlotName
			);

			// XXX: IMPORTANT - 1st check the status of all STR and update 'gSTRStatus'
			checkAllSTRStatus();

			/*
			 * XXX: IMPORTANT - [ALGORITHM] 1st check if any STR can handle all threads by checking nSTRThreadLeft then employ that one
			 * otherwise split all threads into different STR
			 * TODO: We need a job queuing system so that we hold all the test job and sync with all STR
			 * when all the STR are ready to test all jobs at same time simultaneously then we proceed to do send request
			*/

			// XXX: IMPORTANT - 1st remove old raw response data from mongo (just in case)
			// Though, 'createHttpUploadTestreport.php.php' script will delete all raw data after analyze.
			// 1st time there will be no DB name that's why check by 'strlen'
			if(strlen(pszPlotReportDb) > 0)
			{
				mongoc_client_t*		client;
				mongoc_collection_t*	collection;
				const char*				uristr			= "mongodb://127.0.0.1:27017/";
				const char*				collectionName	= "httpUploadTestPlot";

				client = mongoc_client_new(uristr);

				if(!client)
				{
					logError("Failed to initialize mongo uristr [%d]\n", __LINE__);
					return TRUE;
				}

				collection = mongoc_client_get_collection(client, pszPlotReportDb, collectionName);

				bson_t			b;
				bson_bool_t		mongoRet;
				bson_error_t	error;

				bson_init(&b);
				bson_append_int64(&b, "userId", -1, dwUsedId);
				bson_append_utf8(&b, "plotName", -1, pszPlotName, strlen(pszPlotName));

				mongoRet = mongoc_collection_delete(collection, MONGOC_DELETE_NONE, &b, NULL, &error);

				if(!mongoRet)
				{
					logError("%s\n", error.message);
					return TRUE;
				}

				bson_destroy(&b);
				mongoc_collection_destroy(collection);
				mongoc_client_destroy(client);
			}

		    // XXX: IMPORTANT - choose a mongo database randomly and update 'plotReportDb'
		    short int nRandomReportDbIndex = randomNumber(1, gNumOfPlotReportDB - 1);

		    Database db(MYSQL_HOST_ACCESS, MYSQL_USER_ACCESS, MYSQL_PASS_ACCESS, MYSQL_DB_ACCESS);
			Query updateQuery(db);
			updateQuery.DoQuery(
				1024,
				"UPDATE `smith`.`httpUploadTestPlot` SET plotReportDb='%s' WHERE userId='%s' AND plotName='%s'",
				szPlotReportDb[nRandomReportDbIndex], pszUserId, pszPlotName
			);
			updateQuery.free_result();

			// XXX: IMPORTANT - Hop count between smithd and test host
			pthread_t ptHops;
			struct stTraceRoute traceRt;
			urlst* url;
			traceRt.dwUsedId = dwUsedId;

			url = parseUrl(pszBaseAddress);
			sprintf(traceRt.szHost, "%s", url->pszHost);
			sprintf(traceRt.szPlotName, "%s", pszPlotName);
			sprintf(traceRt.szTablename, "httpUploadTestPlot");
			pthread_create(&ptHops, NULL, countMaxHops, (void*)&traceRt);

			// XXX: IMPORTANT - must free the structure
			freeUrl(url);

		    // XXX: IMPORTANT - 1st check that if one STR can handle all jobs then send one request and return
		    for(nI=0; nI!=gListOfThreadRunner; nI++)
			{
		    	if(gSTRStatus[nI][2] >= accessLimit)
				{
					logDebug("smithd got a STR [%d - %s] which can handle all test jobs\n", nI, szListOfThreadRunner[nI]);

					struct stSTRReqValue reqData;
					bzero(&reqData, sizeof(struct stSTRReqValue));

					reqData.nI					= (int) nI;
					reqData.uNoOfThreadToRun	= (UDWORD) accessLimit;
					sprintf(reqData.szUserId,	"%s", pszUserId);
					sprintf(reqData.szPlotName, "%s", pszPlotName);
					reqData.nRandomReportDbIndex = nRandomReportDbIndex;

					HOOKER hooker;

					// Create a hooker of 'uNoOfThreadToRun' thread workers
					if((hooker = newHooker(1, 2, FALSE)) == NULL)
					{
						logError("Failed to create a thread pool struct [%d]\n", __LINE__);
						return TRUE;
					}

					void* statusp;

					int ret = hookerAdd(hooker, uploadTestPlotRunRoutineForSTR, &reqData);

					if(ret == FALSE)
					{
						logError("An error had occurred while adding a task [%d]\n", __LINE__);
						/**
						 * TODO: task is not completed properly so we need to store pszUserId, pszPlotName, pszPlotType
						 * in a separate table called 'unfinishedTestJob' and another process will check it and do the task
						 */

						return TRUE;
					}

				    hookerJoin(hooker, TRUE, &statusp);

					// XXX: IMPORTANT - cleanup hooker
					hookerDestroy(hooker);

					q.free_result();

					return TRUE;
				}
			}

		    int nNumberOfThreadToRun = accessLimit;

		    // XXX: IMPORTANT - 2nd check that if there is no STR can handle all jobs then send multiple request to multiple STR
		    for(nI=0; nI!=gListOfThreadRunner; nI++)
		    {
		    	if(gSTRStatus[nI][2] <= nNumberOfThreadToRun) nNumberOfThreadToRun = gSTRStatus[nI][2];

		    	// XXX: IMPORTANT - just in case
		    	if(nNumberOfThreadToRun < 1) continue;

		    	// XXX: IMPORTANT - 'gSTRStatus[nI][2]' contains how many test job that STR can do at this moment
		    	// gSTRStatus is a global variable and so it can be changed by several other process
				struct stSTRReqValue reqData;
				bzero(&reqData, sizeof(struct stSTRReqValue));

				reqData.nI					= (int) nI;
				reqData.uNoOfThreadToRun	= (UDWORD) nNumberOfThreadToRun;
				sprintf(reqData.szUserId,	"%s", pszUserId);
				sprintf(reqData.szPlotName, "%s", pszPlotName);
				reqData.nRandomReportDbIndex = nRandomReportDbIndex;

				HOOKER hooker;

				// Create a hooker of 'uNoOfThreadToRun' thread workers
				if((hooker = newHooker(1, 2, FALSE)) == NULL)
				{
					logError("Failed to create a thread pool struct [%d]\n", __LINE__);
					return TRUE;
				}

				void* statusp;

				int ret = hookerAdd(hooker, uploadTestPlotRunRoutineForSTR, &reqData);

				if(ret == FALSE)
				{
					logError("An error had occurred while adding a task [%d]\n", __LINE__);
					/**
					 * TODO: task is not completed properly so we need to store pszUserId, pszPlotName, pszPlotType
					 * in a separate table called 'unfinishedTestJob' and another process will check it and do the task
					 */

					return TRUE;
				}

				accessLimit				= accessLimit - nNumberOfThreadToRun;
				nNumberOfThreadToRun	= accessLimit;

			    hookerJoin(hooker, TRUE, &statusp);

				// XXX: IMPORTANT - cleanup hooker
				hookerDestroy(hooker);
		    }

			// XXX: IMPORTANT - take a short nap  for cosmetic effect this does NOT affect performance stats.
			uSleep(10000);
		}
		else
		{
			logDebug("Upload test plot accessLimit is invalid for userId=%u and plotName=%s\n", dwUsedId, pszPlotName);
			return TRUE;
		}
	}
	else
	{
		CLEAN_UP_UL(0, 1024)
		logDebug("Upload test plot Input data is invalid!!!");
		return TRUE;
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
		logError("smith daemon failed to maximum no. of file descriptor in %s at %d\n", __FILE__, __LINE__);
		return (-1);
	}
	// become a session leader to lose controlling TTY
	if((pid = fork()) < 0)
	{
		logError("smith daemon failed to become session leader in %s at %d\n", __FILE__, __LINE__);
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
		logError("smith daemon failed to open /dev/null in %s at %d\n", __FILE__, __LINE__);
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
	if(stat((const char*)SMITH_PID_FILE, &st) == 0)
	{
		logNotice("smithd unlinking PID file %s in %s at %d\n", SMITH_PID_FILE, __FILE__, __LINE__);

		if(unlink((const char*)SMITH_PID_FILE) == -1)
		{
			logCritical("smithd failed to unlink PID file %s in %s at %d\n", SMITH_PID_FILE, __FILE__, __LINE__);
		}
	}

	threadpoolFree(tPoolWorker, 0);
	logNotice("smithd exiting... at %s in %d\n", __FILE__, __LINE__);
	exit(nCode);
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
		smithd* smith		= new smithd();
		smith->m_context	= context;
		smith->m_socket		= &socket;

		while(true)
		{
			// Get a request from the dispatcher.
			zmq::message_t request;
			socket.recv(&request);

			smith->m_request = &request;
			smith->m_recvData = (UBYTE*)request.data();

			smith->onRecvCommand();
		}
    }
	catch(const zmq::error_t& ze)
	{
    	logCritical("Exception - %s\n", ze.what());
    }

    return;
}

void testPlotRunRoutineForSTR(void* arguments)
{
	struct stSTRReqValue* threadData = (struct stSTRReqValue*) arguments;

	int status				= -1;
	UDWORD dwPos			= 0;
	char szPlotName[256]	= {0};

	zmq::context_t	smithThreadRunnerContext(1);
	zmq::socket_t	smithThreadRunnerSocket(smithThreadRunnerContext, ZMQ_REQ);

	int linger = 0;
	smithThreadRunnerSocket.connect(szListOfThreadRunner[threadData->nI]);
	smithThreadRunnerSocket.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));

	UBYTE*			smithThreadRunnerRepData	= new UBYTE[1024];
	UBYTE*			smithThreadRunnerReqData	= new UBYTE[1024];

	dwPos = 0;

	*(int*)&smithThreadRunnerReqData[dwPos] = SMITH_THREAD_RUNNER_CMD_INDEX_HTTP;
	dwPos += 4;

	strcpy((char*)&smithThreadRunnerReqData[dwPos], threadData->szUserId);
	dwPos += strlen((char*)&smithThreadRunnerReqData[dwPos]) + 1;

	strcpy((char*)&smithThreadRunnerReqData[dwPos], threadData->szPlotName);
	dwPos += strlen((char*)&smithThreadRunnerReqData[dwPos]) + 1;

	*(UDWORD*)&smithThreadRunnerReqData[dwPos] = threadData->uNoOfThreadToRun;
	dwPos += 4;

	*(int*)&smithThreadRunnerReqData[dwPos] = threadData->nRandomReportDbIndex;
	dwPos += 4;

	// XXX: IMPORTANT - pass the smithd id so that STR can send status request after the job done
	*(int*)&smithThreadRunnerReqData[dwPos] = gsmithId;
	dwPos += 4;

	zmq::message_t req(dwPos);
	memcpy(req.data(), smithThreadRunnerReqData, dwPos);
	smithThreadRunnerSocket.send(req);

	// Get the reply from STR
	zmq::message_t reply;
	smithThreadRunnerSocket.recv(&reply);
	smithThreadRunnerRepData = (UBYTE*) reply.data();

	dwPos = 0;
	int nSTRStatus			= *(int*)&smithThreadRunnerRepData[dwPos];
	dwPos += 4;
	int nSTRThreadRunning	= *(int*)&smithThreadRunnerRepData[dwPos];
	dwPos += 4;
	int nSTRThreadLeft		= *(int*)&smithThreadRunnerRepData[dwPos];
	dwPos += 4;

	smithThreadRunnerSocket.close();
	smithThreadRunnerRepData 	= NULL;
	smithThreadRunnerReqData	= NULL;

	gSTRStatus[threadData->nI][0] = nSTRStatus;
	gSTRStatus[threadData->nI][1] = nSTRThreadRunning;
	gSTRStatus[threadData->nI][2] = nSTRThreadLeft;

	return;
}

void downloadTestPlotRunRoutineForSTR(void* arguments)
{
	struct stSTRReqValue* threadData = (struct stSTRReqValue*) arguments;

	int status				= -1;
	UDWORD dwPos			= 0;
	char szPlotName[256]	= {0};

	zmq::context_t	smithThreadRunnerContext(1);
	zmq::socket_t	smithThreadRunnerSocket(smithThreadRunnerContext, ZMQ_REQ);

	int linger = 0;
	smithThreadRunnerSocket.connect(szListOfThreadRunner[threadData->nI]);
	smithThreadRunnerSocket.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));

	UBYTE*			smithThreadRunnerRepData	= new UBYTE[1024];
	UBYTE*			smithThreadRunnerReqData	= new UBYTE[1024];

	dwPos = 0;

	*(int*)&smithThreadRunnerReqData[dwPos] = SMITH_THREAD_RUNNER_CMD_INDEX_HTTP_DL;
	dwPos += 4;

	strcpy((char*)&smithThreadRunnerReqData[dwPos], threadData->szUserId);
	dwPos += strlen((char*)&smithThreadRunnerReqData[dwPos]) + 1;

	strcpy((char*)&smithThreadRunnerReqData[dwPos], threadData->szPlotName);
	dwPos += strlen((char*)&smithThreadRunnerReqData[dwPos]) + 1;

	*(UDWORD*)&smithThreadRunnerReqData[dwPos] = threadData->uNoOfThreadToRun;
	dwPos += 4;

	*(int*)&smithThreadRunnerReqData[dwPos] = threadData->nRandomReportDbIndex;
	dwPos += 4;

	// XXX: IMPORTANT - pass the smithd id so that STR can send status request after the job done
	*(int*)&smithThreadRunnerReqData[dwPos] = gsmithId;
	dwPos += 4;

	zmq::message_t req(dwPos);
	memcpy(req.data(), smithThreadRunnerReqData, dwPos);
	smithThreadRunnerSocket.send(req);

	// Get the reply from STR
	zmq::message_t reply;
	smithThreadRunnerSocket.recv(&reply);
	smithThreadRunnerRepData = (UBYTE*) reply.data();

	dwPos = 0;
	int nSTRStatus			= *(int*)&smithThreadRunnerRepData[dwPos];
	dwPos += 4;
	int nSTRThreadRunning	= *(int*)&smithThreadRunnerRepData[dwPos];
	dwPos += 4;
	int nSTRThreadLeft		= *(int*)&smithThreadRunnerRepData[dwPos];
	dwPos += 4;

	smithThreadRunnerSocket.close();
	smithThreadRunnerRepData 	= NULL;
	smithThreadRunnerReqData	= NULL;

	gSTRStatus[threadData->nI][0] = nSTRStatus;
	gSTRStatus[threadData->nI][1] = nSTRThreadRunning;
	gSTRStatus[threadData->nI][2] = nSTRThreadLeft;

	return;
}

void uploadTestPlotRunRoutineForSTR(void* arguments)
{
	struct stSTRReqValue* threadData = (struct stSTRReqValue*) arguments;

	int status				= -1;
	UDWORD dwPos			= 0;
	char szPlotName[256]	= {0};

	zmq::context_t	smithThreadRunnerContext(1);
	zmq::socket_t	smithThreadRunnerSocket(smithThreadRunnerContext, ZMQ_REQ);

	int linger = 0;
	smithThreadRunnerSocket.connect(szListOfThreadRunner[threadData->nI]);
	smithThreadRunnerSocket.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));

	UBYTE*			smithThreadRunnerRepData	= new UBYTE[1024];
	UBYTE*			smithThreadRunnerReqData	= new UBYTE[1024];

	dwPos = 0;

	*(int*)&smithThreadRunnerReqData[dwPos] = SMITH_THREAD_RUNNER_CMD_INDEX_HTTP_UL;
	dwPos += 4;

	strcpy((char*)&smithThreadRunnerReqData[dwPos], threadData->szUserId);
	dwPos += strlen((char*)&smithThreadRunnerReqData[dwPos]) + 1;

	strcpy((char*)&smithThreadRunnerReqData[dwPos], threadData->szPlotName);
	dwPos += strlen((char*)&smithThreadRunnerReqData[dwPos]) + 1;

	*(UDWORD*)&smithThreadRunnerReqData[dwPos] = threadData->uNoOfThreadToRun;
	dwPos += 4;

	*(int*)&smithThreadRunnerReqData[dwPos] = threadData->nRandomReportDbIndex;
	dwPos += 4;

	// XXX: IMPORTANT - pass the smithd id so that STR can send status request after the job done
	*(int*)&smithThreadRunnerReqData[dwPos] = gsmithId;
	dwPos += 4;

	zmq::message_t req(dwPos);
	memcpy(req.data(), smithThreadRunnerReqData, dwPos);
	smithThreadRunnerSocket.send(req);

	// Get the reply from STR
	zmq::message_t reply;
	smithThreadRunnerSocket.recv(&reply);
	smithThreadRunnerRepData = (UBYTE*) reply.data();

	dwPos = 0;
	int nSTRStatus			= *(int*)&smithThreadRunnerRepData[dwPos];
	dwPos += 4;
	int nSTRThreadRunning	= *(int*)&smithThreadRunnerRepData[dwPos];
	dwPos += 4;
	int nSTRThreadLeft		= *(int*)&smithThreadRunnerRepData[dwPos];
	dwPos += 4;

	smithThreadRunnerSocket.close();
	smithThreadRunnerRepData 	= NULL;
	smithThreadRunnerReqData	= NULL;

	gSTRStatus[threadData->nI][0] = nSTRStatus;
	gSTRStatus[threadData->nI][1] = nSTRThreadRunning;
	gSTRStatus[threadData->nI][2] = nSTRThreadLeft;

	return;
}

void checkAllSTRStatus()
{
	int nI;
	UDWORD dwPos;

	for(nI=0; nI<gListOfThreadRunner; nI++)
	{
		zmq::context_t	smithThreadRunnerContext(1);
		zmq::socket_t	smithThreadRunnerSocket(smithThreadRunnerContext, ZMQ_REQ);
		zmq::message_t reply;

		int linger = 0;
		smithThreadRunnerSocket.connect(szListOfThreadRunner[nI]);
		smithThreadRunnerSocket.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));

		UBYTE*			smithThreadRunnerReplyData	= new UBYTE[1024];
		UBYTE*			smithThreadRunnerReqData	= new UBYTE[1024];

		dwPos = 0;
		*(int*)&smithThreadRunnerReqData[dwPos] = SMITH_THREAD_RUNNER_CMD_INDEX_STATUS;
		dwPos += 4;
		zmq::message_t req(dwPos);
		memcpy(req.data(), smithThreadRunnerReqData, dwPos);
		smithThreadRunnerSocket.send(req);

		// Get the reply from smithThreadRunner
		smithThreadRunnerSocket.recv(&reply);
		smithThreadRunnerReplyData = (UBYTE*) reply.data();

		dwPos = 0;
		int nSTRStatus			= *(int*)&smithThreadRunnerReplyData[dwPos];
		dwPos += 4;
		int nSTRThreadRunning	= *(int*)&smithThreadRunnerReplyData[dwPos];
		dwPos += 4;
		int nSTRThreadLeft		= *(int*)&smithThreadRunnerReplyData[dwPos];
		dwPos += 4;

		smithThreadRunnerReplyData 	= NULL;
		smithThreadRunnerReqData	= NULL;

		smithThreadRunnerSocket.close();

		gSTRStatus[nI][0] = nSTRStatus;
		gSTRStatus[nI][1] = nSTRThreadRunning;
		gSTRStatus[nI][2] = nSTRThreadLeft;
	}

	return;
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
			logError("no smithd configuration in smithd config file %s [%d]\n", pszFileName, __LINE__);
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
			logError("no strd configuration in smithd config file %s [%d]\n", pszFileName, __LINE__);
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
			logError("no plotReportDB configuration in smithd config file %s [%d]\n", pszFileName, __LINE__);
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

static void* countMaxHops(void* arguments)
{
	struct timeval tv;
	char szTarget[128];
	int nOption		= 1;
	pthread_t self	= pthread_self();

	// For setting signal handler
	struct sigaction myAction;
	struct stTraceRoute traceRt = *((struct stTraceRoute*)(arguments));

	// XXX: initialize by 0 hops
	traceRt.nHops = 0;

	int sock = socket(PF_INET, SOCK_RAW, IPPROTO_ICMP);

	if(sock < 0)
	{
		logError("ICMP socket creation failed [countMaxHops - %d]\n", __LINE__);
		pthread_exit(NULL);
		return (void*)1;
	}

	tv.tv_sec	= TIMEOUT_SECS;
	// Not init'ing this can cause strange errors
	tv.tv_usec	= 0;

	if(setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(struct timeval)) < 0)
	{
		logError("setsockopt failed [countMaxHops - %d]\n", __LINE__);
		pthread_exit(NULL);
		return (void*)1;
	}

	struct addrinfo* ai;

	if(getaddrinfo(traceRt.szHost, NULL, NULL, &ai ) < 0 )
	{
		logError("getaddrinfo failed [countMaxHops - %d]\n", __LINE__);
		pthread_exit(NULL);
		return (void*)1;
	}

	// get destination by pinging normally
	ping(ai->ai_addr, sock, (uint16_t)0);
	struct sockaddr_in msg = pong(sock);

	int res = getnameinfo((struct sockaddr *)&msg, sizeof(msg), szTarget, sizeof(szTarget), NULL, 0, 0);

	if(res)
	{
		logError("getnameinfo failed [countMaxHops - %d]\n", __LINE__);
		pthread_exit(NULL);
		return (void*)1;
	}

	for(; nOption <= MAX_HOPS; nOption++)
	{
		char hostname[255+1];

		setsockopt(sock, IPPROTO_IP, IP_TTL, &nOption, sizeof(nOption));

		ping(ai->ai_addr, sock, (uint16_t)0);
		struct sockaddr_in msg = pong(sock);

		res = getnameinfo((struct sockaddr *)&msg, sizeof(msg), hostname, sizeof(hostname), NULL, 0, 0);

		if(res)
		{
			logError("getnameinfo failed [countMaxHops - %d]\n", __LINE__);
			pthread_exit(NULL);
			return (void*)1;
		}

		//logInfo("from=%s\n", hostname);

		if(strcmp(hostname, szTarget) == 0)
		{
			traceRt.nHops = nOption;
			logInfo("Total hops for host %s is %d\n", traceRt.szHost, nOption);
			break;
		}
		else
		{
			if(nOption == MAX_HOPS)
			{
				// XXX: IMPORTANT - from PHP check traceRt.nHops > MAX_HOPS then display
				// 'more than <MAX_HOPS> hops'. That's why wee add 'nOption + 1'
				traceRt.nHops = nOption + 1;
				logInfo("More hops exist for host %s\n", traceRt.szHost);
			}
		}
	}

	// XXX: IMPORTANT - update maxHops in mysql
	Database db(MYSQL_HOST_ACCESS, MYSQL_USER_ACCESS, MYSQL_PASS_ACCESS, MYSQL_DB_ACCESS);
	Query q(db);

	// XXX: IMPORTANT - move the test plot in 'testing request accepted' mode
	q.DoQuery(
		1024,
		"UPDATE `smith`.`%s` SET maxHops=%d WHERE userId='%u' AND plotName='%s'",
		traceRt.szTablename, traceRt.nHops, traceRt.dwUsedId, traceRt.szPlotName
	);

	q.free_result();

	pthread_exit(NULL);
	return (void*)1;
}

/**
 * daemonWritePid(void)
 * Write the process ID of the server into the file specified in <path>.
 * The file is automatically deleted on a call to daemonExit().
 */
static int daemonWritePid(void)
{
	int nFd;
	char szBuff[16]	= {0};
	nFd				= safeOpen((const char*)SMITH_PID_FILE, O_RDWR | O_CREAT, LOCKMODE);

	if(nFd < 0)
	{
		logError("daemonWritePid - smithd failed to open \"%s\" [%d]\n", SMITH_PID_FILE, __LINE__);
		return (-1);
	}

	if(writeLockFile(nFd) < 0)
	{
		if(errno == EACCES || errno == EAGAIN)
		{
			close(nFd);
			return (1);
		}

		logCritical("daemonWritePid - smithd can't lock %s [%d]\n", SMITH_PID_FILE, __LINE__);
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
		logError("daemonPrivDrop - smithd failed to set group ID to %u [%d]\n", gDaemonGid, __LINE__);
		return (-1);
	}

	if((geteuid() != gDaemonUid) && (seteuid(gDaemonUid) == -1))
	{
		logError("daemonPrivDrop - smithd failed to set user ID to %u [%d]\n", gDaemonUid, __LINE__);
		return (-1);
	}

	logInfo("daemonPrivDrop - smithd dropped privileges to %u:%u [%d]\n" , gDaemonUid, gDaemonGid, __LINE__);
	return (0);
}

/**
 * Deal with signals
 * LOCKING: acquires and releases configlock
 */
static void* signalThread(void* arg){

	int	nError, nSigno;

	nError = sigwait(&mask, &nSigno);
	if(nError != 0){
		logCritical("signalThread - smithd sigwait failed %s [%d]\n", strerror(nError), __LINE__);
		daemonExit(1);
	}

	switch(nSigno){

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
	logInfo("signalThread - smithd receive SIGHUP signal %s\n", strsignal(nSigno));
	//TODO: Schedule to re-read the configuration file.
	//reReadConf();
}

void sigTERM(int nSigno)
{
	logInfo("signalThread - smithd terminate with signal %s\n", strsignal(nSigno));
	daemonExit(1);
}

void sigQUIT(int nSigno)
{
	logInfo("signalThread - smithd terminate with signal %s\n", strsignal(nSigno));
	daemonExit(1);
}

/**
 *
 */
void help(void)
{
	printf("smith Daemon by 800cycles <info@800cycles.com>\n");
	printf("Usage: /etc/init.d/x.startup start\n"
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
		{"pid",		required_argument,	NULL, 0		},
		{"env",		required_argument,	NULL, 'e'	},
		{"port",	required_argument,	NULL, 0		},
		{"config",	required_argument,	NULL, 0		},
		{"myhost",	required_argument,	NULL, 0		},
		{"myuser",	required_argument,	NULL, 0		},
		{"mypass",	required_argument,	NULL, 0		},
		{"myport",	required_argument,	NULL, 0		},
		{"mydb",	required_argument,	NULL, 0		},
		{"cli",		required_argument,	NULL, 0		},
		{"help",	0, 					NULL, 'h'	},
		{0,			0, 					NULL, 0		}
	};

	char* pszPid	= NULL;
	char* pszPort	= NULL;
	char* pszConfig	= NULL;
	char* pszCli	= NULL;

	// default settings
	strcpy(SMITH_PID_FILE, "/var/run/smithd.pid");
	strcpy(APPLICATION_ENV, "local");
	strcpy(SMITH_CONFIG_FILE, "/etc/smith.conf");
	strcpy(SMITH_CLI_PATH, "/home/smthcp/cli/");

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
						strcpy(SMITH_PID_FILE, "/var/run/smithd.pid");
					}
					else
					{
						strcpy(SMITH_PID_FILE, pszPid);
					}

					pszPid = NULL;
				}

				if(strcmp(pszOpt->name, "port") == 0)
				{
					if((pszPort = safeStrdup(optarg)) == NULL)
					{
						gwPort = (UWORD)5555;
					}
					else
					{
						gwPort = (UWORD)strtoul(pszPort, NULL, 10);
					}

					pszPort = NULL;
				}

				if(strcmp(pszOpt->name, "config") == 0)
				{
					if((pszConfig = safeStrdup(optarg)) == NULL)
					{
						strcpy(SMITH_CONFIG_FILE, "/etc/smith.conf");
					}
					else
					{
						strcpy(SMITH_CONFIG_FILE, pszConfig);
					}

					pszConfig = NULL;
				}

				if(strcmp(pszOpt->name, "cli") == 0)
				{
					if((pszCli = safeStrdup(optarg)) == NULL)
					{
						strcpy(SMITH_CLI_PATH, "/home/smthcp/cli/");
					}
					else
					{
						strcpy(SMITH_CLI_PATH, pszCli);
					}

					pszCli = NULL;
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
		logError("Failed to become a daemon [init - %d]\n", __LINE__);
		exit(1);
	}

	// make sure only one copy of the daemon is running
	if(daemonWritePid())
	{
		logError("smithd already running [init - %d]\n", __LINE__);
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
		printf("smith daemon port not initialized...\n");
		logError("main - smithd port not initialized [%d]\n", __LINE__);
		exit(1);
	}

	if(!gpszProtocol) gpszProtocol = (char*)"tcp";

	// initialize the daemon
	init();

	int nParseStatus = parseConfigFile(SMITH_CONFIG_FILE);

	gSTRStatus = (int**) malloc(gListOfThreadRunner * sizeof(int*));

	// XXX: IMPORTANT - change 3 if you add more status component in gSTRStatus
	for(int nI=0; nI<gListOfThreadRunner; nI++) gSTRStatus[nI] = (int*) malloc(3 * sizeof(int));

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
	// set smithd id
	for(nI=0; nI<gListOfSmithd; nI++)
	{
		if(strcmp(szListOfSmithd[nI], gConnection) == 0)
		{
			gsmithId = nI;
			break;
		}
	}


    // We'll use thread pool to generate a lot of thread to support simultaneous access to phantom
    if((tPoolWorker = threadpoolInit(SMITH_WORKER_THREAD)) == NULL)
	{
		logError("Failed to create a thread pool struct.\n");
		exit(1);
	}

    for(nNumberOfThreadI = 0; nNumberOfThreadI!= SMITH_WORKER_THREAD; nNumberOfThreadI++)
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

    printf("smith daemon shutting down...\n");
    threadpoolFree(tPoolWorker, 0);
	return 0;
}
