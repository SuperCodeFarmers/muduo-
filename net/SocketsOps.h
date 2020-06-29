#ifndef MUDUO_NET_SOCKETSOPS_H
#define MUDUO_NET_SOCKETSOPS_H

#include <arpa/inet.h>

namespace muduo
{
	namespace net
	{
		namespace sockets
		{
			/*
			对socket相关的系统调用进行了封装
			*/

			///
			/// Creates a non-blocking socket file descriptor,
			/// abort if any error.
			//创建一个非阻塞的套接字，如果创建失败就终止程序
			int createNonblockingOrDie(sa_family_t family);

			//连接
			int  connect(int sockfd, const struct sockaddr* addr);
			//绑定，失败就终止程序
			void bindOrDie(int sockfd, const struct sockaddr* addr);
			//监听，监听失败就终止程序
			void listenOrDie(int sockfd);
			int  accept(int sockfd, struct sockaddr_in6* addr);
			ssize_t read(int sockfd, void* buf, size_t count);
			ssize_t readv(int sockfd, const struct iovec* iov, int iovcnt);
			ssize_t write(int sockfd, const void* buf, size_t count);
			void close(int sockfd);
			void shutdownWrite(int sockfd);

			void toIpPort(char* buf, size_t size,
				const struct sockaddr* addr);
			void toIp(char* buf, size_t size,
				const struct sockaddr* addr);

			void fromIpPort(const char* ip, uint16_t port,
				struct sockaddr_in* addr);
			void fromIpPort(const char* ip, uint16_t port,
				struct sockaddr_in6* addr);

			int getSocketError(int sockfd);

			const struct sockaddr* sockaddr_cast(const struct sockaddr_in* addr);
			const struct sockaddr* sockaddr_cast(const struct sockaddr_in6* addr);
			struct sockaddr* sockaddr_cast(struct sockaddr_in6* addr);
			const struct sockaddr_in* sockaddr_in_cast(const struct sockaddr* addr);
			const struct sockaddr_in6* sockaddr_in6_cast(const struct sockaddr* addr);

			struct sockaddr_in6 getLocalAddr(int sockfd);
			struct sockaddr_in6 getPeerAddr(int sockfd);
			bool isSelfConnect(int sockfd);

		}  // namespace sockets
	}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_SOCKETSOPS_H
