#include "muduo/net/EventLoopThread.h"

#include "muduo/net/EventLoop.h"

using namespace muduo;
using namespace muduo::net;

EventLoopThread::EventLoopThread(const ThreadInitCallback& cb,
	const string& name)
	: loop_(NULL),
	exiting_(false),
	thread_(std::bind(&EventLoopThread::threadFunc, this), name), //初始化线程函数
	mutex_(),//初始化mutex
	cond_(mutex_),//条件变量要跟一个mutex一起配合使用
	callback_(cb)
{
}

EventLoopThread::~EventLoopThread()
{
	exiting_ = true;
	if (loop_ != NULL) // not 100% race-free, eg. threadFunc could be running callback_.
	{
		loop_->quit();//退出IO线程，让IO线程的loop循环退出，从而退出IO线程
		thread_.join();//关闭线程
	}
}

//启动线程，并且这个线程成为了IO线程
EventLoop* EventLoopThread::startLoop()
{
	assert(!thread_.started());//断言判断这个线程还没有启动
	thread_.start();//启动线程
	//当这个线程启动后，就调用回调函数，就是EventLoopThread::threadFunc
	//所以有两个线程在运行：1个是startLoop()，一个是threadFunc()
	//这两个线程的运行顺序是不缺定的，
	//所以这个例用一个while循环等待loop_指针不为空

	EventLoop* loop = NULL;
	{
		MutexLockGuard lock(mutex_);
		while (loop_ == NULL)
		{
			cond_.wait();
		}
		loop = loop_;
	}

	return loop;
}

void EventLoopThread::threadFunc()
{
	EventLoop loop;

	//如果在创建EventLoopThread对象时，传入了一个callback_
	//就会在callback_中做一个初始化的工作
	if (callback_)
	{
		callback_(&loop);
	}

	{
		MutexLockGuard lock(mutex_);
		//loop_指针指向了一个栈上的对象，threadFunc函数退出后，这个指针就失效了
		//threadFunc函数退出，就以为了线程退出了，EventLoopThread对象也就没有存在的意义了
		//因而不会有什么大的问题
		loop_ = &loop;
		cond_.notify();
	}

	loop.loop();
	//assert(exiting_);
	MutexLockGuard lock(mutex_);
	loop_ = NULL;
}

