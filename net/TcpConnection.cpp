// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/net/TcpConnection.h"

#include "muduo/base/Logging.h"
#include "muduo/base/WeakCallback.h"
#include "muduo/net/Channel.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/Socket.h"
#include "muduo/net/SocketsOps.h"
#include "TcpConnection.h"

#include <errno.h>

using namespace muduo;
using namespace muduo::net;

void muduo::net::defaultConnectionCallback(const TcpConnectionPtr& conn)
{
	LOG_TRACE << conn->localAddress().toIpPort() << " -> "
		<< conn->peerAddress().toIpPort() << " is "
		<< (conn->connected() ? "UP" : "DOWN");
	// do not call conn->forceClose(), because some users want to register message callback only.
}

void muduo::net::defaultMessageCallback(const TcpConnectionPtr&,
	Buffer* buf,
	Timestamp)
{
	buf->retrieveAll();
}

TcpConnection::TcpConnection(EventLoop* loop,
	const string& nameArg,
	int sockfd,
	const InetAddress& localAddr,
	const InetAddress& peerAddr)
	: loop_(CHECK_NOTNULL(loop)),
	name_(nameArg),
	state_(kConnecting),
	reading_(true),//是否
	socket_(new Socket(sockfd)),//创建一个套接字
	channel_(new Channel(loop, sockfd)),//构造一个通道
	localAddr_(localAddr),//本地地址
	peerAddr_(peerAddr),//对等方地址
	highWaterMark_(64 * 1024 * 1024)
{
	//通道可读事件到来时，回调TcpConnection::handleRead，_1是事件发生时间
	channel_->setReadCallback(
		std::bind(&TcpConnection::handleRead, this, _1));

	// 通道可写事件到来时，回调TcpConnection::handleWrite，把数据发送出去
	channel_->setWriteCallback(
		std::bind(&TcpConnection::handleWrite, this));

	//连接关闭，回调TcpConnection::handleClose
	channel_->setCloseCallback(
		std::bind(&TcpConnection::handleClose, this));

	//发生错误，回调TcpConnection::handleError
	channel_->setErrorCallback(
		std::bind(&TcpConnection::handleError, this));
	LOG_DEBUG << "TcpConnection::ctor[" << name_ << "] at " << this
		<< " fd=" << sockfd;

	socket_->setKeepAlive(true);
}

TcpConnection::~TcpConnection()
{
	LOG_DEBUG << "TcpConnection::dtor[" << name_ << "] at " << this
		<< " fd=" << channel_->fd()
		<< " state=" << stateToString();
	assert(state_ == kDisconnected);
}

bool TcpConnection::getTcpInfo(struct tcp_info* tcpi) const
{
	return socket_->getTcpInfo(tcpi);
}

string TcpConnection::getTcpInfoString() const
{
	char buf[1024];
	buf[0] = '\0';
	socket_->getTcpInfoString(buf, sizeof buf);
	return buf;
}

void TcpConnection::send(const void* data, int len)
{
	send(StringPiece(static_cast<const char*>(data), len));
}

// 线程安全,可以跨线程调用
void TcpConnection::send(const StringPiece& message)
{
	if (state_ == kConnected)
	{
		//判断是否在IO线程当中
		if (loop_->isInLoopThread())
		{
			//在IO线程当中调用sendInLoop(message);
			sendInLoop(message);
		}
		else
		{
			//不在IO线程当中调用runInLoop(message);，把事件放入IO线程的队列中
			//然后再IO线程中再调用SendInLoop(message);
			void (TcpConnection:: * fp)(const StringPiece & message) = &TcpConnection::sendInLoop;
			loop_->runInLoop(
				std::bind(fp,
					this,     // FIXME
					message.as_string()));
			//std::forward<string>(message)));
		}
	}
}

// FIXME efficiency!!!
void TcpConnection::send(Buffer* buf)
{
	if (state_ == kConnected)
	{
		if (loop_->isInLoopThread())
		{
			sendInLoop(buf->peek(), buf->readableBytes());
			buf->retrieveAll();
		}
		else
		{
			void (TcpConnection:: * fp)(const StringPiece & message) = &TcpConnection::sendInLoop;
			loop_->runInLoop(
				std::bind(fp,
					this,     // FIXME
					buf->retrieveAllAsString()));
			//std::forward<string>(message)));
		}
	}
}

void TcpConnection::sendInLoop(const StringPiece& message)
{
	sendInLoop(message.data(), message.size());
}

