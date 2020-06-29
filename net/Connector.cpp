// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//

#include "muduo/net/Connector.h"

#include "muduo/base/Logging.h"
#include "muduo/net/Channel.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/SocketsOps.h"

#include <errno.h>

using namespace muduo;
using namespace muduo::net;

const int Connector::kMaxRetryDelayMs;

Connector::Connector(EventLoop* loop, const InetAddress& serverAddr)
	: loop_(loop),
	serverAddr_(serverAddr),
	connect_(false),
	state_(kDisconnected),
	retryDelayMs_(kInitRetryDelayMs)
{
	LOG_DEBUG << "ctor[" << this << "]";
}

Connector::~Connector()
{
	LOG_DEBUG << "dtor[" << this << "]";
	assert(!channel_);
}

// 可以跨线程调用
void Connector::start()
{
	connect_ = true;
	loop_->runInLoop(std::bind(&Connector::startInLoop, this)); // FIXME: unsafe
}

void Connector::startInLoop()
{
	// 断言处于IO线程中
	loop_->assertInLoopThread();
	assert(state_ == kDisconnected);
	if (connect_)
	{
		// 发起连接
		connect();
	}
	else
	{
		LOG_DEBUG << "do not connect";
	}
}

//关闭函数，可以跨线程调用
void Connector::stop()
{
	// 关闭connector
	connect_ = false;

	// 在IO线程中调用stopInLoop
	loop_->queueInLoop(std::bind(&Connector::stopInLoop, this)); // FIXME: unsafe
	// FIXME: cancel timer
}

void Connector::stopInLoop()
{
	loop_->assertInLoopThread();

	// 如果状态处于正在连接
	if (state_ == kConnecting)
	{
		setState(kDisconnected);

		// 将通道从poller中移除关注，并将channel置空
		int sockfd = removeAndResetChannel();
		retry(sockfd);	// 这里并不是非要重连，只是调用sockets::close(sockfd)
	}
}

void Connector::connect()
{
	// 创建一个非阻塞套接字
	int sockfd = sockets::createNonblockingOrDie(serverAddr_.family());
	int ret = sockets::connect(sockfd, serverAddr_.getSockAddr());
	int savedErrno = (ret == 0) ? 0 : errno;
	switch (savedErrno)
	{
	case 0:
	case EINPROGRESS:	// 非阻塞套接字，未连接成功返回码是EINPROGRESS表示正在连接
	case EINTR:
	case EISCONN:		// 连接成功
		connecting(sockfd);
		break;

	case EAGAIN:
	case EADDRINUSE:
	case EADDRNOTAVAIL:
	case ECONNREFUSED:
	case ENETUNREACH:
		retry(sockfd);	// 重连
		break;

	case EACCES:
	case EPERM:
	case EAFNOSUPPORT:
	case EALREADY:
	case EBADF:
	case EFAULT:
	case ENOTSOCK:
		LOG_SYSERR << "connect error in Connector::startInLoop " << savedErrno;
		sockets::close(sockfd);	// 不能重连，关闭sockfd
		break;

	default:
		LOG_SYSERR << "Unexpected error in Connector::startInLoop " << savedErrno;
		sockets::close(sockfd);
		// connectErrorCallback_();
		break;
	}
}

// 重启，不能跨线程调用
void Connector::restart()
{
	loop_->assertInLoopThread();
	setState(kDisconnected);

	// 初始化重连时间
	retryDelayMs_ = kInitRetryDelayMs;
	connect_ = true;
	startInLoop();
}

