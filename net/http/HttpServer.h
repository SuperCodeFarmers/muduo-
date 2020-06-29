#ifndef MUDUO_NET_HTTP_HTTPSERVER_H
#define MUDUO_NET_HTTP_HTTPSERVER_H

#include "muduo/net/TcpServer.h"

namespace muduo
{
	namespace net
	{

		class HttpRequest;
		class HttpResponse;

		//HTTP服务器类封装
		class HttpServer : noncopyable
		{
		public:
			typedef std::function<void(const HttpRequest&,
				HttpResponse*)> HttpCallback;

			HttpServer(EventLoop* loop,
				const InetAddress& listenAddr,
				const string& name,
				TcpServer::Option option = TcpServer::kNoReusePort);

			EventLoop* getLoop() const { return server_.getLoop(); }

			/// Not thread safe, callback be registered before calling start().
			void setHttpCallback(const HttpCallback& cb)
			{
				httpCallback_ = cb;
			}

			//支持多线程
			void setThreadNum(int numThreads)
			{
				server_.setThreadNum(numThreads);
			}

			void start();

		private:
			//链接
			void onConnection(const TcpConnectionPtr& conn);
			//消息
			void onMessage(const TcpConnectionPtr& conn,
				Buffer* buf,
				Timestamp receiveTime);
			//请求
			void onRequest(const TcpConnectionPtr&, const HttpRequest&);
			/*
			HTTP协议
			在应用层是HTTP协议
			在传输层还是TCP协议
			所以HttpServer还是用的TCP协议，因此包含一个TcpServer
			*/
			TcpServer server_;

			/*
			当服务端收到一个客户端发过来的http请求时，首先回调onMessage()这时一个tcp请求
			在onMessage()中再回调了onRequest(),在onRequest()中又回调了一个用户的httpCallback
			这样子，用户就能使用自己提供的一个处理http请求的操作
			*/
			HttpCallback httpCallback_;//在处理http请求(即调用onRequest)的过程中调用此函数，对请求进行具体的处理
		};

	}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_HTTP_HTTPSERVER_H

/*
当一个HTTP请求到来时，先调用onMessage函数，在onMessage当中又调用了onRequest，
在onRequest中又回调了一个用户的httpCallback_
*/