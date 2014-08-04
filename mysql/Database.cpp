//=============================================================================
//	File name: Database.cpp
//	Description:
//	Tab-Width: 4
//  Author: rockmetoo
//-----------------------------------------------------------------------------

#include <stdio.h>
#include <string>
#include <map>
#include <stdlib.h>
#include <string.h>
#include <mysql/mysql.h>
#include <stdarg.h>
#include "IError.h"
#include "Database.h"

//=============================================================================
//-----------------------------------------------------------------------------
Database::Database(const std::string& d, IError* e)
			: database(d), m_errhandler(e), m_embedded(true)
			, m_mutex(m_mutex), m_b_use_mutex(false){}
//=============================================================================
//-----------------------------------------------------------------------------
Database::Database(Mutex& m, const std::string& d, IError* e)
			: database(d), m_errhandler(e), m_embedded(true)
			, m_mutex(m), m_b_use_mutex(true){}
//=============================================================================
//-----------------------------------------------------------------------------
Database::Database(
		const std::string& h, const std::string& u,
		const std::string& p, const std::string& d, IError* e
)
			: host(h), user(u), password(p), database(d)
			, m_errhandler(e), m_embedded(false), m_mutex(m_mutex)
			, m_b_use_mutex(false){}
//=============================================================================
//-----------------------------------------------------------------------------
Database::Database(
		Mutex& m, const std::string& h, const std::string& u,
		const std::string& p, const std::string& d, IError* e
)
			: host(h), user(u), password(p), database(d)
			, m_errhandler(e), m_embedded(false), m_mutex(m)
			, m_b_use_mutex(true){}
