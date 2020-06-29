#include "muduo/net/Poller.h"
#include "muduo/net/poller/PollPoller.h"
#include "muduo/net/poller/EPollPoller.h"

#include <stdlib.h>

using namespace muduo::net;

Poller* Poller::newDefaultPoller(EventLoop* loop)
{
//判断是否使用poll，如果使用poll则使用poll模型，否则使用epoll模型
  if (::getenv("MUDUO_USE_POLL"))
  {
    return new PollPoller(loop);
  }
  else
  {
    return new EPollPoller(loop);
  }
}
