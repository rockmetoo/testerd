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
