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
