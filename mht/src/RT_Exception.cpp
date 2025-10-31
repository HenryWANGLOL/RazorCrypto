//Created by Yufeng Wang
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include "RT_Exception.h"

namespace mht_rt
{
Exception::Exception(const char * filename, int linenum, const char * error_msg, ...) : std::runtime_error(error_msg)
{
	char * msg = new char[100000];
	va_list args;
	va_start(args, error_msg);
	vsprintf(msg, error_msg, args);	//vsprintf功能与sprintf一样，只是用args代替了...
	va_end(args);

	fprintf(stderr, "Exception caught:\nin file %s, line %d.\nMessage: %s\n"
		"to debug in gdb, use: b %s:%d\n",
		filename, linenum, msg, __FILE__, __LINE__+1);
	strErrorMsg = msg;
	__for_debug();
	delete[] msg;
#ifdef k4_NO_THROW
	exit(-3);
#endif
}

Exception::~Exception()
{
	//dtor
}

};	// namespace

