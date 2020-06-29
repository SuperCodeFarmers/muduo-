#ifndef MUDUO_NET_TCPSERVER_H
#define MUDUO_NET_TCPSERVER_H

#include "muduo/base/Atomic.h"
#include "muduo/base/Types.h"
#include "muduo/net/TcpConnection.h"

#include <map>

namespace muduo
{
	namespace net
	{

		class Acceptor;
		class EventLoop;
		class EventLoopThreadPool;

		///
		/// TCP server, supports single-threaded and thread-pool models.
		///
		/// This is an interface class, so don't expose too much details.
		class TcpServer : noncopyable
		{
		public:
			typedef std::function<void(EventLoop*)> ThreadInitCallback;
			enum Option
			{
				kNoReusePort,
				kReusePort,
			};

			//TcpServer(EventLoop* loop, const InetAddress& listenAddr);
			TcpServer(EventLoop* loop,
				const InetAddress& listenAddr,
				const string& nameArg,
				Option option = kNoReusePort);
			~TcpServer();  // force out-line dtor, for std::unique_ptr members.

			const string& ipPort() const { return ipPort_; }
			const string& name() const { return name_; }
			EventLoop* getLoop() const { return loop_; }

			/// Set the number of threads for handling input.
			///
			/// Always accepts new connection in loop's thread.
			/// Must be called before @c start
			/// @param numThreads
			/// - 0 means all I/O in loop's thread, no thread will created.
			///   this is the default value.
			/// - 1 means all I/O in another thread.
			/// - N means a thread pool with N threads, new connections
			///   are assigned on a round-robin basis.
			void setThreadNum(int numThreads);
			void setThreadInitCallback(const ThreadInitCallback& cb)
			{
				threadInitCallback_ = cb;
			}
			/// valid after calling start()
			std::shared_ptr<EventLoopThreadPool> threadPool()
			{
				return threadPool_;
			}

			/// Starts the server if it's not listenning.
			///
			/// It's harmless to call it multiple times.
			/// Thread safe.
			void start();

			/// Set connection callback.
			/// Not thread safe.
			//设置连接到来或连接关闭的回调函数
			void setConnectionCallback(const ConnectionCallback& cb)
			{
				connectionCallback_ = cb;
			}

			/// Set message callback.
			/// Not thread safe.
			//设置消息到来回调函数
			void setMessageCallback(const MessageCallback& cb)
			{
				messageCallback_ = cb;
			}

			/// Set write complete callback.
			/// Not thread safe.
			void setWriteCompleteCallback(const WriteCompleteCallback& cb)
			{
				writeCompleteCallback_ = cb;
			}

		private:
			/// Not thread safe, but in loop
			   //连接到来时，会回调的函数
			void newConnection(int sockfd, const InetAddress& peerAddr);
			/// Thread safe.
			void removeConnection(const TcpConnectionPtr& conn);
			/// Not thread safe, but in loop
			void removeConnectionInLoop(const TcpConnectionPtr& conn);

			//key:连接名称，value:连接对象的指针
			typedef std::map<string, TcpConnectionPtr> ConnectionMap;

			EventLoop* loop_;  //acceptor_所属的EventLoop， the acceptor loop
			const string ipPort_;	//服务端口
			const string name_;	//服务名
			std::unique_ptr<Acceptor> acceptor_; // avoid revealing Acceptor 接收连接的套接字
			std::shared_ptr<EventLoopThreadPool> threadPool_;//IO线程池
			ConnectionCallback connectionCallback_;//连接到来的回调函数
			MessageCallback messageCallback_;//消息到来的回调函数
			WriteCompleteCallback writeCompleteCallback_;
			ThreadInitCallback threadInitCallback_;
			AtomicInt32 started_;			//是否已经启动
			// always in loop thread
			int nextConnId_;				//写一个连接ID
			ConnectionMap connections_;	//连接列表
		};

	}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_TCPSERVER_H
