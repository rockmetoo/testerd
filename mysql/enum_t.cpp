//=============================================================================
//	File name: enum_t.cpp
//	Description:
//	Tab-Width: 4
//  Author: rockmetoo
//-----------------------------------------------------------------------------

#include "enum_t.h"

#ifdef MYSQLW_NAMESPACE
namespace MYSQLW_NAMESPACE{
#endif

static int strcasecmp(const char *a, const char *b){
	register int n;
	while(*a == *b || (n = tolower (*a) - tolower (*b)) == 0){
		if(*a == '\0') return 0;
		a++, b++;
	}
	return n;
}

//=============================================================================
//-----------------------------------------------------------------------------
enum_t::enum_t(std::map<std::string, uint64_t>& ref) : m_mmap(ref), m_value(0){

	for(std::map<std::string, uint64_t>::iterator it = ref.begin(); it != ref.end(); it++){
		std::string str = (*it).first;
		uint64_t value = (*it).second;
		m_vmap[value] = str;
	}
}
//=============================================================================
//-----------------------------------------------------------------------------
void enum_t::operator=(const std::string& str)
{
	m_value = m_mmap[str];
}
//=============================================================================
//-----------------------------------------------------------------------------
void enum_t::operator=(unsigned short s){

	m_value = s;
}
//=============================================================================
//-----------------------------------------------------------------------------
const std::string& enum_t::String(){

	if(m_vmap.size() != m_mmap.size()){
		for(std::map<std::string, uint64_t>::iterator it = m_mmap.begin(); it != m_mmap.end(); it++){
			std::string str = (*it).first;
			uint64_t value = (*it).second;
			m_vmap[value] = str;
		}
	}
	return m_vmap[m_value];
}
//=============================================================================
//-----------------------------------------------------------------------------
unsigned short enum_t::Value(){

	return m_value;
}
//=============================================================================
//-----------------------------------------------------------------------------
bool enum_t::operator==(const std::string& str){

	if(!strcasecmp(c_str(), str.c_str()))
		return true;
	return false;
}
//=============================================================================
//-----------------------------------------------------------------------------
bool enum_t::operator==(unsigned short s){

	return m_value == s;
}
//=============================================================================
//-----------------------------------------------------------------------------
const char* enum_t::c_str(){

	return String().c_str();
}
//=============================================================================
//-----------------------------------------------------------------------------
bool enum_t::operator!=(const std::string& str){

	if(!strcasecmp(c_str(), str.c_str()))
		return !true;
	return !false;
}


#ifdef MYSQLW_NAMESPACE
}
#endif
