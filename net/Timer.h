// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_TIMER_H
#define MUDUO_NET_TIMER_H

#include "muduo/base/Atomic.h"
#include "muduo/base/Timestamp.h"
#include "muduo/net/Callbacks.h"

namespace muduo
{
	namespace net
	{

		///
		/// Internal class for timer event.
		///
		class Timer : noncopyable
		{
		public:
			Timer(TimerCallback cb, Timestamp when, double interval)
				: callback_(std::move(cb)),
				expiration_(when),
				interval_(interval),
				repeat_(interval > 0.0),
				sequence_(s_numCreated_.incrementAndGet())//先加后获取
			{ }

			//run调用回调函数
			void run() const
			{
				callback_();
			}

			Timestamp expiration() const { return expiration_; }
			bool repeat() const { return repeat_; }
			int64_t sequence() const { return sequence_; }

			//重启
			void restart(Timestamp now);

			static int64_t numCreated() { return s_numCreated_.get(); }

		private:
			const TimerCallback callback_;	//定时器的回调函数
			Timestamp expiration_;		//下一次的超时时刻，当超时时刻到来时，定时器的回调函数会被调用
			const double interval_;		//超时时间间隔，如果是一次性定时器，该值为0
			const bool repeat_;			//是否重复，如果为false是一次定时器
			const int64_t sequence_;	//定时器序号

			static AtomicInt64 s_numCreated_;	//定时器计数，当前已经创建的定时器数量
		};

	}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_TIMER_H
