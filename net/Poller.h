#ifndef MUDUO_NET_POLLER_H
#define MUDUO_NET_POLLER_H

#include <map>
#include <vector>

#include "muduo/base/Timestamp.h"
#include "muduo/net/EventLoop.h"

namespace muduo
{
	namespace net
	{

		class Channel;

		///
		/// Base class for IO Multiplexing
		///
		/// This class doesn't own the Channel objects.
		//轮询器
		class Poller : noncopyable
		{
		public:
			typedef std::vector<Channel*> ChannelList;//channe返回的活动通道

			Poller(EventLoop* loop);
			virtual ~Poller();

			/// Polls the I/O events.
			/// Must be called in the loop thread.
			virtual Timestamp poll(int timeoutMs, ChannelList* activeChannels) = 0;

			/// Changes the interested I/O events.
			/// Must be called in the loop thread.
			virtual void updateChannel(Channel* channel) = 0;

			/// Remove the channel, when it destructs.
			/// Must be called in the loop thread.
			virtual void removeChannel(Channel* channel) = 0;

			virtual bool hasChannel(Channel* channel) const;

			static Poller* newDefaultPoller(EventLoop* loop);

			void assertInLoopThread() const
			{
				ownerLoop_->assertInLoopThread();
			}

		protected:
			typedef std::map<int, Channel*> ChannelMap;
			//用于存储fd和fd相对于的Channel
			ChannelMap channels_;

		private:
			EventLoop* ownerLoop_;	// Poller所属的EventLoop
		};

	}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_POLLER_H