void TcpConnection::sendInLoop(const void* data, size_t len)
{
	loop_->assertInLoopThread();
	ssize_t nwrote = 0;
	size_t remaining = len;
	bool faultError = false;
	if (state_ == kDisconnected)
	{
		LOG_WARN << "disconnected, give up writing";
		return;
	}
	// if no thing in output queue, try writing directly
	// 通道中没有关注可写事件并且发送缓冲区没有数据，可以直接write
	if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0)
	{
		nwrote = sockets::write(channel_->fd(), data, len);
		if (nwrote >= 0)
		{
			remaining = len - nwrote;
			// remaining == 0说明要发送的数据都拷贝到了内核缓冲区，说明写完了
			// 写完了，就回调writeCompleteCallback_
			// 如果remaining > 0,会在后面把为发送的数据添加到output buffer当中
			if (remaining == 0 && writeCompleteCallback_)
			{
				loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));
			}
		}
		else // nwrote < 0，出错
		{
			nwrote = 0;
			if (errno != EWOULDBLOCK)
			{
				LOG_SYSERR << "TcpConnection::sendInLoop";
				if (errno == EPIPE || errno == ECONNRESET) // FIXME: any others?
				{
					faultError = true;
				}
			}
		}
	}

	assert(remaining <= len);
	// 断言没有错误，并且还有数据没有写完。说明内核当中的发送缓冲区满了，要将未写完的数据添加到output buffer中
	if (!faultError && remaining > 0)
	{
		size_t oldLen = outputBuffer_.readableBytes();	// 当前output buffer中的数据

		//  如果操作highWaterMark_(高水位标),回调highWaterMarkCallback_
		if (oldLen + remaining >= highWaterMark_
			&& oldLen < highWaterMark_
			&& highWaterMarkCallback_)
		{
			// 在highWaterMarkCallback_中我们可以把该连接断开，这要看该回调函数怎么实现
			loop_->queueInLoop(std::bind(highWaterMarkCallback_, shared_from_this(), oldLen + remaining));
		}

		// 把未发送的数据添加到output buffer中
		outputBuffer_.append(static_cast<const char*>(data) + nwrote, remaining);

		// output buffer中有数据了，我们就要关注POLLOUT事件，如果没有关注我们就要立即关注
		if (!channel_->isWriting())
		{
			channel_->enableWriting();	// 关注POLLOUT事件
		}
	}
}

void TcpConnection::shutdown()
{
	// FIXME: use compare and swap
	// 判断当前状态是否还处于连接的状态
	if (state_ == kConnected)
	{
		// 把状态改为正在关闭连接
		setState(kDisconnecting);
		// FIXME: shared_from_this()?
		// 在当前IO线程当中调用shutdownInLoop，可以把this指针改为shared_from_this
		loop_->runInLoop(std::bind(&TcpConnection::shutdownInLoop, this));
	}
}

void TcpConnection::shutdownInLoop()
{
	loop_->assertInLoopThread();
	if (!channel_->isWriting())
	{
		// Tcp套接字是全双工的
		// 只有处于不关注POLLOUT事件(可写事件)，我们才可以关闭该连接，
		// 即我们要关闭写这一半操作，必须要把发送的数据发送完后，取消关注POLLOUT事件
		// we are not writing
		socket_->shutdownWrite();
	}
}

// void TcpConnection::shutdownAndForceCloseAfter(double seconds)
// {
//   // FIXME: use compare and swap
//   if (state_ == kConnected)
//   {
//     setState(kDisconnecting);
//     loop_->runInLoop(std::bind(&TcpConnection::shutdownAndForceCloseInLoop, this, seconds));
//   }
// }

// void TcpConnection::shutdownAndForceCloseInLoop(double seconds)
// {
//   loop_->assertInLoopThread();
//   if (!channel_->isWriting())
//   {
//     // we are not writing
//     socket_->shutdownWrite();
//   }
//   loop_->runAfter(
//       seconds,
//       makeWeakCallback(shared_from_this(),
//                        &TcpConnection::forceCloseInLoop));
// }

void TcpConnection::forceClose()
{
	// FIXME: use compare and swap
	if (state_ == kConnected || state_ == kDisconnecting)
	{
		setState(kDisconnecting);
		loop_->queueInLoop(std::bind(&TcpConnection::forceCloseInLoop, shared_from_this()));
	}
}

void TcpConnection::forceCloseWithDelay(double seconds)
{
	if (state_ == kConnected || state_ == kDisconnecting)
	{
		setState(kDisconnecting);
		loop_->runAfter(
			seconds,
			makeWeakCallback(shared_from_this(),
				&TcpConnection::forceClose));  // not forceCloseInLoop to avoid race condition
	}
}

void TcpConnection::forceCloseInLoop()
{
	loop_->assertInLoopThread();
	if (state_ == kConnected || state_ == kDisconnecting)
	{
		// as if we received 0 byte in handleRead();
		handleClose();
	}
}

const char* TcpConnection::stateToString() const
{
	switch (state_)
	{
	case kDisconnected:
		return "kDisconnected";
	case kConnecting:
		return "kConnecting";
	case kConnected:
		return "kConnected";
	case kDisconnecting:
		return "kDisconnecting";
	default:
		return "unknown state";
	}
}

