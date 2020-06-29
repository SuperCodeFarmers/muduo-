// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/net/EventLoopThreadPool.h"

#include "muduo/net/EventLoop.h"
#include "muduo/net/EventLoopThread.h"

#include <stdio.h>

using namespace muduo;
using namespace muduo::net;

EventLoopThreadPool::EventLoopThreadPool(EventLoop* baseLoop, const string& nameArg)
	: baseLoop_(baseLoop),
	name_(nameArg),
	started_(false),
	numThreads_(0),
	next_(0)
{
}

EventLoopThreadPool::~EventLoopThreadPool()
{
	// Don't delete loop, it's stack variable
}

// 启动线程池
void EventLoopThreadPool::start(const ThreadInitCallback& cb)
{
	assert(!started_);
	baseLoop_->assertInLoopThread();

	started_ = true;

	for (int i = 0; i < numThreads_; ++i)
	{
		char buf[name_.size() + 32];
		snprintf(buf, sizeof buf, "%s%d", name_.c_str(), i);
		EventLoopThread* t = new EventLoopThread(cb, buf);	//创建一个线程
		threads_.push_back(std::unique_ptr<EventLoopThread>(t));	// 把这个线程压入EventLoop当中
		loops_.push_back(t->startLoop());	// 启动EventLoopThread线程，在进入事件循环之前，会调用cb
	}

	// numThreads_ == 0说明没有创建这些IO线程，并且cb不为空，那么我们也调用一下cb
	if (numThreads_ == 0 && cb)
	{
		// 只有一个EventLoop，在这个EventLoop进入事件循环之前，调用cb
		cb(baseLoop_);
	}
}

EventLoop* EventLoopThreadPool::getNextLoop()
{
	// 判断是否在当前线程中
	baseLoop_->assertInLoopThread();
	assert(started_);
	EventLoop* loop = baseLoop_;	// 先把loop指针指向baseLoop_这个loop指针就是acceptor所属的Reactor，也就是MainReactor

	// 如果loops_为空，这loop指向baseLoop
	// 如果不为空，安装rund-robin（RR 轮叫）的调度方式选择一个EventLoop
	// 如果这个IO线程池它创建的线程个数为0，则这个loop对选择的还是baseLoop_，即mainReactor，这个就是单线程模式，这时baseLoop不仅处理监听套接字还处理已连接套接字
	// 如果这个线程池中有线程，则这个线程只处理监听套接字accept这个通道是否处于活跃状态
	if (!loops_.empty())
	{
		// round-robin
		loop = loops_[next_];// 在这个列表中选择一个loop
		++next_;// 下一个evenLoop的下标

		// 如果下标大于size()了，就重置一下
		if (implicit_cast<size_t>(next_) >= loops_.size())
		{
			next_ = 0;
		}
	}
	return loop;
}

EventLoop* EventLoopThreadPool::getLoopForHash(size_t hashCode)
{
	baseLoop_->assertInLoopThread();
	EventLoop* loop = baseLoop_;

	if (!loops_.empty())
	{
		loop = loops_[hashCode % loops_.size()];
	}
	return loop;
}

std::vector<EventLoop*> EventLoopThreadPool::getAllLoops()
{
	baseLoop_->assertInLoopThread();
	assert(started_);
	if (loops_.empty())
	{
		return std::vector<EventLoop*>(1, baseLoop_);
	}
	else
	{
		return loops_;
	}
}