//=============================================================================
//-----------------------------------------------------------------------------
Database::~Database(){

	for(opendb_v::iterator it = m_opendbs.begin(); it != m_opendbs.end(); it++){
		OPENDB* p = *it;
		mysql_close(&p -> mysql);
	}
	while(m_opendbs.size()){
		opendb_v::iterator it = m_opendbs.begin();
		OPENDB* p = *it;
		if(p -> busy){
			error("destroying Database object before Query object");
		}
		delete p;
		m_opendbs.erase(it);
	}
}
//=============================================================================
//-----------------------------------------------------------------------------
void Database::OnMyInit(OPENDB* odb){

	mysql_options(&odb->mysql, MYSQL_SET_CHARSET_NAME, "utf8");
	// using embedded server (libmysqld)
	if(m_embedded){
		mysql_options(&odb->mysql, MYSQL_READ_DEFAULT_GROUP, "test_libmysqld_CLIENT");
	}
}
//=============================================================================
//-----------------------------------------------------------------------------
void Database::RegErrHandler(IError* p){

	m_errhandler = p;
}
//=============================================================================
//-----------------------------------------------------------------------------
Database::OPENDB* Database::grabdb(){

	Lock lck(m_mutex, m_b_use_mutex);
	OPENDB* odb = NULL;

	for(opendb_v::iterator it = m_opendbs.begin(); it != m_opendbs.end(); it++){
		odb = *it;
		if(!odb -> busy){
			break;
		}else{
			odb = NULL;
		}
	}
	if(!odb){
		odb = new OPENDB;
		if(!odb){
			error("grabdb: OPENDB struct couldn't be created");
			return NULL;
		}
		if(!mysql_init(&odb -> mysql)){
			error("mysql_init() failed - list size %d",m_opendbs.size());
			delete odb;
			return NULL;
		}
		// use callback to set mysql_options() before connect, etc
		this->OnMyInit(odb);
		if(m_embedded)		{
			if(!mysql_real_connect(&odb -> mysql, NULL, NULL, NULL, database.c_str(), 0, NULL, 0)){
				error("mysql_real_connect(NULL, NULL, NULL, %s, 0, NULL, 0) failed - list size %d", database.c_str(), m_opendbs.size());
				delete odb;
				return NULL;
			}
		}else{
			if(
				!mysql_real_connect(&odb -> mysql, host.c_str(), user.c_str(), password.c_str(), database.c_str(), 0, NULL, 0)
			){
				error(
					"mysql_real_connect(%s, %s, ***, %s, 0, NULL, 0) failed - list size %d"
					, host.c_str(), user.c_str(), database.c_str(), m_opendbs.size()
				);
				delete odb;
				return NULL;
			}
		}
		odb -> busy = true;
		m_opendbs.push_back(odb);
	}else{
		if(mysql_ping(&odb -> mysql)){
			error("mysql_ping() failed when reusing an old connection from the connection pool");
		}
		odb -> busy = true;
	}
	return odb;
}
//=============================================================================
//-----------------------------------------------------------------------------
void Database::freedb(Database::OPENDB* odb){

	Lock lck(m_mutex, m_b_use_mutex);
	if(odb){
		odb -> busy = false;
	}
}
//=============================================================================
//-----------------------------------------------------------------------------
void Database::error(const char* pszFormat, ...){

	if(m_errhandler){
		va_list ap;
		char errstr[5000];
		va_start(ap, pszFormat);
		vsnprintf(errstr, 5000, pszFormat, ap);
		va_end(ap);
		m_errhandler -> error(*this, errstr);
	}
}
//=============================================================================
//-----------------------------------------------------------------------------
void Database::error(Query& q, const char* pszFormat, ...){

	if(m_errhandler){
		va_list ap;
		char errstr[5000];
		va_start(ap, pszFormat);
		vsnprintf(errstr, 5000, pszFormat, ap);
		va_end(ap);
		m_errhandler -> error(*this, q, errstr);
	}
}
//=============================================================================
//-----------------------------------------------------------------------------
bool Database::Connected(){

	OPENDB* odb = grabdb();
	if(!odb){
		return false;
	}
	int ping_result = mysql_ping(&odb -> mysql);
	if(ping_result){
		error("mysql_ping() failed");
	}
	freedb(odb);
	return ping_result ? false : true;
}
//=============================================================================
//-----------------------------------------------------------------------------
Database::Lock::Lock(Mutex& mutex,bool use) : m_mutex(mutex),m_b_use(use){

	if(m_b_use) m_mutex.Lock();
}
//=============================================================================
//-----------------------------------------------------------------------------
Database::Lock::~Lock(){

	if(m_b_use) m_mutex.Unlock();
}
//=============================================================================
//-----------------------------------------------------------------------------
Database::Mutex::Mutex(){

	pthread_mutex_init(&m_mutex, NULL);
}
//=============================================================================
//-----------------------------------------------------------------------------
Database::Mutex::~Mutex(){

	pthread_mutex_destroy(&m_mutex);
}
//=============================================================================
//-----------------------------------------------------------------------------
void Database::Mutex::Lock(){

	pthread_mutex_lock(&m_mutex);
}
//=============================================================================
//-----------------------------------------------------------------------------
void Database::Mutex::Unlock(){

	pthread_mutex_unlock(&m_mutex);
}
//=============================================================================
//-----------------------------------------------------------------------------
std::string Database::safestr(const std::string& str){

	std::string str2;
	size_t stSize = str.size();
	for(size_t i = 0; i < stSize; i++){
		switch(str[i]){
		case '\'':
		case '\\':
		case 34:
			str2 += '\\';
			break;
		default:
			str2 += str[i];
			break;
		}
	}
	return str2;
}
//=============================================================================
//-----------------------------------------------------------------------------
std::string Database::unsafestr(const std::string& str){

	std::string str2;
	size_t stSize = str.size();
	for(size_t i = 0; i < stSize; i++){
		if(str[i] == '\\'){
			i++;
		}
		if(i < str.size()){
			str2 += str[i];
		}
	}
	return str2;
}
//=============================================================================
//-----------------------------------------------------------------------------
std::string Database::xmlsafestr(const std::string& str){

	std::string str2;
	size_t stSize = str.size();
	for(size_t i = 0; i < stSize; i++){
		switch (str[i]){
			case '&':
				str2 += "&amp;";
				break;
			case '<':
				str2 += "&lt;";
				break;
			case '>':
				str2 += "&gt;";
				break;
			case '"':
				str2 += "&quot;";
				break;
			case '\'':
				str2 += "&apos;";
				break;
			default:
				str2 += str[i];
				break;
		}
	}
	return str2;
}
//=============================================================================
//-----------------------------------------------------------------------------
int64_t Database::a2bigint(const std::string& str){

	int64_t val = 0;
	bool sign = false;
	size_t i = 0;
	if(str[i] == '-'){
		sign = true;
		i++;
	}
	size_t stSize = str.size();
	for(; i < stSize; i++){
		val = val * 10 + (str[i] - 48);
	}
	return sign ? -val : val;
}
//=============================================================================
//-----------------------------------------------------------------------------
uint64_t Database::a2ubigint(const std::string& str){

	uint64_t val = 0;
	size_t stSize = str.size();
	for(size_t i = 0; i < stSize; i++){
		val = val * 10 + (str[i] - 48);
	}
	return val;
}
