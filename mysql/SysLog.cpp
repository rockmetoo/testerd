//=============================================================================
//	File name: SysLog.cpp
//	Description:
//	Tab-Width: 4
//  Author: rockmetoo
//-----------------------------------------------------------------------------

#include <mysql/mysql.h>
#include <syslog.h>
#include "Database.h"
#include "Query.h"
#include "IError.h"
#include "SysLog.h"

#ifdef MYSQLW_NAMESPACE
namespace MYSQLW_NAMESPACE{
#endif
//=============================================================================
//-----------------------------------------------------------------------------
SysLog::SysLog(const std::string& appname, int option, int facility)
{
	static char blah[100];
	strcpy(blah, appname.c_str());
	openlog(blah, option, facility);
}
//=============================================================================
//-----------------------------------------------------------------------------
SysLog::~SysLog()
{
	closelog();
}
//=============================================================================
//-----------------------------------------------------------------------------
void SysLog::error(Database& db, const std::string& str)
{
	syslog(LOG_ERR, "%s", str.c_str() );
}
//=============================================================================
//-----------------------------------------------------------------------------
void SysLog::error(Database& db, Query& q, const std::string& str)
{
	syslog(LOG_ERR, "%s: %s(%d)", str.c_str(),q.GetError().c_str(),q.GetErrno() );
	syslog(LOG_ERR, "QUERY: \"%s\"", q.GetLastQuery().c_str());
}

#ifdef MYSQLW_NAMESPACE
}
#endif
