#ifndef __smithd_h__
#define __smithd_h__

#include "common.h"
#include <zmq.hpp>

int 	m_argc;
char** 	m_argv;


class testerBrokerd
{
	private:

	public:
		testerBrokerd();
		~testerBrokerd();

		BOOL onRecvCommand();
		BOOL onRecvRunSpecificPlot();
		BOOL onRecvRunGetSTRStatusById();
		BOOL onRecvRunSTRThreadDone();
		BOOL onRecvRunDownloadSpecificPlot();
		BOOL onRecvRunUploadSpecificPlot();

		int system(const char* cmdstring);

		int m_byCmd;
		zmq::message_t* m_request;
		zmq::context_t* m_context;
		zmq::socket_t*	m_socket;
		UBYTE*			m_recvData;
};

#endif
