// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/net/poller/EPollPoller.h"

#include "muduo/base/Logging.h"
#include "muduo/net/Channel.h"

#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <sys/epoll.h>
#include <unistd.h>

using namespace muduo;
using namespace muduo::net;

// On Linux, the constants of poll(2) and epoll(4)
// are expected to be the same.
static_assert(EPOLLIN == POLLIN, "epoll uses same flag values as poll");
static_assert(EPOLLPRI == POLLPRI, "epoll uses same flag values as poll");
static_assert(EPOLLOUT == POLLOUT, "epoll uses same flag values as poll");
static_assert(EPOLLRDHUP == POLLRDHUP, "epoll uses same flag values as poll");
static_assert(EPOLLERR == POLLERR, "epoll uses same flag values as poll");
static_assert(EPOLLHUP == POLLHUP, "epoll uses same flag values as poll");

namespace
{
	const int kNew = -1;
	const int kAdded = 1;
	const int kDeleted = 2;
}

EPollPoller::EPollPoller(EventLoop* loop)
	: Poller(loop),
	epollfd_(::epoll_create1(EPOLL_CLOEXEC)),/*epoll_create1，
											 如果flags参数为0，则除了省略了size参数之外，它与epoll_create是相同的。
											 如果flags参数不为0，则目前它只能是EPOLL_CLOEXEC，
											 用于设置该描述符的close-on-exec(FD_CLOEXEC)标志。*/
	events_(kInitEventListSize)//预先分配容纳事件额空间
{
	if (epollfd_ < 0)
	{
		LOG_SYSFATAL << "EPollPoller::EPollPoller";
	}
}

EPollPoller::~EPollPoller()
{
	::close(epollfd_);
}

Timestamp EPollPoller::poll(int timeoutMs, ChannelList* activeChannels)
{
	LOG_TRACE << "fd total count " << channels_.size();
	//使用epoll模型，调用epoll_wait
	int numEvents = ::epoll_wait(epollfd_,
		&*events_.begin(),//返回的事件，保存到事件列表当中
		static_cast<int>(events_.size()),
		timeoutMs);
	int savedErrno = errno;
	Timestamp now(Timestamp::now());
	if (numEvents > 0)
	{
		LOG_TRACE << numEvents << " events happened";
		fillActiveChannels(numEvents, activeChannels);
		if (implicit_cast<size_t>(numEvents) == events_.size())
		{
			//当事件列表里面位置不够用时，我们进行动态扩容
			events_.resize(events_.size() * 2);
		}
	}
	else if (numEvents == 0)
	{
		LOG_TRACE << "nothing happened";
	}
	else
	{
		// error happens, log uncommon ones
		if (savedErrno != EINTR)
		{
			errno = savedErrno;
			LOG_SYSERR << "EPollPoller::poll()";
		}
	}
	return now;
}

//填充活跃的通道
//把返回的事件添加到ChannelList中
void EPollPoller::fillActiveChannels(int numEvents,
	ChannelList* activeChannels) const
{
	assert(implicit_cast<size_t>(numEvents) <= events_.size());
	for (int i = 0; i < numEvents; ++i)
	{
		Channel* channel = static_cast<Channel*>(events_[i].data.ptr);
#ifndef NDEBUG
		int fd = channel->fd();
		ChannelMap::const_iterator it = channels_.find(fd);
		assert(it != channels_.end());
		assert(it->second == channel);
#endif
		channel->set_revents(events_[i].events);
		//把有事件的通道加入 活跃通道队列中
		activeChannels->push_back(channel);
	}
}

void EPollPoller::updateChannel(Channel* channel)
{
	//断言判断是否处于IO线程当中
	Poller::assertInLoopThread();
	//index在EPollPoller中表示的是状态
	const int index = channel->index();
	LOG_TRACE << "fd = " << channel->fd()
		<< " events = " << channel->events() << " index = " << index;
	if (index == kNew || index == kDeleted)
	{
		// a new one, add with EPOLL_CTL_ADD
		int fd = channel->fd();

		//当index==kNew时，说明是新的通道
		if (index == kNew)
		{
			assert(channels_.find(fd) == channels_.end());
			//将新的fd和channel添加到ChannelMap中
			channels_[fd] = channel;
		}
		else // index == kDeleted
		{
			assert(channels_.find(fd) != channels_.end());
			assert(channels_[fd] == channel);
		}

		//设置channel的状态，为kAdded表示该通道为已添加的状态
		channel->set_index(kAdded);
		//在把通道添加到Epoll中
		update(EPOLL_CTL_ADD, channel);
	}
	else
	{
		// update existing one with EPOLL_CTL_MOD/DEL
		int fd = channel->fd();
		(void)fd;
		assert(channels_.find(fd) != channels_.end());
		assert(channels_[fd] == channel);
		assert(index == kAdded);
		if (channel->isNoneEvent())
		{
			update(EPOLL_CTL_DEL, channel);
			channel->set_index(kDeleted);
		}
		else
		{
			update(EPOLL_CTL_MOD, channel);
		}
	}
}

//移除通道
void EPollPoller::removeChannel(Channel* channel)
{
	Poller::assertInLoopThread();
	int fd = channel->fd();
	LOG_TRACE << "fd = " << fd;
	assert(channels_.find(fd) != channels_.end());
	assert(channels_[fd] == channel);
	assert(channel->isNoneEvent());

	//获取通道状态
	int index = channel->index();
	//断言 当前通道的状态为kAdded或kDeleted，它不能等于kNew，如果等于kNew说明根本就不在通道当中
	assert(index == kAdded || index == kDeleted);
	size_t n = channels_.erase(fd);
	(void)n;
	assert(n == 1);

	//如果等于kAdded，还要在Epoll当中移除
	if (index == kAdded)
	{
		update(EPOLL_CTL_DEL, channel);
	}
	channel->set_index(kNew);
}

void EPollPoller::update(int operation, Channel* channel)
{
	struct epoll_event event;
	memZero(&event, sizeof event);//初始化event结构体
	event.events = channel->events();
	event.data.ptr = channel;//event中的数据指针指向事件通道
	int fd = channel->fd();
	LOG_TRACE << "epoll_ctl op = " << operationToString(operation)
		<< " fd = " << fd << " event = { " << channel->eventsToString() << " }";
	if (::epoll_ctl(epollfd_, operation, fd, &event) < 0)
	{
		if (operation == EPOLL_CTL_DEL)
		{
			//如果删除失败，不会退出程序
			LOG_SYSERR << "epoll_ctl op =" << operationToString(operation) << " fd =" << fd;
		}
		else
		{
			//如果是添加或修改失败了，就退出程序
			LOG_SYSFATAL << "epoll_ctl op =" << operationToString(operation) << " fd =" << fd;
		}
	}
}

const char* EPollPoller::operationToString(int op)
{
	switch (op)
	{
	case EPOLL_CTL_ADD:
		return "ADD";
	case EPOLL_CTL_DEL:
		return "DEL";
	case EPOLL_CTL_MOD:
		return "MOD";
	default:
		assert(false && "ERROR op");
		return "Unknown Operation";
	}
}
