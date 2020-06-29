// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_ASYNCLOGGING_H
#define MUDUO_BASE_ASYNCLOGGING_H

#include "muduo/base/BlockingQueue.h"
#include "muduo/base/BoundedBlockingQueue.h"
#include "muduo/base/CountDownLatch.h"
#include "muduo/base/Mutex.h"
#include "muduo/base/Thread.h"
#include "muduo/base/LogStream.h"

#include <atomic>
#include <vector>

namespace muduo
{

	class AsyncLogging : noncopyable
	{
	public:

		AsyncLogging(const string& basename,
			off_t rollSize,
			int flushInterval = 3);

		~AsyncLogging()
		{
			if (running_)
			{
				stop();
			}
		}

		// 供前端生产者线程调用(日志数据写到缓冲区)
		void append(const char* logline, int len);

		void start()
		{
			running_ = true;
			thread_.start();	// 日志线程
			latch_.wait();
		}

		void stop() NO_THREAD_SAFETY_ANALYSIS
		{
			running_ = false;
			cond_.notify();
			thread_.join();
		}

	private:

		// 供后端消费者线程调用(将数据写到日志文件)
		void threadFunc();

		typedef muduo::detail::FixedBuffer<muduo::detail::kLargeBuffer> Buffer;	// 缓冲区类型，固定大小的缓冲区
		typedef std::vector<std::unique_ptr<Buffer>> BufferVector;	// 缓冲区列表
		typedef BufferVector::value_type BufferPtr;	// 可以理解为Buffer的智能指针，可以管理Buffer的生存期
													// 类似于C++11中的unique_ptr，具备移动语义
													// （两个unique_ptr不能指向一个对象，不能进行复制操作只能进行移动操作）

		const int flushInterval_;	// 超时时间，在flushInterval_秒内，缓冲区没写满，仍将缓冲区中的数据写到文件中
		std::atomic<bool> running_;
		const string basename_;	// 日志文件名
		const off_t rollSize_;	// 日志文件的滚动大小，当日志文件到达一定大小就创建一个新的日志文件
		muduo::Thread thread_;
		muduo::CountDownLatch latch_;	// 用于等待线程启动
		muduo::MutexLock mutex_;
		muduo::Condition cond_ GUARDED_BY(mutex_);
		BufferPtr currentBuffer_ GUARDED_BY(mutex_);	// 当前缓冲区
		BufferPtr nextBuffer_ GUARDED_BY(mutex_);		// 预备缓冲区
		BufferVector buffers_ GUARDED_BY(mutex_);		// 带写入文件的已填满的缓冲区
	};

}  // namespace muduo

#endif  // MUDUO_BASE_ASYNCLOGGING_H
