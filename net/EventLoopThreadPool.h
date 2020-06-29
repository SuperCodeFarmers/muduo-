
// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_EVENTLOOPTHREADPOOL_H
#define MUDUO_NET_EVENTLOOPTHREADPOOL_H

#include "muduo/base/noncopyable.h"
#include "muduo/base/Types.h"

#include <functional>
#include <memory>
#include <vector>

namespace muduo
{

	namespace net
	{

		class EventLoop;
		class EventLoopThread;

		class EventLoopThreadPool : noncopyable
		{
		public:
			typedef std::function<void(EventLoop*)> ThreadInitCallback;

			EventLoopThreadPool(EventLoop* baseLoop, const string& nameArg);
			~EventLoopThreadPool();
			void setThreadNum(int numThreads) { numThreads_ = numThreads; }
			void start(const ThreadInitCallback& cb = ThreadInitCallback());

			// valid after calling start()
			/// round-robin
			EventLoop* getNextLoop();

			/// with the same hash code, it will always return the same EventLoop
			EventLoop* getLoopForHash(size_t hashCode);

			std::vector<EventLoop*> getAllLoops();

			bool started() const
			{
				return started_;
			}

			const string& name() const
			{
				return name_;
			}

		private:

			EventLoop* baseLoop_;	// 与Acceptor所属的EventLoop相同
			string name_;
			bool started_;		// 是否启动
			int numThreads_;		// 线程数
			int next_;			// 新线程到来，所选择的EventLoop对象下标
			std::vector<std::unique_ptr<EventLoopThread>> threads_;	// IO线程列表
			std::vector<EventLoop*> loops_;							// EventLoop列表
			//threads_所管理的对象都是栈上的对象，当我们释放threads_对象时，它所管理的对象也会自动释放
			//而且它所管理的对象都是unique_ptr是独占的
			//EventLoop都是栈上的对象，所以我们不需要在析构的时候释放它
		};

	}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_EVENTLOOPTHREADPOOL_H
