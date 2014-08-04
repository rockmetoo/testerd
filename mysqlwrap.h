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
//=============================================================================
//	File name: enum_t.h
//	Description:
//	Tab-Width: 4
//  Author: rockmetoo
//-----------------------------------------------------------------------------

#ifndef __enum_t_h__
#define __enum_t_h__

#include <string>
#include <map>
#include <stdint.h>

// Implements MySQL ENUM datatype.
class enum_t{

	public:
		enum_t(std::map<std::string, uint64_t>&);

		const std::string& String();
		unsigned short Value();
		const char* c_str();

		void operator=(const std::string&);
		void operator=(unsigned short);
		bool operator==(const std::string&);
		bool operator==(unsigned short);
		bool operator!=(const std::string&);

	private:
		std::map<std::string, uint64_t>& m_mmap;
		std::map<unsigned short, std::string> m_vmap;
		unsigned short m_value;
};

#endif
//=============================================================================
//	File name: set_t.h
//	Description:
//	Tab-Width: 4
//  Author: rockmetoo
//-----------------------------------------------------------------------------

#ifndef __set_t_h__
#define __set_t_h__

#include <string>
#include <map>
#include <stdint.h>

// Implements MySQL SET datatype.
class set_t{

	public:
		set_t(std::map<std::string, uint64_t>&);

		const std::string& String();
		uint64_t Value();
		const char *c_str();

		bool in_set(const std::string&);

		void operator=(const std::string&);
		void operator=(uint64_t);
		void operator|=(uint64_t);
		void operator&=(uint64_t);
		void operator+=(const std::string&);
		void operator-=(const std::string&);

	private:
		std::map<std::string, uint64_t>& m_mmap;
		uint64_t m_value;
		std::string m_strvalue;
};

#endif
//=============================================================================
//	File name: Database.h
//	Description:
//	Tab-Width: 4
//  Author: rockmetoo
//-----------------------------------------------------------------------------

#ifndef __database_h__
#define __database_h__

#include <pthread.h>
#include <string>
#include <list>
#include <stdint.h>

class IError;
class Query;
class Mutex;

// Connection information and pooling
class Database{

	// Mutex container class, used by Lock. Ingroup threading
	public:
		class Mutex{
			public:
				Mutex();
				~Mutex();
				void Lock();
				void Unlock();
			private:
				pthread_mutex_t m_mutex;
		};
		// Mutex helper class

	private:
		class Lock{
		public:
			Lock(Mutex& mutex,bool use);
			~Lock();
		private:
			Mutex& m_mutex;
			bool m_b_use;
		};

	public:
		// Connection pool struct.
		struct OPENDB{
			OPENDB() : busy(false){}
			MYSQL mysql;
			bool busy;
		};
		typedef std::list<OPENDB*> opendb_v;

	public:
		// Use embedded libmysqld.
		Database(const std::string& database, IError* = NULL);
		// Use embedded libmysqld + thread safe.
		Database(Mutex& ,const std::string& database, IError* = NULL);
		// Connect to a MySQL server
		Database(
			const std::string& host,
			const std::string& user,
			const std::string& password = "",
			const std::string& database = "",
			IError* = NULL
		);
		// Connect to a MySQL server + thread safe
		Database(
			Mutex& , const std::string& host,
			const std::string& user,
			const std::string& password = "",
			const std::string& database = "",
			IError* = NULL
		);
		// Destructor can be override in derived class
		virtual ~Database();
		// Callback after mysql_init
		virtual void OnMyInit(OPENDB*);
		// Try to establish connection with given host
		bool Connected();
		void RegErrHandler(IError*);
		void error(Query&, const char* pszFormat, ...);

