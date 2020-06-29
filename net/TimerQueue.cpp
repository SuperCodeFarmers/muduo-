// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS
#endif

#include "muduo/net/TimerQueue.h"

#include "muduo/base/Logging.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/Timer.h"
#include "muduo/net/TimerId.h"

#include <sys/timerfd.h>
#include <unistd.h>

namespace muduo
{
	namespace net
	{
		namespace detail
		{

			int createTimerfd()
			{
				int timerfd = ::timerfd_create(CLOCK_MONOTONIC,
					TFD_NONBLOCK | TFD_CLOEXEC);
				if (timerfd < 0)
				{
					LOG_SYSFATAL << "Failed in timerfd_create";
				}
				return timerfd;
			}


			//把Timestamp转成itimerspec格式
			//计算超时时刻与当前时间的时间差
			struct timespec howMuchTimeFromNow(Timestamp when)
			{
				//超时时刻的微妙数-当前时刻的微妙数
				int64_t microseconds = when.microSecondsSinceEpoch()
					- Timestamp::now().microSecondsSinceEpoch();
				//如果小于100我们直接赋给100，精确度不用太高
				if (microseconds < 100)
				{
					microseconds = 100;
				}
				struct timespec ts;
				ts.tv_sec = static_cast<time_t>(
					microseconds / Timestamp::kMicroSecondsPerSecond);
				ts.tv_nsec = static_cast<long>(
					(microseconds % Timestamp::kMicroSecondsPerSecond) * 1000);
				return ts;
			}

			void readTimerfd(int timerfd, Timestamp now)
			{
				uint64_t howmany;
				ssize_t n = ::read(timerfd, &howmany, sizeof howmany);
				LOG_TRACE << "TimerQueue::handleRead() " << howmany << " at " << now.toString();
				if (n != sizeof howmany)
				{
					LOG_ERROR << "TimerQueue::handleRead() reads " << n << " bytes instead of 8";
				}
			}

			//重置定时器的超时时刻
			void resetTimerfd(int timerfd, Timestamp expiration)
			{
				// wake up loop by timerfd_settime()
				struct itimerspec newValue;
				struct itimerspec oldValue;
				memZero(&newValue, sizeof newValue);
				memZero(&oldValue, sizeof oldValue);
				newValue.it_value = howMuchTimeFromNow(expiration);
				int ret = ::timerfd_settime(timerfd, 0, &newValue, &oldValue);
				if (ret)
				{
					LOG_SYSERR << "timerfd_settime()";
				}
			}

		}  // namespace detail
	}  // namespace net
}  // namespace muduo

using namespace muduo;
using namespace muduo::net;
using namespace muduo::net::detail;

TimerQueue::TimerQueue(EventLoop* loop)
	: loop_(loop),
	timerfd_(createTimerfd()),//创建定时器文件描述符
	timerfdChannel_(loop, timerfd_),
	timers_(),
	callingExpiredTimers_(false)
{
	//当定时器通道可读事件产生的时候会回调handleRead成员函数
	timerfdChannel_.setReadCallback(std::bind(&TimerQueue::handleRead, this));
	// we are always reading the timerfd, we disarm it with timerfd_settime.
	//这个通道会加入Epoll中
	timerfdChannel_.enableReading();
}

TimerQueue::~TimerQueue()
{
	timerfdChannel_.disableAll();
	timerfdChannel_.remove();
	::close(timerfd_);
	// do not remove channel, since we're in EventLoop::dtor();
	for (const Entry& timer : timers_)
	{
		delete timer.second;
	}
}

//增加一个定时器
TimerId TimerQueue::addTimer(TimerCallback cb,//定时器回调函数
	Timestamp when,//超时事件
	double interval)//时间间隔
{
	//构造一个定时器对象
	Timer* timer = new Timer(std::move(cb), when, interval);
	//调用回调函数
	loop_->runInLoop(
		std::bind(&TimerQueue::addTimerInLoop, this, timer));
	return TimerId(timer, timer->sequence());
}

void TimerQueue::cancel(TimerId timerId)
{
	loop_->runInLoop(
		std::bind(&TimerQueue::cancelInLoop, this, timerId));
}

//添加定时器到EventLoop中
void TimerQueue::addTimerInLoop(Timer* timer)
{
	//断言是否处于IO线程当中
	loop_->assertInLoopThread();
	//插入一个定时器，有可能会使得最早到期的定时器发生改变
	//TimerQueue维护了一系列的定时器，那么这些定时器都有自己的到期时间
	//我们新插入的定时器到期时间可能比原有的定时器到期时间早
	//那么earliestChanged，就为true
	bool earliestChanged = insert(timer);

	if (earliestChanged)
	{
		//重置定时器的超时时刻（timerfd_settime）
		//我们能要把早先的定时器先触发
		resetTimerfd(timerfd_/*定时器的fd*/, timer->expiration()/*到期事件*/);
	}
}

