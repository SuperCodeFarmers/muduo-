// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_SOCKET_H
#define MUDUO_NET_SOCKET_H

#include "muduo/base/noncopyable.h"

// struct tcp_info is in <netinet/tcp.h>
struct tcp_info;

namespace muduo
{
	///
	/// TCP networking.
	///
	namespace net
	{

		class InetAddress;

		///
		/// Wrapper of socket file descriptor.
		///
		/// It closes the sockfd when desctructs.
		/// It's thread safe, all operations are delagated to OS.
		class Socket : noncopyable
		{
		public:
			//构造函数在构造的时候就把sockfd_给赋值了
			explicit Socket(int sockfd)
				: sockfd_(sockfd)
			{ }

			// Socket(Socket&&) //移动语义
			//我们使用RAII方式来封装，所以在析构的是否就直接把sock给close掉
			~Socket();

			int fd() const { return sockfd_; }
			// return true if success.
			bool getTcpInfo(struct tcp_info*) const;
			bool getTcpInfoString(char* buf, int len) const;

			/// abort if address in use
			void bindAddress(const InetAddress& localaddr);
			/// abort if address in use
			void listen();

			// On success, returns a non-negative integer that is
			// a descriptor for the accepted socket, which has been
			// set to non-blocking and close-on-exec. *peeraddr is assigned.
			// On error, -1 is returned, and *peeraddr is untouched.
			int accept(InetAddress* peeraddr);

			void shutdownWrite();

			//
			//Enable/disable TCP_NODELAY (disable/enable Nagle's algorithm).
			//启用/禁用TCP_NODELAY(禁用/启用Nagle算法)。
			//禁用Nagle算法，可以避免连续发包出现延迟，这对于编写低延迟的网络服务很重要
			void setTcpNoDelay(bool on);

			///
			// Enable/disable SO_REUSEADDR
			// 启用/禁用SO_REUSEADDR
			// 设置地址是否重复利用
			void setReuseAddr(bool on);

			//
			// Enable/disable SO_REUSEPORT
			// 设置端口是否重复利用
			void setReusePort(bool on);

			///
			// Enable/disable SO_KEEPALIVE
			// TCP keepalive是指定期探测连接是否存在，如果应用层有心跳包，这个选项是不需要设置的
			void setKeepAlive(bool on);

		private:
			//socket类只有一个数据成员，即套接字
			const int sockfd_;
		};

	}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_SOCKET_H
