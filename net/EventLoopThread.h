#ifndef MUDUO_NET_EVENTLOOPTHREAD_H
#define MUDUO_NET_EVENTLOOPTHREAD_H

#include "muduo/base/Condition.h"
#include "muduo/base/Mutex.h"
#include "muduo/base/Thread.h"

namespace muduo
{
	namespace net
	{

		class EventLoop;
		/*
		安装面向对象的思想，EventLoopThread应该是继承Thread类的一个类，
		但是muduo使用基于对象的思想，它包含了一个Thread类的对象
		*/
		class EventLoopThread : noncopyable
		{
		public:
			typedef std::function<void(EventLoop*)> ThreadInitCallback;

			EventLoopThread(const ThreadInitCallback& cb = ThreadInitCallback(),
				const string& name = string());
			~EventLoopThread();
			EventLoop* startLoop(); //启动线程，该线程就成为了IO线程

		private:
			void threadFunc();//线程函数

			EventLoop* loop_ GUARDED_BY(mutex_);//指向一个EventLoop对象,一个IO线程有且只有一个EventLoop对象
			bool exiting_;
			Thread thread_;
			MutexLock mutex_;
			Condition cond_ GUARDED_BY(mutex_);
			ThreadInitCallback callback_;//回调函数在EventLoop;:loop事件循环之前被调用
		};

	}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_EVENTLOOPTHREAD_H

