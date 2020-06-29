#include "muduo/base/Logging.h"
#include "muduo/net/Channel.h"
#include "muduo/net/EventLoop.h"

#include <sstream>

#include <poll.h>

using namespace muduo;
using namespace muduo::net;

const int Channel::kNoneEvent = 0;
const int Channel::kReadEvent = POLLIN | POLLPRI;//关注数据到来事件和紧急数据事件
const int Channel::kWriteEvent = POLLOUT;//数据可写事件

Channel::Channel(EventLoop* loop, int fd__)
	: loop_(loop),
	fd_(fd__),
	events_(0),
	revents_(0),
	index_(-1),//在构造Channel对象时，index是-1，在EpollPoller中是kNew的状态
	logHup_(true),
	tied_(false),
	eventHandling_(false),
	addedToLoop_(false)
{
}

Channel::~Channel()
{
	assert(!eventHandling_);
	assert(!addedToLoop_);
	if (loop_->isInLoopThread())
	{
		assert(!loop_->hasChannel(this));
	}
}

void Channel::tie(const std::shared_ptr<void>& obj)
{
	tie_ = obj;
	tied_ = true;
}

void Channel::update()
{
	addedToLoop_ = true;
	loop_->updateChannel(this);
}

//在调用remove函数之前要确保调用disableAll
void Channel::remove()
{
	assert(isNoneEvent());
	addedToLoop_ = false;
	loop_->removeChannel(this);
}

//当事件到来时会调用handleEvent来处理
void Channel::handleEvent(Timestamp receiveTime)
{
	// if-else是一个生存期的控制，可以把他看成只调用了一个handleEventWithGuard函数
	std::shared_ptr<void> guard;
	if (tied_)
	{
		guard = tie_.lock();
		if (guard)
		{
			handleEventWithGuard(receiveTime);
		}
	}
	else
	{
		handleEventWithGuard(receiveTime);
	}
}


void Channel::handleEventWithGuard(Timestamp receiveTime)
{
	eventHandling_ = true;
	LOG_TRACE << reventsToString();
	// 接下来判断返回的事件是什么类型

	// POLLHUP是对方描述符挂起或关闭，这时我们要关闭fd
	// 收到POLLHUP并且没有收到POLLIN事件
	if ((revents_ & POLLHUP) && !(revents_ & POLLIN))
	{
		if (logHup_)
		{
			LOG_WARN << "fd = " << fd_ << " Channel::handle_event() POLLHUP";
		}
		if (closeCallback_) closeCallback_();
	}

	//无效的文件描述符
	if (revents_ & POLLNVAL)
	{
		LOG_WARN << "fd = " << fd_ << " Channel::handle_event() POLLNVAL";
	}

	//有错误发生
	if (revents_ & (POLLERR | POLLNVAL))
	{
		if (errorCallback_) errorCallback_();
	}
	//POLLRDHUP是对等方关闭了写操作或关闭了半连接，因为套接字是全双工的，POLLRDHUP是指对方关闭了写操作
	// POLLPRI是有紧急数据
	if (revents_ & (POLLIN | POLLPRI | POLLRDHUP))
	{
		if (readCallback_) readCallback_(receiveTime);
	}
	//写操作
	if (revents_ & POLLOUT)
	{
		if (writeCallback_) writeCallback_();
	}
	eventHandling_ = false;
}

string Channel::reventsToString() const
{
	return eventsToString(fd_, revents_);
}

string Channel::eventsToString() const
{
	return eventsToString(fd_, events_);
}

string Channel::eventsToString(int fd, int ev)
{
	std::ostringstream oss;
	oss << fd << ": ";
	if (ev & POLLIN)
		oss << "IN ";
	if (ev & POLLPRI)
		oss << "PRI ";
	if (ev & POLLOUT)
		oss << "OUT ";
	if (ev & POLLHUP)
		oss << "HUP ";
	if (ev & POLLRDHUP)
		oss << "RDHUP ";
	if (ev & POLLERR)
		oss << "ERR ";
	if (ev & POLLNVAL)
		oss << "NVAL ";

	return oss.str();
}
