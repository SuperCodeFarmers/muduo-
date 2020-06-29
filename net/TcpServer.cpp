// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/net/TcpServer.h"

#include "muduo/base/Logging.h"
#include "muduo/net/Acceptor.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/EventLoopThreadPool.h"
#include "muduo/net/SocketsOps.h"

#include <stdio.h>  // snprintf

using namespace muduo;
using namespace muduo::net;

TcpServer::TcpServer(EventLoop* loop,
	const InetAddress& listenAddr,
	const string& nameArg,
	Option option)
	: loop_(CHECK_NOTNULL(loop)),//检查loop不是空指针
	ipPort_(listenAddr.toIpPort()),//端口号
	name_(nameArg),//名称
	acceptor_(new Acceptor(loop, listenAddr, option == kReusePort)),// Acceptor对象
	threadPool_(new EventLoopThreadPool(loop, name_)),	// 构造一个EventLoopThreadPool对象，这个loop就是MainReactor
	connectionCallback_(defaultConnectionCallback),
	messageCallback_(defaultMessageCallback),
	nextConnId_(1)
{
	//_1对应的是socket文件描述符，_2对应的是对等方的地址(InetAddrss)
	acceptor_->setNewConnectionCallback(
		std::bind(&TcpServer::newConnection, this, _1, _2));
}

TcpServer::~TcpServer()
{
	loop_->assertInLoopThread();
	LOG_TRACE << "TcpServer::~TcpServer [" << name_ << "] destructing";

	for (auto& item : connections_)
	{
		TcpConnectionPtr conn(item.second);
		item.second.reset();
		conn->getLoop()->runInLoop(
			std::bind(&TcpConnection::connectDestroyed, conn));
	}
}

// 设置线程数，实际上是调用了线程池对象里面的setThreadNum
void TcpServer::setThreadNum(int numThreads)
{
	assert(0 <= numThreads);
	threadPool_->setThreadNum(numThreads);
}

//该函数多次调用是无害的
//该函数可以跨线程调用
void TcpServer::start()
{
	if (started_.getAndSet(1) == 0)
	{
		// 启动线程，可以传递一个线程初始化的函数，这个初始化函数通过setThreadInitCallback()来设置
		threadPool_->start(threadInitCallback_);

		// 断言判断是否处于侦听状态,如果不处于侦听状态，则调用执行监听listen
		assert(!acceptor_->listenning());
		// 让loop_调用listen函数
		loop_->runInLoop(//get_pointer返回acceptor_的原生指针，到时候可以通过该原生指针调用listen函数
			std::bind(&Acceptor::listen, get_pointer(acceptor_)));
	}
}

//一个新的连接
void TcpServer::newConnection(int sockfd, const InetAddress& peerAddr)
{
	loop_->assertInLoopThread();
	//采用轮询的方法把新的连接加入到线程池中，这样使得每个线程所维护的socket都是均匀的
	EventLoop* ioLoop = threadPool_->getNextLoop();
	char buf[64];
	snprintf(buf, sizeof buf, "-%s#%d", ipPort_.c_str(), nextConnId_);
	++nextConnId_;
	string connName = name_ + buf;

	LOG_INFO << "TcpServer::newConnection [" << name_
		<< "] - new connection [" << connName
		<< "] from " << peerAddr.toIpPort();
	//构造一个本地地址
	InetAddress localAddr(sockets::getLocalAddr(sockfd));
	// FIXME poll with zero timeout to double confirm the new connection
	// FIXME use make_shared if necessary
	//创建一个连接对象
	TcpConnectionPtr conn(new TcpConnection(ioLoop,
		connName,
		sockfd,
		localAddr,
		peerAddr));
	//把conn放入列表中
	connections_[connName] = conn;
	conn->setConnectionCallback(connectionCallback_);
	conn->setMessageCallback(messageCallback_);
	conn->setWriteCompleteCallback(writeCompleteCallback_);

	conn->setCloseCallback(
		std::bind(&TcpServer::removeConnection, this, _1)); // FIXME: unsafe

	//我们不能直接使用conn对象调用用connectEstablished()进行连接建立，我们要在它所属的IO线程中调用
	//然后调用conn它的TcpConnection::connectEstablished
	ioLoop->runInLoop(std::bind(&TcpConnection::connectEstablished, conn));
}

void TcpServer::removeConnection(const TcpConnectionPtr& conn)
{
	// FIXME: unsafe
	loop_->runInLoop(std::bind(&TcpServer::removeConnectionInLoop, this, conn));
}

void TcpServer::removeConnectionInLoop(const TcpConnectionPtr& conn)
{
	loop_->assertInLoopThread();
	LOG_INFO << "TcpServer::removeConnectionInLoop [" << name_
		<< "] - connection " << conn->name();

	//将channel对象重列表中移除
	size_t n = connections_.erase(conn->name());
	(void)n;
	assert(n == 1);
	EventLoop* ioLoop = conn->getLoop();
	ioLoop->queueInLoop(
		//将conn与TcpConnection::connectDestroyed相绑定产生一个Function对象，这时conn的引用会+1
		std::bind(&TcpConnection::connectDestroyed, conn));
}

