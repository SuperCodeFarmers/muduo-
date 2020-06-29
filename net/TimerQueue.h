// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_TIMERQUEUE_H
#define MUDUO_NET_TIMERQUEUE_H

#include <set>
#include <vector>

#include "muduo/base/Mutex.h"
#include "muduo/base/Timestamp.h"
#include "muduo/net/Callbacks.h"
#include "muduo/net/Channel.h"

namespace muduo
{
	namespace net
	{

		class EventLoop;
		class Timer;
		class TimerId;

		///
		/// A best efforts timer queue.
		/// No guarantee that the callback will be on time.
		///
		class TimerQueue : noncopyable
		{
		public:

			//一个TimerQueue对象属于一个EventLoop对象
			explicit TimerQueue(EventLoop* loop);
			~TimerQueue();

			/// Schedules the callback to be run at given time,
			/// repeats if @c interval > 0.0.
			///
			/// Must be thread safe. Usually be called from other threads.
			//一定是线程安全的，可以跨线程调用。通常情况下被其他线程调用。
			//可以不在所属EventLoop线程中调用
			//添加定时器，返回TimerID对象
			TimerId addTimer(TimerCallback cb,
				Timestamp when,
				double interval);
			//cancel也可以跨线程调用
			//取消定时器，需要指明定时器ID才能取消定时器
			void cancel(TimerId timerId);

		private:

			// FIXME: use unique_ptr<Timer> instead of raw pointers.
			// This requires heterogeneous comparison lookup (N3465) from C++14
			// so that we can find an T* in a set<unique_ptr<T>>.
			   //unique_ptr是C++11标准的一个独享所有权的智能指针
			   //但可以进行移动构造与移动赋值操作，即所有权可以移动到另一个对象（而非拷贝构造）
			typedef std::pair<Timestamp, Timer*> Entry;
			typedef std::set<Entry> TimerList;//TimerList以时间戳来进行排序

			//ActiveTimerSet与TimerList是一样的，只不过其排序方式不一样
			//ActiveTimerSet按时间戳地址排序
			typedef std::pair<Timer*, int64_t> ActiveTimer;
			typedef std::set<ActiveTimer> ActiveTimerSet;

			//一下成员函数只可以在其所属的IO线程中调用。因而不需要加锁
			//服务器性能杀手之一是锁竞争，所以要尽可能少用锁
			void addTimerInLoop(Timer* timer);
			void cancelInLoop(TimerId timerId);
			// called when timerfd alarms
			void handleRead();//处理可读事件
			// move out all expired timers
			//返回超时的定时器列表，在该时刻可能有多个超时的定时器
			std::vector<Entry> getExpired(Timestamp now);
			//对这些超时的定时器重置，因为这些定时器可能是可重复的定时器
			void reset(const std::vector<Entry>& expired, Timestamp now);

			bool insert(Timer* timer);

			EventLoop* loop_;//所属EventLoop
			const int timerfd_;//timerfd_create所创建出来的定时器文件描述符
			Channel timerfdChannel_;//定时器通道，当定时器事件到来的时候，可读事件产生，会回调handleRead函数
			// Timer list sorted by expiration
			TimerList timers_;//timers_是按到期时间排序的列表

			// for cancel()
			ActiveTimerSet activeTimers_;	//按照Timer地址排序的列表
			bool callingExpiredTimers_;		//atomic 是否处于调用超时的定时器当中
			ActiveTimerSet cancelingTimers_;//保存的是被取消的定时器
		};

	}  // namespace net
}  // namespace muduo
#endif  // MUDUO_NET_TIMERQUEUE_H
