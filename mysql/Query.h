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
