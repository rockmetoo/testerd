/**
 * @filemname: log.c
 * @createdon: October 26, 2010
 * @description: Implementation of a logger that appends log messages to a file
 * with a preceding timestamp. Methods support both syslog or own logfile.
 * @author: rockmetoo
 */

#include "log.h"

static FILE* LOG = NULL;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct logPrio{
	int		nPriority;
	char*	pszDescription;
	}logPriority[] = {
	{LOG_EMERG,   "Emergency"},
	{LOG_ALERT,   "Alert"},
	{LOG_CRIT,    "Critical"},
	{LOG_ERR,     "Error"},
	{LOG_WARNING, "Warning"},
	{LOG_NOTICE,  "Notice"},
	{LOG_INFO,    "Info"},
	{LOG_DEBUG,   "Debug"},
	{-1,          NULL}
};

/**
 * Private decleration
 */
static int			openLog();
static char*		timeFormat(char* pszTime, int nSize);
static const char*	logPriorityDescription(int nPriority);
static void			logLog(int nPriority, const char* pszString, va_list ap);
static void			logBacktrace();

/**
 * Initialize the log system and 'log' function
 * @return TRUE if the log system was successfully initialized
 */
int logInit()
{
	if(!openLog()) return FALSE;

	//Register logClose to be called at program termination
	atexit(logClose);
	return TRUE;
}
//=============================================================================
//	Logging interface with priority support
//	@param pszString A formated (printf-style) string to log
//-----------------------------------------------------------------------------
void logEmergency(const char* pszString, ...){
	if(logInit()){
		va_list ap;
		ASSERT(pszString);
		va_start(ap, pszString);
		logLog(LOG_EMERG, pszString, ap);
		va_end(ap);
		logBacktrace();
	}
}
//=============================================================================
//	Logging interface with priority support
//	@param s A formated (printf-style) string to log
//-----------------------------------------------------------------------------
void logAlert(const char* pszString, ...){
	if(logInit()){
		va_list ap;
		ASSERT(pszString);
		va_start(ap, pszString);
		logLog(LOG_ALERT, pszString, ap);
		va_end(ap);
		logBacktrace();
	}
}
//=============================================================================
//	Logging interface with priority support
//	@param s A formated (printf-style) string to log
//-----------------------------------------------------------------------------
void logCritical(const char* pszString, ...){
	if(logInit()){
		va_list ap;
		ASSERT(pszString);
		va_start(ap, pszString);
		logLog(LOG_CRIT, pszString, ap);
		va_end(ap);
		logBacktrace();
	}
}
//=============================================================================
//	Logging interface with priority support
//	@param pszString A formated (printf-style) string to log
//-----------------------------------------------------------------------------
void logError(const char* pszString, ...){
	if(logInit()){
		va_list ap;
		ASSERT(pszString);
		va_start(ap, pszString);
		logLog(LOG_ERR, pszString, ap);
		va_end(ap);
		logBacktrace();
	}
}
//=============================================================================
//	Logging interface with priority support
//	@param pszString A formated (printf-style) string to log
//-----------------------------------------------------------------------------
void logWarning(const char* pszString, ...){
	if(logInit()){
		va_list ap;
		ASSERT(pszString);
		va_start(ap, pszString);
		logLog(LOG_WARNING, pszString, ap);
		va_end(ap);
	}
}
//=============================================================================
//	Logging interface with priority support
//	@param pszString A formated (printf-style) string to log
//-----------------------------------------------------------------------------
void logNotice(const char* pszString, ...){
	if(logInit()){
		va_list ap;
		ASSERT(pszString);
		va_start(ap, pszString);
		logLog(LOG_NOTICE, pszString, ap);
		va_end(ap);
	}
}
//=============================================================================
//	Logging interface with priority support
//	@param pszString A formated (printf-style) string to log
//-----------------------------------------------------------------------------
void logInfo(const char* pszString, ...){
	if(logInit()){
		va_list ap;
		ASSERT(pszString);
		va_start(ap, pszString);
		logLog(LOG_INFO, pszString, ap);
		va_end(ap);
	}
}
//=============================================================================
//	Logging interface with priority support
//	@param pszString A formated (printf-style) string to log
//-----------------------------------------------------------------------------
void logDebug(const char* pszString, ...){
	if(logInit()){
		va_list ap;
		ASSERT(pszString);
		va_start(ap, pszString);
		logLog(LOG_DEBUG, pszString, ap);
		va_end(ap);
	}
}
//=============================================================================
//	Close the log file or syslog
//-----------------------------------------------------------------------------
void logClose(){
	if(g_nIsSyslog){
		closelog();
	}
	if(LOG  && (0 != fclose(LOG))){
		logError("%s: Error closing the log file -- %s\n",	g_szProgName, STRERROR);
	}
	LOG = NULL;
}
//=============================================================================
//	Open a log file or syslog
//-----------------------------------------------------------------------------
static int openLog(){
	if(g_nIsSyslog){
		openlog(g_szProgName, LOG_PID, g_nFacility);
	}
	else{
		if(!LOG){
			umask(LOGMASK);
			if((LOG = fopen(g_szLogFileName, "a+")) == (FILE*)NULL){
				//if own log file creation failed then error this log to syslog
				g_nIsSyslog = 1;
				logError(
					"%s: Error opening the log file '%s' for writing -- %s\n"
					, g_szProgName, g_szLogFileName, STRERROR
				);
				return (FALSE);
			}
			//Set logger in unbuffered mode
			setvbuf(LOG, NULL, _IONBF, 0);
		}
	}
	return TRUE;
}
//=============================================================================
//	Returns the current time as a formated string
//-----------------------------------------------------------------------------
static char* timeFormat(char* pszTime, int nSize){
	time_t now;
	struct tm tm;
	time(&now);
	localtime_r(&now, &tm);
	if(!strftime(pszTime, nSize, TIMEFORMAT, &tm))
	*pszTime = 0;
	return pszTime;
}
//=============================================================================
//	Get a textual description of the actual log priority.
//	@param nPriority The log priority
//	@return A string describing the log priority in clear text. If the
//	priority is not found NULL is returned.
//-----------------------------------------------------------------------------
static const char* logPriorityDescription(int nPriority){
	struct logPrio* lp = logPriority;
	while((*lp).pszDescription){
		if(nPriority == (*lp).nPriority){
			return (*lp).pszDescription;
		}
		lp++;
	}
	return "unknown";
}
//=============================================================================
//	Log a message to logfile or syslog.
//	@param nPriority A message priority
//	@param pszString A formated (printf-style) string to log
//-----------------------------------------------------------------------------
static void logLog(int nPriority, const char* pszString, va_list ap){
	va_list ap_copy;
	ASSERT(pszString);
	LOCK(log_mutex)
	va_copy(ap_copy, ap);
	vfprintf(stderr, pszString, ap_copy);
	va_end(ap_copy);
	fflush(stderr);
	if(g_nIsSyslog){
		va_copy(ap_copy, ap);
		vsyslog(nPriority, pszString, ap_copy);
		va_end(ap_copy);
	}else if(LOG){
		char datetime[256];
		fprintf(LOG, "[%s] %s: ", timeFormat(datetime, 256), logPriorityDescription(nPriority));
		va_copy(ap_copy, ap);
		vfprintf(LOG, pszString, ap_copy);
		va_end(ap_copy);
	}
	END_LOCK;
}
//=============================================================================
//-----------------------------------------------------------------------------
static void logBacktrace(){
	int nI, nFrames;
	void* vCallstack[128];
	char** pszStrs;
	if(g_nDebug){
		nFrames = backtrace(vCallstack, 128);
		pszStrs = backtrace_symbols(vCallstack, nFrames);
		logDebug("-------------------------------------------------------------------------------\n");
		for (nI = 0; nI < nFrames; ++nI) logDebug("    %s\n", pszStrs[nI]);
		logDebug("-------------------------------------------------------------------------------\n");
		FREEP(pszStrs);
	}
}
//=============================================================================
//End of program
//-----------------------------------------------------------------------------