void TcpConnection::setTcpNoDelay(bool on)
{
	socket_->setTcpNoDelay(on);
}

void TcpConnection::startRead()
{
	loop_->runInLoop(std::bind(&TcpConnection::startReadInLoop, this));
}

void TcpConnection::startReadInLoop()
{
	loop_->assertInLoopThread();
	if (!reading_ || !channel_->isReading())
	{
		channel_->enableReading();
		reading_ = true;
	}
}

void TcpConnection::stopRead()
{
	loop_->runInLoop(std::bind(&TcpConnection::stopReadInLoop, this));
}

void TcpConnection::stopReadInLoop()
{
	loop_->assertInLoopThread();
	if (reading_ || channel_->isReading())
	{
		channel_->disableReading();
		reading_ = false;
	}
}

// 连接建立
void TcpConnection::connectEstablished()
{
	loop_->assertInLoopThread();
	assert(state_ == kConnecting);//判断是否处于正在连接状态
	setState(kConnected);//设置成已连接状态

	//获取当前TcpConnection对象的shared_ptr
	channel_->tie(shared_from_this());

	//关注这个通道的可读事件
	//TcpConnection所对应的通道加入到Poller中关注
	channel_->enableReading();

	//回调connectionCallback，该回调函数是用户的回调函数
	connectionCallback_(shared_from_this());
}

void TcpConnection::connectDestroyed()
{
	loop_->assertInLoopThread();
	if (state_ == kConnected)
	{
		setState(kDisconnected);
		channel_->disableAll();

		//回调用户的回调函数
		connectionCallback_(shared_from_this());
	}
	channel_->remove();
}

void TcpConnection::handleRead(Timestamp receiveTime)
{
	loop_->assertInLoopThread();
	int savedErrno = 0;
	//读取通道，把数据读到缓冲区inputBuffer_中
	ssize_t n = inputBuffer_.readFd(channel_->fd(), &savedErrno);
	if (n > 0)
	{
		//读取成功后，回调messageCallback_，把当前对象传给
		messageCallback_(shared_from_this(), &inputBuffer_, receiveTime);
	}
	else if (n == 0)
	{
		//read返回0，说明是客户端断开连接,下面是处理连接断开
		handleClose();
	}
	else
	{
		//处理错误
		errno = savedErrno;
		LOG_SYSERR << "TcpConnection::handleRead";
		handleError();
	}
}

// 内核缓冲区有空间了，会回调该函数，即POLLOUT事件触发了
void TcpConnection::handleWrite()
{
	loop_->assertInLoopThread();
	if (channel_->isWriting())	// 如果通道出去关注POLLOUT事件，我们就把output buffer中的数据写入
	{
		ssize_t n = sockets::write(channel_->fd(),
			outputBuffer_.peek(),
			outputBuffer_.readableBytes());
		// 一次写入不一定把数据全部写入
		if (n > 0)
		{
			// n > 0，我们要把output buffer中的数据移除已经发送的字节
			outputBuffer_.retrieve(n);
			if (outputBuffer_.readableBytes() == 0)	// 应用层发送缓冲区已全部清空，发送完毕
			{
				
				channel_->disableWriting();	// 发送完毕，我们应该停止关注POLLOUT事件，以免出现busy loop
				if (writeCompleteCallback_)	// 回调writeCompleteCallback_，没有数据了要回调。
				{
					loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));
				}

				//数据发送完毕，并且连接状态为kDisconnecting，即上层应用发送数据后要关闭连接
				if (state_ == kDisconnecting)
				{
					shutdownInLoop();	// 关闭连接
				}
			}
		}
		else
		{
			LOG_SYSERR << "TcpConnection::handleWrite";
			// if (state_ == kDisconnecting)
			// {
			//   shutdownInLoop();
			// }
		}
	}
	else
	{
		LOG_TRACE << "Connection fd = " << channel_->fd()
			<< " is down, no more writing";
	}
}

void TcpConnection::handleClose()
{
	loop_->assertInLoopThread();
	LOG_TRACE << "fd = " << channel_->fd() << " state = " << stateToString();
	assert(state_ == kConnected || state_ == kDisconnecting);
	// we don't close fd, leave it to dtor, so we can find leaks easily.

	//设置状态
	setState(kDisconnected);
	channel_->disableAll();

	//获取这个对象的shared_ptr指针
	TcpConnectionPtr guardThis(shared_from_this());

	//回调了用户的回调函数
	connectionCallback_(guardThis);
	// must be the last line
	//closeCallback_会调用TcpServer的removeConnection()
	closeCallback_(guardThis);//调用TcpServer::removeConnection
}

void TcpConnection::handleError()
{
	int err = sockets::getSocketError(channel_->fd());
	LOG_ERROR << "TcpConnection::handleError [" << name_
		<< "] - SO_ERROR = " << err << " " << strerror_tl(err);
}

