#ifndef MUDUO_NET_EVENTLOOP_H
#define MUDUO_NET_EVENTLOOP_H

#include <atomic>
#include <functional>
#include <vector>

#include <boost/any.hpp>

#include "muduo/base/Mutex.h"
#include "muduo/base/CurrentThread.h"
#include "muduo/base/Timestamp.h"
#include "muduo/net/Callbacks.h"
#include "muduo/net/TimerId.h"

namespace muduo
{
	namespace net
	{
		//前项声明class Channel;class Poller;class Channel;
		class Poller;
		class TimerQueue;

		///
		/// Reactor, at most one per thread.
		///
		/// This is an interface class, so don't expose too much details.
		class EventLoop : noncopyable
		{
		public:
			typedef std::function<void()> Functor;

			EventLoop();
			~EventLoop();  // force out-line dtor, for std::unique_ptr members.

			///
			/// Loops forever.
			///
			/// Must be called in the same thread as creation of the object.
			///
			void loop();

			/// Quits loop.
			///
			/// This is not 100% thread safe, if you call through a raw pointer,
			/// better to call through shared_ptr<EventLoop> for 100% safety.
			void quit();

			///
			/// Time when poll returns, usually means data arrival.
			///
			Timestamp pollReturnTime() const { return pollReturnTime_; }

			int64_t iteration() const { return iteration_; }

			/// Runs callback immediately in the loop thread.
			/// It wakes up the loop, and run the cb.
			/// If in the same loop thread, cb is run within the function.
			/// Safe to call from other threads.
		  //在循环线程中立即运行回调。
		  //它唤醒循环，并运行cb。
		  //如果在同一个循环线程中，则在函数中运行cb。
		  //从其他线程调用是安全的。
			void runInLoop(Functor cb);
			/// Queues callback in the loop thread.
			/// Runs after finish pooling.
			/// Safe to call from other threads.
			void queueInLoop(Functor cb);

			size_t queueSize() const;

			// timers

			///
			/// Runs callback at 'time'.
			/// Safe to call from other threads.
			//在某一时刻运行定时器
			TimerId runAt(Timestamp time, TimerCallback cb);
			///
			/// Runs callback after @c delay seconds.
			/// Safe to call from other threads.
			//过一段时间运行定时器
			TimerId runAfter(double delay, TimerCallback cb);
			///
			/// Runs callback every @c interval seconds.
			/// Safe to call from other threads.
			//每隔一段时间运行定时器
			TimerId runEvery(double interval, TimerCallback cb);
			///
			/// Cancels the timer.
			/// Safe to call from other threads.
			//取消定时器
			void cancel(TimerId timerId);

			// internal usage
			void wakeup();//唤醒
			void updateChannel(Channel* channel);/*在Poller中添加或者更新通道*/
			void removeChannel(Channel* channel);/*从Poller中移除通道*/
			bool hasChannel(Channel* channel);

			// pid_t threadId() const { return threadId_; }
			void assertInLoopThread()
			{
				//如果是的话什么都不做，否的话
				if (!isInLoopThread())
				{
					abortNotInLoopThread();
				}
			}
			//判断是否处于对象线程中
			bool isInLoopThread() const { return threadId_ == CurrentThread::tid(); }
			// bool callingPendingFunctors() const { return callingPendingFunctors_; }
			bool eventHandling() const { return eventHandling_; }

			void setContext(const boost::any& context)
			{
				context_ = context;
			}

			const boost::any& getContext() const
			{
				return context_;
			}

			boost::any* getMutableContext()
			{
				return &context_;
			}

			static EventLoop* getEventLoopOfCurrentThread();

		private:
			void abortNotInLoopThread();
			void handleRead();  // waked up
			void doPendingFunctors();

			void printActiveChannels() const; // DEBUG

			typedef std::vector<Channel*> ChannelList;

			bool looping_; /*是否处于事件循环状态 atomic */
			std::atomic<bool> quit_;/*是否退出，原子类型*/
			bool eventHandling_; /*当前是否处于事件处理的状态 atomic */
			bool callingPendingFunctors_; /* atomic */
			int64_t iteration_;
			const pid_t threadId_;//当前对象所属线程ID
			Timestamp pollReturnTime_;/*调用poll函数时返回的时间戳*/
			std::unique_ptr<Poller> poller_;/*poller的生存期由EventLoop控制*/
			std::unique_ptr<TimerQueue> timerQueue_;

			//唤醒文件描述符，用于事件的通知，是eventfd()所创建的文件描述符
			//用于线程或进程间的通信
			int wakeupFd_;

			// unlike in TimerQueue, which is an internal class,
			// we don't expose Channel to client.
			//wakeupChannel_是eventfd所创建的文件描述符通道

			//该通道将会纳入到poller_中来管理
			std::unique_ptr<Channel> wakeupChannel_;
			boost::any context_;

			// scratch variables
			ChannelList activeChannels_;/*Channel返回的活动事件的通道*/
			Channel* currentActiveChannel_;/*当前正在处理的活动通道*/

			mutable MutexLock mutex_;
			std::vector<Functor> pendingFunctors_ GUARDED_BY(mutex_);
		};

	}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_EVENTLOOP_H
