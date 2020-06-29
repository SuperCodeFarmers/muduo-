#ifndef MUDUO_NET_TCPCONNECTION_H
#define MUDUO_NET_TCPCONNECTION_H

#include "muduo/base/noncopyable.h"
#include "muduo/base/StringPiece.h"
#include "muduo/base/Types.h"
#include "muduo/net/Callbacks.h"
#include "muduo/net/Buffer.h"
#include "muduo/net/InetAddress.h"

#include <memory>

#include <boost/any.hpp>

// struct tcp_info is in <netinet/tcp.h>
struct tcp_info;

namespace muduo
{
	namespace net
	{

		class Channel;
		class EventLoop;
		class Socket;

		///
		/// TCP connection, for both client and server usage.
		///
		/// This is an interface class, so don't expose too much details.
		class TcpConnection : noncopyable,
			public std::enable_shared_from_this<TcpConnection>
		{
		public:
			/// Constructs a TcpConnection with a connected sockfd
			///
			/// User should not create this object.
			TcpConnection(EventLoop* loop,
				const string& name,
				int sockfd,
				const InetAddress& localAddr,
				const InetAddress& peerAddr);
			~TcpConnection();

			EventLoop* getLoop() const { return loop_; }
			const string& name() const { return name_; }
			const InetAddress& localAddress() const { return localAddr_; }
			const InetAddress& peerAddress() const { return peerAddr_; }
			bool connected() const { return state_ == kConnected; }
			bool disconnected() const { return state_ == kDisconnected; }
			// return true if success.
			bool getTcpInfo(struct tcp_info*) const;
			string getTcpInfoString() const;

			// void send(string&& message); // C++11
			void send(const void* message, int len);
			void send(const StringPiece& message);
			// void send(Buffer&& message); // C++11
			void send(Buffer* message);  // this one will swap data
			void shutdown(); // NOT thread safe, no simultaneous calling
			// void shutdownAndForceCloseAfter(double seconds); // NOT thread safe, no simultaneous calling
			void forceClose();
			void forceCloseWithDelay(double seconds);
			void setTcpNoDelay(bool on);
			// reading or not
			void startRead();
			void stopRead();
			bool isReading() const { return reading_; }; // NOT thread safe, may race with start/stopReadInLoop

			// 把一个未知类型赋值给context_
			void setContext(const boost::any& context)
			{
				context_ = context;
			}

			//获取该未知类型的时候不会更改该context_
			const boost::any& getContext() const
			{
				// context_在外边不可改变
				return context_;
			}

			//获取外界提供的对象,可以改变该对象
			boost::any* getMutableContext()
			{
				// context_在外边可改变
				return &context_;
			}

			/*
			应用程序不会调用TcpConnnnection的setCallback，他会调用TcpServer的setCallback
			*/
			void setConnectionCallback(const ConnectionCallback& cb)
			{
				connectionCallback_ = cb;
			}

			void setMessageCallback(const MessageCallback& cb)
			{
				messageCallback_ = cb;
			}

			void setWriteCompleteCallback(const WriteCompleteCallback& cb)
			{
				writeCompleteCallback_ = cb;
			}

			// 设置高水位标回调函数
			void setHighWaterMarkCallback(const HighWaterMarkCallback& cb, size_t highWaterMark)
			{
				highWaterMarkCallback_ = cb; highWaterMark_ = highWaterMark;
			}

			/// Advanced interface
			Buffer* inputBuffer()
			{
				return &inputBuffer_;
			}

			Buffer* outputBuffer()
			{
				return &outputBuffer_;
			}

			/// Internal use only.
			//只能用于内部使用
			void setCloseCallback(const CloseCallback& cb)
			{
				closeCallback_ = cb;
			}

			// called when TcpServer accepts a new connection
			void connectEstablished();   // should be called only once

			// called when TcpServer has removed me from its map
			//当TcpServer将我从其映射中删除时调用
			//连接摧毁
			void connectDestroyed();  // should be called only once

		private:
			//连接状态
			enum StateE { kDisconnected/*关闭连接*/, kConnecting/*正在连接*/, kConnected/*连接成功*/, kDisconnecting/*正在关闭连接*/ };
			void handleRead(Timestamp receiveTime);
			void handleWrite();
			void handleClose();
			void handleError();

			// void sendInLoop(string&& message);
			void sendInLoop(const StringPiece& message);
			void sendInLoop(const void* message, size_t len);
			void shutdownInLoop();
			// void shutdownAndForceCloseInLoop(double seconds);
			void forceCloseInLoop();
			void setState(StateE s) { state_ = s; }
			const char* stateToString() const;
			void startReadInLoop();
			void stopReadInLoop();

			EventLoop* loop_;//所属EventLoop
			const string name_;//连接名称
			StateE state_;  // 连接状态，FIXME: use atomic variable
			bool reading_;
			// we don't expose those classes to client.
			//
			std::unique_ptr<Socket> socket_;
			std::unique_ptr<Channel> channel_;

			//一个连接他有两个地址，一个本地地址，一个对等方地址
			const InetAddress localAddr_;
			const InetAddress peerAddr_;

			//连接回调函数在TcpServer中设置，在newConnection()中设置
			ConnectionCallback connectionCallback_;

			//消息回调函数在TcpServer中设置，在newConnection()中设置
			MessageCallback messageCallback_;

			// 数据发送完毕回调函数,即所有的用户数据都已拷贝到内核缓冲区时回调该函数
			// output buffer被清空的时候也会回调该函数，这意味着所有的用户数据都拷贝到内核缓冲区了，
			// 可以理解为低水位标回调函数,也可以理解为output buffer被清空了没有数据,把他当成水位,水位是低的，低水位的时候回调一下该函数
			WriteCompleteCallback writeCompleteCallback_;

			// 高水位标回调函数，当output buffer达到一定程度的时候回调该函数
			// 调用该函数意味着对等方接收不及时,导致output buffer不断增大，在该函数中，我们可以设置断开该连接，防止output buffer被撑爆
			HighWaterMarkCallback highWaterMarkCallback_;

			//关闭连接回调函数，内部的断开连接回调函数,它是TcpServer中的removeConnection()这个函数
			CloseCallback closeCallback_;
			size_t highWaterMark_;	// 高水位标的最大值，当达到该值就要回调高水位标函数，断开连接，防止output buffer被撑爆
			Buffer inputBuffer_;    // 应用层接收缓冲区
			Buffer outputBuffer_;   // 应用层发送缓冲区   FIXME: use list<Buffer> as output buffer.

			boost::any context_;    //可以与外界的任意类型的对象进行绑定，能够接收任意类型的对象
									//context_在muduo中用在了httpserver中

			// FIXME: creationTime_, lastReceiveTime_
			//        bytesReceived_, bytesSent_
		};

		typedef std::shared_ptr<TcpConnection> TcpConnectionPtr;

	}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_TCPCONNECTION_H