		//=============================================================================
		// Request a database connection.
		// The "grabdb" method is used by the Query class, so that each object instance of Query gets a unique
		// When the Query object is deleted, then "freedb" is called - the database connection stays open in the
		// m_opendbs vector. New Query objects can then reuse old connections.
		//=============================================================================
		OPENDB* grabdb();
		void freedb(OPENDB* odb);
		// utility
		std::string safestr(const std::string& );
		std::string unsafestr(const std::string& );
		std::string xmlsafestr(const std::string& );

		int64_t a2bigint(const std::string& );
		uint64_t a2ubigint(const std::string& );

	private:
		Database(const Database& ) : m_mutex(m_mutex) {}
		Database& operator=(const Database& ) { return* this; }
		void error(const char* pszFormat, ...);

		std::string host;
		std::string user;
		std::string password;
		std::string database;
		opendb_v m_opendbs;
		IError *m_errhandler;
		bool m_embedded;
		Mutex& m_mutex;
		bool m_b_use_mutex;
};

#endif
//=============================================================================
//	File name: Query.h
//	Description:
//	Tab-Width: 4
//  Author: rockmetoo
//-----------------------------------------------------------------------------

#ifndef __query_h__
#define __query_h__

#include <string>
#include <map>
#include <stdint.h>

// SQL Statement execute / result set helper class.
class Query{

	public:
		//Constructor accepting reference to database object.
							Query(Database& dbin);
		//Constructor accepting reference to database object and query to execute.
							Query(Database& dbin, const std::string& sql);
							~Query();
		//Check to see if database object is connectable.
		bool				Connected();
		//Return reference to database object.
		Database&			GetDatabase() const;
		//Return string of last query executed.
		const std::string&	GetLastQuery();
		//execute() returns true if query is successful, does not store result.
		bool				execute(const std::string& sql);
		bool				DoQuery(size_t stLen, const char* pszSql, ...);
		//execute query and store result.
		MYSQL_RES*			get_result(const std::string& sql);
		//free stored result, must be called after get_result()
		void				free_result();
		//Fetch next result row. return false if there was no row to fetch (end of rows)
		MYSQL_ROW			fetch_row();
		//Get id of last insert.
		my_ulonglong		insert_id();
		//Returns number of rows returned by last select call.
		long				num_rows();
		//Number of columns in current result.
		int					num_cols();
		//Last error string.
		std::string			GetError();
		//Last error code.
		int					GetErrno();

		//Check if column x in current row is null.
		bool				is_null(const std::string& x);
		bool				is_null(int x);
		bool				is_null();

		//Execute query and return first result as a string.
		const char*			get_string(const std::string& sql);
		//Execute query and return first result as a long integer.
		long				get_count(const std::string& sql);
		//Execute query and return first result as a double.
		double				get_num(const std::string& sql);

		const char*			getstr(const std::string& x);
		const char*			getstr(int x);
		const char*			getstr();
		long				getval(const std::string& x);
		long				getval(int x);
		long				getval();
		unsigned long		getuval(const std::string& x);
		unsigned long		getuval(int x);
		unsigned long		getuval();
		int64_t				getbigint(const std::string& x);
		int64_t				getbigint(int x);
		int64_t				getbigint();
		uint64_t			getubigint(const std::string& x);
		uint64_t			getubigint(int x);
		uint64_t			getubigint();
		double				getnum(const std::string& x);
		double				getnum(int x);
		double				getnum();
		std::string			safestr(const std::string& x);

	protected:
							Query(Database* dbin);
							Query(Database* dbin,const std::string& sql);
	private:
							Query(const Query& q) : m_db(q.GetDatabase()) {}
		Query&				operator=(const Query& ) { return* this; }
		void				error(const std::string& );
		Database&			m_db;
		Database::OPENDB*	odb;
		MYSQL_RES* 			res;
		MYSQL_ROW			row;
		short				rowcount;
		std::string			m_tmpstr;
		std::string			m_last_query;
		std::map<std::string, int> m_nmap;
		int					m_num_cols;
		char				szFieldName[128];
		char				szTableName[128];
		MYSQL_FIELD*		m_mysql_field;
};

#endif
