

#include "muduo/net/poller/PollPoller.h"

#include "muduo/base/Logging.h"
#include "muduo/base/Types.h"
#include "muduo/net/Channel.h"

#include <assert.h>
#include <errno.h>
#include <poll.h>

using namespace muduo;
using namespace muduo::net;

PollPoller::PollPoller(EventLoop* loop)
  : Poller(loop)
{
}

PollPoller::~PollPoller() = default;

Timestamp PollPoller::poll(int timeoutMs, ChannelList* activeChannels)
{
  // XXX pollfds_ shouldn't change
  int numEvents = ::poll(&*pollfds_.begin(), pollfds_.size(), timeoutMs);
  int savedErrno = errno;
  Timestamp now(Timestamp::now());
  if (numEvents > 0)
  {
    LOG_TRACE << numEvents << " events happened";
	//遍历事件，利用activeChannels返回活跃的事件。填充活跃的事件
    fillActiveChannels(numEvents, activeChannels);
  }
  else if (numEvents == 0)
  {
	//poll返回0，说明超时了
    LOG_TRACE << " nothing happened";
  }
  else
  {
    if (savedErrno != EINTR)
    {
      errno = savedErrno;
      LOG_SYSERR << "PollPoller::poll()";
    }
  }
  return now;
}

void PollPoller::fillActiveChannels(int numEvents,
                                    ChannelList* activeChannels) const
{
  for (PollFdList::const_iterator pfd = pollfds_.begin();
      pfd != pollfds_.end() && numEvents > 0; ++pfd)
  {
	  //pfd->revents > 0,说明产生了事件
    if (pfd->revents > 0)
    {
	  //每处理一个事件就减一
      --numEvents;
	  //查找文件描述符所在的通道
      ChannelMap::const_iterator ch = channels_.find(pfd->fd);
      assert(ch != channels_.end());
      Channel* channel = ch->second;
      assert(channel->fd() == pfd->fd);
      channel->set_revents(pfd->revents);
      // pfd->revents = 0;
      activeChannels->push_back(channel);
    }
  }
}

//用于注册或更新通道
void PollPoller::updateChannel(Channel* channel)
{
  Poller::assertInLoopThread();
  LOG_TRACE << "fd = " << channel->fd() << " events = " << channel->events();
  //index<0，说明是一个新的通道
  if (channel->index() < 0)
  {
    // a new one, add to pollfds_
    assert(channels_.find(channel->fd()) == channels_.end());
    struct pollfd pfd;
    pfd.fd = channel->fd();
    pfd.events = static_cast<short>(channel->events());
    pfd.revents = 0;
    pollfds_.push_back(pfd);
    int idx = static_cast<int>(pollfds_.size())-1;
    channel->set_index(idx);
    channels_[pfd.fd] = channel;
  }
  else
  {
    // update existing one
    assert(channels_.find(channel->fd()) != channels_.end());
    assert(channels_[channel->fd()] == channel);
    int idx = channel->index();
    assert(0 <= idx && idx < static_cast<int>(pollfds_.size()));
    struct pollfd& pfd = pollfds_[idx];
    assert(pfd.fd == channel->fd() || pfd.fd == -channel->fd()-1);
    pfd.fd = channel->fd();
    pfd.events = static_cast<short>(channel->events());
    pfd.revents = 0;
	//将一个通道暂时更改为不关注事件，但不从poller中移除该通道
    if (channel->isNoneEvent())
    {
      // ignore this pollfd
	  //暂时忽略该文件描述符的事件
	  //这里可以把pfd.fd直接设置为-1
      pfd.fd = -channel->fd()-1;	//因为0是一个特殊的fd，-0还是0，所以要0减一，这样子设置是为了removeChannel的优化
    }
  }
}

//移除不关注的事件，在移除该通道时，该通道必须要不关注事件了，所以在移除之前一定要先调用updateChannel(),把该fd设置为NoneEvent
void PollPoller::removeChannel(Channel* channel)
{
  Poller::assertInLoopThread();
  LOG_TRACE << "fd = " << channel->fd(); 

  assert(channels_.find(channel->fd()) != channels_.end());//断言判断fd肯定在channels_中
  assert(channels_[channel->fd()] == channel);

  //在我们移除事件时，要先把这个文件描述符的事件给处理完，它没有事件了，我们才能进行移除
  assert(channel->isNoneEvent());

  int idx = channel->index();
  assert(0 <= idx && idx < static_cast<int>(pollfds_.size()));
  const struct pollfd& pfd = pollfds_[idx]; (void)pfd;
  assert(pfd.fd == -channel->fd()-1 && pfd.events == channel->events());
  //移除，我们使用key移除返回的是移除的个数
  size_t n = channels_.erase(channel->fd());
  assert(n == 1); (void)n;
  //判断我们移除的poll是不是在最后一个位置
  if (implicit_cast<size_t>(idx) == pollfds_.size()-1)
  {
	//在最后一个位置则直接pop就行
    pollfds_.pop_back();
  }
  else
  {
	//不在最后一个位置，
	//我们将待删除元素与最后一个元素交换在pop_back
    int channelAtEnd = pollfds_.back().fd;
    iter_swap(pollfds_.begin()+idx, pollfds_.end()-1);
    if (channelAtEnd < 0)
    {
      channelAtEnd = -channelAtEnd-1;
    }
    channels_[channelAtEnd]->set_index(idx);
    pollfds_.pop_back();
  }
}

