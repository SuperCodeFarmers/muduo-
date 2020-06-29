
#include "muduo/net/EventLoop.h"

#include "muduo/base/Logging.h"
#include "muduo/base/Mutex.h"
#include "muduo/net/Channel.h"
#include "muduo/net/Poller.h"
#include "muduo/net/SocketsOps.h"
#include "muduo/net/TimerQueue.h"

#include <algorithm>

#include <signal.h>
#include <sys/eventfd.h>
#include <unistd.h>

using namespace muduo;
using namespace muduo::net;

namespace
{
	//当前线程的EventLoop指针
	//线程的局部存储，__thread表示每个线程都有一个EventLoop指针，否则的话这个变量是共享的
	__thread EventLoop* t_loopInThisThread = 0;

	const int kPollTimeMs = 10000;

	int createEventfd()
	{
		int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
		if (evtfd < 0)
		{
			LOG_SYSERR << "Failed in eventfd";
			abort();
		}
		return evtfd;
	}

#pragma GCC diagnostic ignored "-Wold-style-cast"
	class IgnoreSigPipe
	{
	public:
		IgnoreSigPipe()
		{
			// 忽略SIGPIPE这个信号
			::signal(SIGPIPE, SIG_IGN);
			// LOG_TRACE << "Ignore SIGPIPE";
		}
	};
#pragma GCC diagnostic error "-Wold-style-cast"

	IgnoreSigPipe initObj;
}  // namespace

//获取当前EventLoop所在线程信息
EventLoop* EventLoop::getEventLoopOfCurrentThread()
{
	return t_loopInThisThread;
}

EventLoop::EventLoop()
	: looping_(false),//looping等于false表示还没有处于循环的状态
	quit_(false),
	eventHandling_(false),
	callingPendingFunctors_(false),
	iteration_(0),
	threadId_(CurrentThread::tid()),//当我们创建该对象时，我们就把该线程的ID进行缓存起来
	poller_(Poller::newDefaultPoller(this)),//创建轮询器对象
	timerQueue_(new TimerQueue(this)),
	wakeupFd_(createEventfd()),// 创建唤醒文件描述符eventfd
	wakeupChannel_(new Channel(this, wakeupFd_)),//创建一个通道，把wakeupFd传入
	currentActiveChannel_(NULL)
{
	LOG_DEBUG << "EventLoop created " << this << " in thread " << threadId_;
	//判断t_loopInThisThread，确保每个线程只有一个eventloop对象
	if (t_loopInThisThread)
	{
		//如果当前线程创建了EventLoop对象，终止(LOG_FATAL)
		LOG_FATAL << "Another EventLoop " << t_loopInThisThread
			<< " exists in this thread " << threadId_;
	}
	else
	{
		//如果不存在，则当前对象指针就是this
		t_loopInThisThread = this;
	}

	//设置wakeup通道的回调函数
	wakeupChannel_->setReadCallback(
		std::bind(&EventLoop::handleRead, this));
	// we are always reading the wakeupfd
	// 启动读事件
	wakeupChannel_->enableReading();
}

EventLoop::~EventLoop()
{
	LOG_DEBUG << "EventLoop " << this << " of thread " << threadId_
		<< " destructs in thread " << CurrentThread::tid();
	wakeupChannel_->disableAll();
	wakeupChannel_->remove();
	::close(wakeupFd_);
	//析构的时候要把EventLoop指针置为NULL
	t_loopInThisThread = NULL;
}

//事件循环，该函数不能跨线程调用
//只能在创建该对象的线程中调用
void EventLoop::loop()
{
	assert(!looping_);
	//断言当前处于创建该对象的线程当中
	assertInLoopThread();
	//开始事件循环，looping设为true
	looping_ = true;
	quit_ = false;  // FIXME: what if someone calls quit() before loop() ?
	LOG_TRACE << "EventLoop " << this << " start looping";

	while (!quit_)
	{
		//第一步:清除活动通道
		activeChannels_.clear();
		//第二步:调用poll，返回活动的通道activeChannels_
		pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_);
		++iteration_;
		if (Logger::logLevel() <= Logger::TRACE)
		{
			//打印活动通道，其实实在进行日志的登记
			printActiveChannels();
		}
		// TODO sort channel by priority
		eventHandling_ = true;
		//遍历活动通道，对其所对应的fd进行事件处理
		for (Channel* channel : activeChannels_)
		{
			// 设置当前正在处理的通道
			currentActiveChannel_ = channel;
			// 处理事件
			currentActiveChannel_->handleEvent(pollReturnTime_);
		}
		// 处理完后把当前正在处理通道置为NULL
		currentActiveChannel_ = NULL;
		eventHandling_ = false;
		// 运行等待(未决)函数
		doPendingFunctors();
	}

	LOG_TRACE << "EventLoop " << this << " stop looping";
	looping_ = false;
}

