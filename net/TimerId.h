
#ifndef MUDUO_NET_TIMERID_H
#define MUDUO_NET_TIMERID_H

#include "muduo/base/copyable.h"

namespace muduo
{
	namespace net
	{

		class Timer;

		///
		/// An opaque identifier, for canceling Timer.
		//一个不透明的标识符，用于取消计时器。
		/*
		不透明的意思就是我们要关注它的存在，我们要看到它的存在。
		透明的是我们不需要关注它的存在
		*/
		class TimerId : public muduo::copyable
		{
		public:
			TimerId()
				: timer_(NULL),
				sequence_(0)
			{
			}

			TimerId(Timer* timer, int64_t seq)
				: timer_(timer),
				sequence_(seq)
			{
			}

			// default copy-ctor, dtor and assignment are okay

			friend class TimerQueue;

		private:
			Timer* timer_;//定时器对象
			int64_t sequence_;//定时器序号
		};

	}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_TIMERID_H
