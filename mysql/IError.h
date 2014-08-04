//=============================================================================
//	File name: IError.h
//	Description:
//	Tab-Width: 4
//  Author: rockmetoo
//-----------------------------------------------------------------------------

#ifndef __ierror_h__
#define __ierror_h__

#include <string>

#ifdef MYSQLW_NAMESPACE
namespace MYSQLW_NAMESPACE{
#endif

class Database;
class Query;

//Log class interface.
class IError
{
public:
	IError();
	virtual void error(Database&, const std::string&) = 0;
	virtual void error(Database&, Query&, const std::string&) = 0;
	virtual ~IError();
};


#ifdef MYSQLW_NAMESPACE
}
#endif

#endif //__ierror_h__
