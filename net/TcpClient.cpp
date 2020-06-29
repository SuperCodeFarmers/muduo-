// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//

#include "muduo/net/TcpClient.h"

#include "muduo/base/Logging.h"
#include "muduo/net/Connector.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/SocketsOps.h"

#include <stdio.h>  // snprintf

using namespace muduo;
using namespace muduo::net;

// TcpClient::TcpClient(EventLoop* loop)
//   : loop_(loop)
// {
// }

// TcpClient::TcpClient(EventLoop* loop, const string& host, uint16_t port)
//   : loop_(CHECK_NOTNULL(loop)),
//     serverAddr_(host, port)
// {
// }

namespace muduo
{
	namespace net
	{
		namespace detail
		{

			void removeConnection(EventLoop* loop, const TcpConnectionPtr& conn)
			{
				loop->queueInLoop(std::bind(&TcpConnection::connectDestroyed, conn));
			}

			void removeConnector(const ConnectorPtr& connector)
			{
				//connector->
			}

		}  // namespace detail
	}  // namespace net
}  // namespace muduo

TcpClient::TcpClient(EventLoop* loop,
	const InetAddress& serverAddr,
	const string& nameArg)
	: loop_(CHECK_NOTNULL(loop)),
	connector_(new Connector(loop, serverAddr)),
	name_(nameArg),
	connectionCallback_(defaultConnectionCallback),
	messageCallback_(defaultMessageCallback),
	retry_(false),
	connect_(true),
	nextConnId_(1)
{
	// 连接成功回调函数，一旦连接建立成功，就会回调newConnection
	connector_->setNewConnectionCallback(
		std::bind(&TcpClient::newConnection, this, _1));
	// FIXME setConnectFailedCallback
	LOG_INFO << "TcpClient::TcpClient[" << name_
		<< "] - connector " << get_pointer(connector_);
}

TcpClient::~TcpClient()
{
	LOG_INFO << "TcpClient::~TcpClient[" << name_
		<< "] - connector " << get_pointer(connector_);
	TcpConnectionPtr conn;
	bool unique = false;
	{
		MutexLockGuard lock(mutex_);
		unique = connection_.unique();
		conn = connection_;
	}
	if (conn)
	{
		assert(loop_ == conn->getLoop());
		// FIXME: not 100% safe, if we are in different thread
		// 重新设置TcpConnection中的closeCallback_为detail::removeConnection
		// 而不是TcpClient的removeConnection，因为TcpClinet有重连功能
		CloseCallback cb = std::bind(&detail::removeConnection, loop_, _1);
		loop_->runInLoop(
			std::bind(&TcpConnection::setCloseCallback, conn, cb));
		if (unique)
		{
			conn->forceClose();
		}
	}
	else
	{
		// 这种情况，说明connector处于未连接状态，将connector_停止
		connector_->stop();
		// FIXME: HACK
		loop_->runAfter(1, std::bind(&detail::removeConnector, connector_));
	}
}

void TcpClient::connect()
{
	// FIXME: check state
	LOG_INFO << "TcpClient::connect[" << name_ << "] - connecting to "
		<< connector_->serverAddress().toIpPort();
	connect_ = true;
	connector_->start();
}

// 用于连接已建立的情况下，关闭连接
void TcpClient::disconnect()
{
	connect_ = false;

	{
		MutexLockGuard lock(mutex_);
		if (connection_)
		{
			connection_->shutdown();
		}
	}
}

// 停止connector，可能连接尚未建立成功我们停止建立连接
void TcpClient::stop()
{
	connect_ = false;
	connector_->stop();
}

void TcpClient::newConnection(int sockfd)
{
	loop_->assertInLoopThread();
	InetAddress peerAddr(sockets::getPeerAddr(sockfd));
	char buf[32];
	snprintf(buf, sizeof buf, ":%s#%d", peerAddr.toIpPort().c_str(), nextConnId_);
	++nextConnId_;
	string connName = name_ + buf;

	InetAddress localAddr(sockets::getLocalAddr(sockfd));
	// FIXME poll with zero timeout to double confirm the new connection
	// FIXME use make_shared if necessary
	// 因为在Connector中如果连接建立成功，我们就会把该sockfd的可写事件从poller中移除，并且置空channel
	// 我们创建一个已连接的对象，关注这个已连接的对象的可写事件
	TcpConnectionPtr conn(new TcpConnection(loop_,
		connName,
		sockfd,
		localAddr,
		peerAddr));

	conn->setConnectionCallback(connectionCallback_);
	conn->setMessageCallback(messageCallback_);
	conn->setWriteCompleteCallback(writeCompleteCallback_);
	conn->setCloseCallback(
		std::bind(&TcpClient::removeConnection, this, _1)); // FIXME: unsafe
	{
		MutexLockGuard lock(mutex_);
		connection_ = conn;
	}
	conn->connectEstablished();
}

// 连接断开回调函数
void TcpClient::removeConnection(const TcpConnectionPtr& conn)
{
	loop_->assertInLoopThread();
	assert(loop_ == conn->getLoop());

	{
		MutexLockGuard lock(mutex_);
		assert(connection_ == conn);
		// 把我们保存的TcpConnection进行重置
		connection_.reset();
	}

	// 在IO线程中调用connectDestroyed
	loop_->queueInLoop(std::bind(&TcpConnection::connectDestroyed, conn));
	if (retry_ && connect_)
	{
		LOG_INFO << "TcpClient::connect[" << name_ << "] - Reconnecting to "
			<< connector_->serverAddress().toIpPort();
		// 这里的重连是指连接建立成功之后被断开的重连
		connector_->restart();
	}
}

