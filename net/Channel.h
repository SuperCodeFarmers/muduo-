#ifndef MUDUO_NET_CHANNEL_H
#define MUDUO_NET_CHANNEL_H

#include "muduo/base/noncopyable.h"
#include "muduo/base/Timestamp.h"

#include <functional>
#include <memory>

namespace muduo
{
	namespace net
	{

		class EventLoop;

		///
		/// A selectable I/O channel.
		///
		/// This class doesn't own the file descriptor.
		/// The file descriptor could be a socket,
		/// an eventfd, a timerfd, or a signalfd
		//这个类不拥有文件描述符,文件描述符被Socket类所拥有了
		class Channel : noncopyable
		{
		public:
			// 事件的回调处理
			typedef std::function<void()> EventCallback;
			//读事件回调处理，我们需要多传一个时间戳
			typedef std::function<void(Timestamp)> ReadEventCallback;
			//一个EventLoop可以包含多个Channel，但是一个Channel只能由一个EventLoop负责
			Channel(EventLoop* loop, int fd);
			~Channel();

			// 当事件到来了，会调用handleEvent进行处理
			void handleEvent(Timestamp receiveTime);
			//设置读回调、写回调、关闭回调、出错回调
			void setReadCallback(ReadEventCallback cb)
			{
				readCallback_ = std::move(cb);
			}
			void setWriteCallback(EventCallback cb)
			{
				writeCallback_ = std::move(cb);
			}
			void setCloseCallback(EventCallback cb)
			{
				closeCallback_ = std::move(cb);
			}
			void setErrorCallback(EventCallback cb)
			{
				errorCallback_ = std::move(cb);
			}

			/// Tie this channel to the owner object managed by shared_ptr,
			/// prevent the owner object being destroyed in handleEvent.
			//将这个通道绑定到shared_ptr管理的owner对象，
			//防止在handleEvent中销毁所有者对象。
			void tie(const std::shared_ptr<void>&);

			int fd() const { return fd_; }
			// 返回注册的事件
			int events() const { return events_; }
			//设置返回的事件
			void set_revents(int revt) { revents_ = revt; } // used by pollers
			// int revents() const { return revents_; }
			//判断events是否等于kNoneEvent
			bool isNoneEvent() const { return events_ == kNoneEvent; }

			//关注kReadEvent或者加入kReadEvent事件
			//启动读、关注可读事件
			void enableReading() { events_ |= kReadEvent; update(); }
			//关闭读
			void disableReading() { events_ &= ~kReadEvent; update(); }
			void enableWriting() { events_ |= kWriteEvent; update(); }
			void disableWriting() { events_ &= ~kWriteEvent; update(); }
			//不关注事件了
			void disableAll() { events_ = kNoneEvent; update(); }
			bool isWriting() const { return events_ & kWriteEvent; }
			bool isReading() const { return events_ & kReadEvent; }

			// for Poller
			int index() { return index_; }
			void set_index(int idx) { index_ = idx; }

			// for debug
			// 把这个事件转成字符串以便调试
			string reventsToString() const;
			string eventsToString() const;

			void doNotLogHup() { logHup_ = false; }

			EventLoop* ownerLoop() { return loop_; }
			//移除
			void remove();

		private:
			static string eventsToString(int fd, int ev);

			void update();
			void handleEventWithGuard(Timestamp receiveTime);

			static const int kNoneEvent;//没有事件，默认情况下等于0
			static const int kReadEvent;//读事件.默认为 POLLIN | POLLPRI
			static const int kWriteEvent;//写事件，默认为 POLLOUT

			EventLoop* loop_;		//用于记录它所属的EventLoop
			const int  fd_;		//文件描述符，但不负责关闭该文件描述符
			int        events_;	//关注的事件
			int        revents_;	// poll/epoll返回的事件
			int        index_;	// used by Poller.在poll事件中表示数组中的序号，在epoll事件中表示通道的状态
			bool       logHup_;

			std::weak_ptr<void> tie_;
			bool tied_;
			bool eventHandling_;	//是否处于事件中
			bool addedToLoop_;
			//事件的回调函数
			ReadEventCallback readCallback_;
			EventCallback writeCallback_;
			EventCallback closeCallback_;
			EventCallback errorCallback_;
		};

	}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_CHANNEL_H