//该函数可以跨线程调用
void EventLoop::quit()
{
	quit_ = true;
	//有可能loop()在执行while(!quit_)时退出，
	//然后EventLoop销毁，然后我们访问一个无效的对象。
	//可以在两个地方使用mutex_进行修复。
	if (!isInLoopThread())
	{
		//如果不是在IO线程中调用，可能当前poll函数处于阻塞的状态，我们还要唤醒IO线程，以便循环能够跑到while(!quit_)进行判断
		//以便在loop中的时候，可以真正的走到loop，让程序退出。
		// 如果不这样做，就算quit_ = true,它也不能走到loop中的while(!quit_),让循环退出
		wakeup();
	}
}

//调用回调函数
//在IO线程中执行某个回调函数，该函数可以跨线程调用
void EventLoop::runInLoop(Functor cb)
{
	if (isInLoopThread())
	{
		//如果是当前IO线程调用runInLoop，则同步调用cb
		cb();
	}
	else
	{
		//如果是其他线程调用runInLoop，则异步的将cb添加到队列中
		queueInLoop(std::move(cb));
	}
}

//将任务添加到循环队列中
void EventLoop::queueInLoop(Functor cb)
{
	{
		//使用互斥锁进行保护
		MutexLockGuard lock(mutex_);
		pendingFunctors_.push_back(std::move(cb));
	}

	//调用queueInLoop的线程不是当前IO线程，为了及时的调用，需要唤醒
	//或者调用queueInLoop的线程是当前IO线程，并且此时正在调用PendingFunctor，需要唤醒
	//只有当前IO线程的事件回调中调用queueInLoop才不需要唤醒
	if (!isInLoopThread() || callingPendingFunctors_)
	{
		wakeup();
	}
}

size_t EventLoop::queueSize() const
{
	MutexLockGuard lock(mutex_);
	return pendingFunctors_.size();
}

//在某一时刻运行定时器
TimerId EventLoop::runAt(Timestamp time, TimerCallback cb)
{
	return timerQueue_->addTimer(std::move(cb), time, 0.0);
}

//过一段时间运行定时器
TimerId EventLoop::runAfter(double delay, TimerCallback cb)
{
	Timestamp time(addTime(Timestamp::now(), delay));
	return runAt(time, std::move(cb));
}

//每隔一段时间运行定时器
TimerId EventLoop::runEvery(double interval, TimerCallback cb)
{
	Timestamp time(addTime(Timestamp::now(), interval));
	return timerQueue_->addTimer(std::move(cb), time, interval);
}

void EventLoop::cancel(TimerId timerId)
{
	return timerQueue_->cancel(timerId);
}

void EventLoop::updateChannel(Channel* channel)
{
	// channel 所处的Loop必须是当前EventLoop对象
	assert(channel->ownerLoop() == this);
	assertInLoopThread();
	poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel* channel)
{
	assert(channel->ownerLoop() == this);
	assertInLoopThread();
	if (eventHandling_)
	{
		assert(currentActiveChannel_ == channel ||
			std::find(activeChannels_.begin(), activeChannels_.end(), channel) == activeChannels_.end());
	}
	poller_->removeChannel(channel);
}

bool EventLoop::hasChannel(Channel* channel)
{
	assert(channel->ownerLoop() == this);
	assertInLoopThread();
	return poller_->hasChannel(channel);
}

void EventLoop::abortNotInLoopThread()
{
	LOG_FATAL << "EventLoop::abortNotInLoopThread - EventLoop " << this
		<< " was created in threadId_ = " << threadId_
		<< ", current thread id = " << CurrentThread::tid();
}

//唤醒eventFD
void EventLoop::wakeup()
{
	//唤醒的方式:向eventFD写入8Bity的数据，就可以唤醒等待的线程
	uint64_t one = 1;//8字节的缓冲区
	ssize_t n = sockets::write(wakeupFd_, &one, sizeof one);
	if (n != sizeof one)
	{
		LOG_ERROR << "EventLoop::wakeup() writes " << n << " bytes instead of 8";
	}
}

void EventLoop::handleRead()
{
	uint64_t one = 1;
	ssize_t n = sockets::read(wakeupFd_, &one, sizeof one);
	if (n != sizeof one)
	{
		LOG_ERROR << "EventLoop::handleRead() reads " << n << " bytes instead of 8";
	}
}

//运行等待任务
void EventLoop::doPendingFunctors()
{
	std::vector<Functor> functors;
	callingPendingFunctors_ = true;

	{
		MutexLockGuard lock(mutex_);
		functors.swap(pendingFunctors_);
	}

	for (const Functor& functor : functors)
	{
		functor();
	}
	callingPendingFunctors_ = false;
}

void EventLoop::printActiveChannels() const
{
	for (const Channel* channel : activeChannels_)
	{
		LOG_TRACE << "{" << channel->reventsToString() << "} ";
	}
}

