//=============================================================================
//	File name: StderrLog.cpp
//	Description:
//	Tab-Width: 4
//  Author: rockmetoo
//-----------------------------------------------------------------------------

#include <stdio.h>
#include <string>
#include <map>
#include <mysql/mysql.h>
#include "Database.h"
#include "Query.h"
#include "IError.h"
#include "StderrLog.h"


#ifdef MYSQLW_NAMESPACE
namespace MYSQLW_NAMESPACE{
#endif
//=============================================================================
//-----------------------------------------------------------------------------
void StderrLog::error(Database& db,const std::string& str)
{
	time_t t = time(NULL);
	struct tm *tp = localtime(&t);
	fprintf(stderr,"%d-%02d-%02d %02d:%02d:%02d :: Database: %s\n",
		tp -> tm_year + 1900,tp -> tm_mon + 1,tp -> tm_mday,
		tp -> tm_hour,tp -> tm_min, tp -> tm_sec,
		str.c_str());
}
//=============================================================================
//-----------------------------------------------------------------------------
void StderrLog::error(Database& db,Query& q,const std::string& str)
{
	time_t t = time(NULL);
	struct tm *tp = localtime(&t);
	fprintf(stderr,"%d-%02d-%02d %02d:%02d:%02d :: Query: %s: %s(%d)\n",
		tp -> tm_year + 1900,tp -> tm_mon + 1,tp -> tm_mday,
		tp -> tm_hour,tp -> tm_min, tp -> tm_sec,
		str.c_str(),q.GetError().c_str(),q.GetErrno());
	fprintf(stderr," (QUERY: \"%s\")\n",q.GetLastQuery().c_str());
}

#ifdef MYSQLW_NAMESPACE
}
#endif
