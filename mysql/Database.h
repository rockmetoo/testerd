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