//取消循环
void TimerQueue::cancelInLoop(TimerId timerId)
{
	loop_->assertInLoopThread();
	assert(timers_.size() == activeTimers_.size());
	ActiveTimer timer(timerId.timer_, timerId.sequence_);
	//查找该定时器
	ActiveTimerSet::iterator it = activeTimers_.find(timer);
	if (it != activeTimers_.end())
	{
		size_t n = timers_.erase(Entry(it->first->expiration(), it->first));
		assert(n == 1); (void)n;
		delete it->first; // 如果使用unique_ptr，就不用手动删除了
		activeTimers_.erase(it);//移除
	}
	else if (callingExpiredTimers_)//如果定时器不在列表当中，就是已经到期了
	{
		//已经到期了，并且正在调用回调函数的定时器
		cancelingTimers_.insert(timer);
	}
	assert(timers_.size() == activeTimers_.size());
}


void TimerQueue::handleRead()
{
	loop_->assertInLoopThread();
	Timestamp now(Timestamp::now());
	readTimerfd(timerfd_, now);//清除该事件，避免一直触发

	//获取该时刻之前所有的定时器列表（即超时定时器列表）
	std::vector<Entry> expired = getExpired(now);

	callingExpiredTimers_ = true;
	cancelingTimers_.clear();
	// safe to callback outside critical section
	for (const Entry& it : expired)
	{
		//调用定时器的回调函数
		it.second->run();
	}
	callingExpiredTimers_ = false;

	//如果不是一次性定时器，我们要重启
	reset(expired, now);
}


//获取该时刻之前所有的定时器列表（即超时定时器列表）
std::vector<TimerQueue::Entry> TimerQueue::getExpired(Timestamp now)
{
	assert(timers_.size() == activeTimers_.size());
	std::vector<Entry> expired;
	Entry sentry(now, reinterpret_cast<Timer*>(UINTPTR_MAX));
	//返回第一个未到期的Timer的迭代器
	//lower_bound的含义是返回第一个值 >= sentry的元素的iterator
	//即*end >= sentry,从而end->fist > now
	TimerList::iterator end = timers_.lower_bound(sentry);
	assert(end == timers_.end() || now < end->first);
	//将到期的定时器插入到 expired
	std::copy(timers_.begin(), end, back_inserter(expired));
	//从timers_中移除到期的定时器
	timers_.erase(timers_.begin(), end);

	//从activeTimers_中移除到期的定时器
	for (const Entry& it : expired)
	{
		ActiveTimer timer(it.second, it.second->sequence());
		size_t n = activeTimers_.erase(timer);
		assert(n == 1); (void)n;
	}

	assert(timers_.size() == activeTimers_.size());
	return expired;
}

void TimerQueue::reset(const std::vector<Entry>& expired, Timestamp now)
{
	Timestamp nextExpire;

	for (const Entry& it : expired)
	{
		ActiveTimer timer(it.second, it.second->sequence());
		//如果是重复的定时器并且是未取消定时器，则重启该定时器
		if (it.second->repeat()
			&& cancelingTimers_.find(timer) == cancelingTimers_.end())
		{
			it.second->restart(now);
			insert(it.second);
		}
		else
		{
			//一次性定时器不用重置或者已被取消的定时器是不能重置的，因此直接删除该定时器
		  // FIXME move to a free list
			delete it.second; // FIXME: no delete please
		}
	}

	if (!timers_.empty())
	{
		nextExpire = timers_.begin()->second->expiration();
	}

	if (nextExpire.valid())
	{
		resetTimerfd(timerfd_, nextExpire);
	}
}

bool TimerQueue::insert(Timer* timer)
{
	//断言
	loop_->assertInLoopThread();
	assert(timers_.size() == activeTimers_.size());
	//最早到期时间是否改变
	bool earliestChanged = false;
	Timestamp when = timer->expiration();//取出到期时间
	TimerList::iterator it = timers_.begin();
	if (it == timers_.end() || when < it->first)
	{
		//timers是空的
		//如果定时器的到期时间早于原有的定时器到期时间，把earliestChanged置为true
		earliestChanged = true;
	}
	{
		//插入到timers_
		std::pair<TimerList::iterator, bool> result = timers_.insert(Entry(when, timer));
		assert(result.second);
		(void)result;
	}
	{
		//插入到activeTimers_
		std::pair<ActiveTimerSet::iterator, bool> result = activeTimers_.insert(ActiveTimer(timer, timer->sequence()));
		assert(result.second);
		(void)result;
	}

	assert(timers_.size() == activeTimers_.size());
	return earliestChanged;
}

