// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/net/SocketsOps.h"

#include "muduo/base/Logging.h"
#include "muduo/base/Types.h"
#include "muduo/net/Endian.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>  // snprintf
#include <sys/socket.h>
#include <sys/uio.h>  // readv
#include <unistd.h>

using namespace muduo;
using namespace muduo::net;

namespace
{

	//定义套接字地址类型
	typedef struct sockaddr SA;


#if VALGRIND || defined (NO_ACCEPT4)

	//设置socket为非阻塞模式
	void setNonBlockAndCloseOnExec(int sockfd)
	{
		// non-block
		  //先获取套接字标志
		int flags = ::fcntl(sockfd, F_GETFL, 0);
		//在此标志上加上O_NONBLOCK
		flags |= O_NONBLOCK;
		//再设置其socket标志位
		int ret = ::fcntl(sockfd, F_SETFL, flags);
		// FIXME check

		// close-on-exec
		flags = ::fcntl(sockfd, F_GETFD, 0);
		flags |= FD_CLOEXEC;
		ret = ::fcntl(sockfd, F_SETFD, flags);
		// FIXME check

		(void)ret;
	}
#endif

}  // namespace

//sockaddr_in是网际地址，sockaddr是通用地址
//将网际地址转成sockaddr*通用地址
const struct sockaddr* sockets::sockaddr_cast(const struct sockaddr_in6* addr)
{
	//先隐含转化为const void*指针，在强制转化为const struct sockaddr*类型指针
	return static_cast<const struct sockaddr*>(implicit_cast<const void*>(addr));
}

struct sockaddr* sockets::sockaddr_cast(struct sockaddr_in6* addr)
{
	return static_cast<struct sockaddr*>(implicit_cast<void*>(addr));
}

const struct sockaddr* sockets::sockaddr_cast(const struct sockaddr_in* addr)
{
	return static_cast<const struct sockaddr*>(implicit_cast<const void*>(addr));
}

const struct sockaddr_in* sockets::sockaddr_in_cast(const struct sockaddr* addr)
{
	return static_cast<const struct sockaddr_in*>(implicit_cast<const void*>(addr));
}

const struct sockaddr_in6* sockets::sockaddr_in6_cast(const struct sockaddr* addr)
{
	return static_cast<const struct sockaddr_in6*>(implicit_cast<const void*>(addr));
}

