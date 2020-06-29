// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/net/Acceptor.h"

#include "muduo/base/Logging.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/InetAddress.h"
#include "muduo/net/SocketsOps.h"

#include <errno.h>
#include <fcntl.h>
//#include <sys/types.h>
//#include <sys/stat.h>
#include <unistd.h>

using namespace muduo;
using namespace muduo::net;

Acceptor::Acceptor(EventLoop* loop, const InetAddress& listenAddr, bool reuseport)
	: loop_(loop),
	acceptSocket_(sockets::createNonblockingOrDie(listenAddr.family())),//创建了套接字
	acceptChannel_(loop, acceptSocket_.fd()),//关注套接字的事件
	listenning_(false),//在创建accept的时候是不监听的，只有当调用listen的时候才开始监听
	idleFd_(::open("/dev/null", O_RDONLY | O_CLOEXEC))//预先准备一个文件描述符，空闲的文件描述符
{
	assert(idleFd_ >= 0);
	//设置socket的地址和端口可以重复利用
	acceptSocket_.setReuseAddr(true);
	acceptSocket_.setReusePort(reuseport);
	acceptSocket_.bindAddress(listenAddr);
	acceptChannel_.setReadCallback(
		//这个管道设置一个读的回调函数，设置为Acceptor::handleRead
		//当acceptSocket有事件的时候，hanndle会调用handleEvent，进而调用到Acceptor的handleRead
		std::bind(&Acceptor::handleRead, this));
}

Acceptor::~Acceptor()
{
	//处理掉所有的事件，才能结束
	acceptChannel_.disableAll();
	acceptChannel_.remove();//处理掉该通道
	::close(idleFd_);//关闭文件描述符
}

void Acceptor::listen()
{
	loop_->assertInLoopThread();
	listenning_ = true;//设置监听标志位=true
	acceptSocket_.listen();//开启监听
	acceptChannel_.enableReading();//关注它的可读事件
}

//处理Accepte可读事件
void Acceptor::handleRead()
{
	loop_->assertInLoopThread();
	InetAddress peerAddr;
	//FIXME loop until no more
	//接受连接
	int connfd = acceptSocket_.accept(&peerAddr);
	if (connfd >= 0)
	{
		// string hostport = peerAddr.toIpPort();
		// LOG_TRACE << "Accepts of " << hostport;

		  //回调上层应用函数
		if (newConnectionCallback_)
		{
			newConnectionCallback_(connfd, peerAddr);
		}
		else
		{
			//如果上层应用程序没有设置回调函数，则把该connfd socket关闭
			sockets::close(connfd);
		}
	}
	else
	{
		LOG_SYSERR << "in Acceptor::handleRead";
		// Read the section named "The special problem of
		// accept()ing when you can't" in libev's doc.
		// By Marc Lehmann, author of libev.

		//如果失败了，可能的错误是文件描述符不够了
		if (errno == EMFILE)
		{
			//因为我们使用epoll的模式是电平触发，如果我们不处理就会一直触发该事件
			::close(idleFd_);
			idleFd_ = ::accept(acceptSocket_.fd(), NULL, NULL);
			::close(idleFd_);
			idleFd_ = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
		}
	}
}

