#include "muduo/net/Timer.h"

using namespace muduo;
using namespace muduo::net;

AtomicInt64 Timer::s_numCreated_;

void Timer::restart(Timestamp now)
{
	//如果是重复的定时器
	if (repeat_)
	{
		//得到当前时间，加上一个时间间隔
		expiration_ = addTime(now, interval_);
	}
	else
	{
		//下一个超时时刻等于一个非法时间
		expiration_ = Timestamp::invalid();
	}
}
