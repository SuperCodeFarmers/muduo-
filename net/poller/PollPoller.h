#ifndef MUDUO_NET_POLLER_POLLPOLLER_H
#define MUDUO_NET_POLLER_POLLPOLLER_H

#include "muduo/net/Poller.h"

#include <vector>

struct pollfd;

namespace muduo
{
namespace net
{

///
/// IO Multiplexing with poll(2).
///
class PollPoller : public Poller
{
 public:

  PollPoller(EventLoop* loop);
  ~PollPoller() override;
  //activeChannels是Channel返回的活动通道
  Timestamp poll(int timeoutMs, ChannelList* activeChannels) override;
  //从Poller中添加或者更新通道，即通道的注册也是从这里进行注册
  void updateChannel(Channel* channel) override;
  //从Poller中移除通道
  void removeChannel(Channel* channel) override;

 private:
  void fillActiveChannels(int numEvents,
                          ChannelList* activeChannels) const;

  typedef std::vector<struct pollfd> PollFdList;
  PollFdList pollfds_;
};

}  // namespace net
}  // namespace muduo
#endif  // MUDUO_NET_POLLER_POLLPOLLER_H
