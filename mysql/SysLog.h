//=============================================================================
//	File name: SysLog.h
//	Description:
//	Tab-Width: 4
//  Author: rockmetoo
//-----------------------------------------------------------------------------

#ifndef __syslog_h__
#define __syslog_h__

#include <syslog.h>
#include <string.h>

// Log class writing to syslog.
class SysLog : public IError{

	public:
		SysLog(const std::string& = "database", int = LOG_PID, int = LOG_USER);
		virtual ~SysLog();

		void error(Database&,const std::string&);
		void error(Database&,Query&,const std::string&);

};

#endif
