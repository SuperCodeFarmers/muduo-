#ifndef MUDUO_NET_INSPECT_INSPECTOR_H
#define MUDUO_NET_INSPECT_INSPECTOR_H

#include "muduo/base/Mutex.h"
#include "muduo/net/http/HttpRequest.h"
#include "muduo/net/http/HttpServer.h"

#include <map>

//Inspector对象一定要在主线程中构造

namespace muduo
{
	namespace net
	{

		class ProcessInspector;
		class PerformanceInspector;
		class SystemInspector;

		// An internal inspector of the running process, usually a singleton.
		// Better to run in a seperated thread, as some method may block for seconds

		// 检查员
		//运行进程的内部检查器，通常是单例的
		//最好在单独的线程中运行，因为某些方法可能会阻塞几秒钟
		class Inspector : noncopyable
		{
		public:
			typedef std::vector<string> ArgList;
			typedef std::function<string(HttpRequest::Method, const ArgList & args)> Callback;
			Inspector(EventLoop* loop,
				const InetAddress& httpAddr,
				const string& name);
			~Inspector();

			/// Add a Callback for handling the special uri : /mudule/command
			void add(const string& module,
				const string& command,
				const Callback& cb,
				const string& help);
			void remove(const string& module, const string& command);

		private:
			typedef std::map<string, Callback> CommandList;	// 命令列表，<命令名称, 命令处理函数>
			typedef std::map<string, string> HelpList;	// 帮助列表，<命令名称, 帮助信息>

			void start();
			void onRequest(const HttpRequest& req, HttpResponse* resp);

			// 监控类是一个http服务器,所以包含了一个HttpServer对象
			HttpServer server_;
			std::unique_ptr<ProcessInspector> processInspector_;//命令程序对象
			std::unique_ptr<PerformanceInspector> performanceInspector_;
			std::unique_ptr<SystemInspector> systemInspector_;
			MutexLock mutex_;
			std::map<string, CommandList> modules_ GUARDED_BY(mutex_);//命令名和命令的回调函数
			std::map<string, HelpList> helps_ GUARDED_BY(mutex_);
		};

	}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_INSPECT_INSPECTOR_H
