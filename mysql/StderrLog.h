//=============================================================================
//	File name: StderrLog.h
//	Description:
//	Tab-Width: 4
//  Author: rockmetoo
//-----------------------------------------------------------------------------

#ifndef __stderrlog_h__
#define __stderrlog_h__

// Log class writing to stderr.
class StderrLog : public IError{

	public:
		void error(Database&, const std::string&);
		void error(Database&, Query&,const std::string&);
};

#endif