//创建非阻塞套接字，创建失败就终止
int sockets::createNonblockingOrDie(sa_family_t family)
{
#if VALGRIND
	//VALGRIND是一个内存检测工具，检测内存泄漏

	//创建一个套接字
	int sockfd = ::socket(family, SOCK_STREAM, IPPROTO_TCP);
	if (sockfd < 0)
	{
		LOG_SYSFATAL << "sockets::createNonblockingOrDie";
	}

	//将返回的套接字设置为非阻塞模式
	setNonBlockAndCloseOnExec(sockfd);
#else
	//创建一个套接字
	//在Linux2.6以上的内核支持SOCK_NONBLOCK与SOCK_CLOEXEC
	int sockfd = ::socket(family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
	if (sockfd < 0)
	{
		LOG_SYSFATAL << "sockets::createNonblockingOrDie";
	}
#endif
	return sockfd;
}

void sockets::bindOrDie(int sockfd, const struct sockaddr* addr)
{
	int ret = ::bind(sockfd, addr, static_cast<socklen_t>(sizeof(struct sockaddr_in6)));
	if (ret < 0)
	{
		LOG_SYSFATAL << "sockets::bindOrDie";
	}
}

void sockets::listenOrDie(int sockfd)
{
	int ret = ::listen(sockfd, SOMAXCONN);
	if (ret < 0)
	{
		LOG_SYSFATAL << "sockets::listenOrDie";
	}
}

//接受连接
int sockets::accept(int sockfd, struct sockaddr_in6* addr)
{
	socklen_t addrlen = static_cast<socklen_t>(sizeof * addr);
#if VALGRIND || defined (NO_ACCEPT4)
	int connfd = ::accept(sockfd, sockaddr_cast(addr), &addrlen);
	setNonBlockAndCloseOnExec(connfd);
#else
	int connfd = ::accept4(sockfd, sockaddr_cast(addr),
		&addrlen, SOCK_NONBLOCK | SOCK_CLOEXEC);
#endif
	if (connfd < 0)
	{
		int savedErrno = errno;
		LOG_SYSERR << "Socket::accept";
		//当连接失败时，查看返回的错误是什么错误，如果不是致命的错误就返回回来
		switch (savedErrno)
		{
		case EAGAIN:
		case ECONNABORTED:
		case EINTR:
		case EPROTO: // ???
		case EPERM:
		case EMFILE: // per-process lmit of open file desctiptor ???
		  // expected errors
			errno = savedErrno;
			break;
		case EBADF:
		case EFAULT:
		case EINVAL:
		case ENFILE:
		case ENOBUFS:
		case ENOMEM:
		case ENOTSOCK:
		case EOPNOTSUPP:
			// unexpected errors
			LOG_FATAL << "unexpected error of ::accept " << savedErrno;
			break;
		default:
			LOG_FATAL << "unknown error of ::accept " << savedErrno;
			break;
		}
	}
	return connfd;
}

int sockets::connect(int sockfd, const struct sockaddr* addr)
{
	return ::connect(sockfd, addr, static_cast<socklen_t>(sizeof(struct sockaddr_in6)));
}

ssize_t sockets::read(int sockfd, void* buf, size_t count)
{
	return ::read(sockfd, buf, count);
}

//readv与read不同之处在于，接收的数据可以填充到多个缓冲区中
ssize_t sockets::readv(int sockfd, const struct iovec* iov, int iovcnt)
{
	return ::readv(sockfd, iov, iovcnt);
}

ssize_t sockets::write(int sockfd, const void* buf, size_t count)
{
	return ::write(sockfd, buf, count);
}

void sockets::close(int sockfd)
{
	if (::close(sockfd) < 0)
	{
		LOG_SYSERR << "sockets::close";
	}
}

//只关闭写的这一半套接字
void sockets::shutdownWrite(int sockfd)
{
	if (::shutdown(sockfd, SHUT_WR) < 0)
	{
		LOG_SYSERR << "sockets::shutdownWrite";
	}
}

//将addr装换成IP和Port的形式保存到buf中
void sockets::toIpPort(char* buf, size_t size,
	const struct sockaddr* addr)
{
	toIp(buf, size, addr);
	size_t end = ::strlen(buf);
	const struct sockaddr_in* addr4 = sockaddr_in_cast(addr);
	//将port转成16位无符号整数
	uint16_t port = sockets::networkToHost16(addr4->sin_port);
	assert(size > end);
	//把端口格式化到buf中
	snprintf(buf + end, size - end, ":%u", port);
}

//把addr中的网际地址转成字符串形式，存在buf中
void sockets::toIp(char* buf, size_t size,
	const struct sockaddr* addr)
{
	if (addr->sa_family == AF_INET)
	{
		//断言判断 size要》=网际地址长度的大小
		assert(size >= INET_ADDRSTRLEN);
		const struct sockaddr_in* addr4 = sockaddr_in_cast(addr);
		//inet_ntoa是将网络字节序的地址转成点分十进制的形式
		//inet_ntop比inet_ntoa好一些
		::inet_ntop(AF_INET, &addr4->sin_addr, buf, static_cast<socklen_t>(size));
	}
	else if (addr->sa_family == AF_INET6)
	{
		assert(size >= INET6_ADDRSTRLEN);
		const struct sockaddr_in6* addr6 = sockaddr_in6_cast(addr);
		::inet_ntop(AF_INET6, &addr6->sin6_addr, buf, static_cast<socklen_t>(size));
	}
}

//从IP和Port构造出一个sockaddr_in即构造一个网际协议地址
void sockets::fromIpPort(const char* ip, uint16_t port,
	struct sockaddr_in* addr)
{
	//只支持IPv4
	addr->sin_family = AF_INET;
	//将端口转成网络字节序
	addr->sin_port = hostToNetwork16(port);
	//将点分十进制IP转化成网络字节序的IP
	if (::inet_pton(AF_INET, ip, &addr->sin_addr) <= 0)
	{
		LOG_SYSERR << "sockets::fromIpPort";
	}
}

void sockets::fromIpPort(const char* ip, uint16_t port,
	struct sockaddr_in6* addr)
{
	addr->sin6_family = AF_INET6;
	addr->sin6_port = hostToNetwork16(port);
	if (::inet_pton(AF_INET6, ip, &addr->sin6_addr) <= 0)
	{
		LOG_SYSERR << "sockets::fromIpPort";
	}
}

//获取套接字错误
int sockets::getSocketError(int sockfd)
{
	int optval;
	socklen_t optlen = static_cast<socklen_t>(sizeof optval);

	if (::getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0)
	{
		return errno;
	}
	else
	{
		return optval;
	}
}

//过去套接字的地址
//对于一个已连接的套接字，它有一个本地IP地址和对等方IP地址

//获取socket本地地址
struct sockaddr_in6 sockets::getLocalAddr(int sockfd)
{
	struct sockaddr_in6 localaddr;
	memZero(&localaddr, sizeof localaddr);
	socklen_t addrlen = static_cast<socklen_t>(sizeof localaddr);
	if (::getsockname(sockfd, sockaddr_cast(&localaddr), &addrlen) < 0)
	{
		LOG_SYSERR << "sockets::getLocalAddr";
	}
	return localaddr;
}
//获取对等方地址
struct sockaddr_in6 sockets::getPeerAddr(int sockfd)
{
	struct sockaddr_in6 peeraddr;
	memZero(&peeraddr, sizeof peeraddr);
	socklen_t addrlen = static_cast<socklen_t>(sizeof peeraddr);
	if (::getpeername(sockfd, sockaddr_cast(&peeraddr), &addrlen) < 0)
	{
		LOG_SYSERR << "sockets::getPeerAddr";
	}
	return peeraddr;
}

bool sockets::isSelfConnect(int sockfd)
{
	struct sockaddr_in6 localaddr = getLocalAddr(sockfd);
	struct sockaddr_in6 peeraddr = getPeerAddr(sockfd);
	if (localaddr.sin6_family == AF_INET)
	{
		const struct sockaddr_in* laddr4 = reinterpret_cast<struct sockaddr_in*>(&localaddr);
		const struct sockaddr_in* raddr4 = reinterpret_cast<struct sockaddr_in*>(&peeraddr);
		return laddr4->sin_port == raddr4->sin_port
			&& laddr4->sin_addr.s_addr == raddr4->sin_addr.s_addr;
	}
	else if (localaddr.sin6_family == AF_INET6)
	{
		return localaddr.sin6_port == peeraddr.sin6_port
			&& memcmp(&localaddr.sin6_addr, &peeraddr.sin6_addr, sizeof localaddr.sin6_addr) == 0;
	}
	else
	{
		return false;
	}
}

