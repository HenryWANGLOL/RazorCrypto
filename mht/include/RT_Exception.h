//Created by Yufeng Wang

#pragma once
#include <string>
#include <stdexcept>
/**
@brief Base class of exception of

usage:
sm_throw("formatted string %d %s", 2, "haha"); //args are just like in printf


catch exception:
try
{
	//your code here
}
catch(mht::Exception e)
{
	//do something with e. error message will be output, so you don't actually need to cout again.
}

*/

// 如果要抛异常, 注释掉下面这行, 否则只是输出错误并退出。通常抛异常会有诸多不便，在windows下会弹出错误窗口等待点击卡住后续任务，在Linux下则会dump core占用大量磁盘空间。


namespace mht_rt
{
	#define k4_NO_THROW

	#ifdef k4_NO_THROW
	#define k4_throw(...) new Exception(__FILE__, __LINE__, __VA_ARGS__)
	#define k4_assert(condi, ...) { if (!(condi)) new mht_rt::Exception(__FILE__, __LINE__, __VA_ARGS__); }
	#else
	#define k4_throw(...) throw Exception(__FILE__, __LINE__, __VA_ARGS__)
	#define k4_assert(condi, ...) { if (!(condi)) throw mht_rt::Exception(__FILE__, __LINE__, __VA_ARGS__); }
	#endif

class Exception : public std::runtime_error
{
	public:
		Exception(const char * filename, int linenum, const char * error_msg, ...);
		~Exception();

		std::string getErrorMsg() const { return strErrorMsg; }

	protected:
	private:
		void __for_debug() const {
			// 构造函数会调用本函数，可以把断点设在此以便捕捉
			return;
		}
		std::string strErrorMsg;
};


};	// namespace