void Connector::connecting(int sockfd)
{
	setState(kConnecting);
	assert(!channel_);
	// 创建一个channel对象与sockfd关联起来
	// 我们在Connector对象构造函数时,是没有直接创建channel对象，只有sockfd创建成功了才创建channel对象
	channel_.reset(new Channel(loop_, sockfd));
	
	// 设置可写回调函数，这时候如果socket没有错误，sockfd就处于可写状态
	// 一但连接成功他就会调用handleWrite
	channel_->setWriteCallback(
		std::bind(&Connector::handleWrite, this)); // FIXME: unsafe

	// 设置错误回调函数
	channel_->setErrorCallback(
		std::bind(&Connector::handleError, this)); // FIXME: unsafe

	// channel_->tie(shared_from_this()); is not working,
	// as channel_ is not managed by shared_ptr
	channel_->enableWriting();	// 让Poller关注可写事件，连接成功之后会调用handleWrite
}

int Connector::removeAndResetChannel()
{
	channel_->disableAll();
	channel_->remove();		// 从poller中移除关注
	int sockfd = channel_->fd();
	// Can't reset channel_ here, because we are inside Channel::handleEvent
	// 不能在这里重置channel_，因为我们正在调用Channel::handleEvent
	// 所以我们把resetChannel加入队列中，然后我们就跳出这个函数后就能执行resetChannel
	loop_->queueInLoop(std::bind(&Connector::resetChannel, this)); // FIXME: unsafe
	return sockfd;
}

void Connector::resetChannel()
{
	// 将channel置空
	channel_.reset();
}

void Connector::handleWrite()
{
	LOG_TRACE << "Connector::handleWrite " << state_;

	if (state_ == kConnecting)
	{
		// 如果连接成功了,这个channel就没有价值了
		// 为什么把可写状态取消关注，因为已连接套接字对于电平触发是一直处于可写状态，所以要取消关注
		int sockfd = removeAndResetChannel();	// 从Poller中移除关注，并将channel置空

		// socket可写并不意味着连接一定建立成功
		// 还需要用getsocktopt(sockfd, SOL_SOL_SOCKET, SO_ERROR, ...)再次确认一下
		int err = sockets::getSocketError(sockfd);
		if (err)	// 有错误
		{
			LOG_WARN << "Connector::handleWrite - SO_ERROR = "
				<< err << " " << strerror_tl(err);
			retry(sockfd);	// 重连
		}
		else if (sockets::isSelfConnect(sockfd))	// 自连接
		{
			LOG_WARN << "Connector::handleWrite - Self connect";
			retry(sockfd);	// 重连
		}
		else	// 连接成功
		{
			// 设置状态
			setState(kConnected);
			if (connect_)
			{
				newConnectionCallback_(sockfd);	// 回调
			}
			else
			{
				sockets::close(sockfd);
			}
		}
	}
	else
	{
		// what happened?
		assert(state_ == kDisconnected);
	}
}

// 产生错误的回调函数
void Connector::handleError()
{
	LOG_ERROR << "Connector::handleError state=" << state_;
	if (state_ == kConnecting)
	{
		// 从poller中移除关注，并将channel置空
		int sockfd = removeAndResetChannel();
		int err = sockets::getSocketError(sockfd);
		LOG_TRACE << "SO_ERROR = " << err << " " << strerror_tl(err);
		retry(sockfd);	// 重新发起连接
	}
}

// 重连，采用back-off策略重连，即重连事件逐渐延长，0.5s, 1s, 2s, ...直至30s
void Connector::retry(int sockfd)
{
	// 重连之前先关闭套接字
	sockets::close(sockfd);
	// 设置状态
	setState(kDisconnected);
	if (connect_)
	{
		LOG_INFO << "Connector::retry - Retry connecting to " << serverAddr_.toIpPort()
			<< " in " << retryDelayMs_ << " milliseconds. ";
		// 注册一个定时操作，返回一个定时器
		loop_->runAfter(retryDelayMs_ / 1000.0,
			std::bind(&Connector::startInLoop, shared_from_this()));

		// 下次重连时间
		retryDelayMs_ = std::min(retryDelayMs_ * 2, kMaxRetryDelayMs);
	}
	else
	{
		LOG_DEBUG << "do not connect";
	}
}

