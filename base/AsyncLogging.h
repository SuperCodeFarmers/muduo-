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

		// ��ǰ���������̵߳���(��־����д��������)
		void append(const char* logline, int len);

		void start()
		{
			running_ = true;
			thread_.start();	// ��־�߳�
			latch_.wait();
		}

		void stop() NO_THREAD_SAFETY_ANALYSIS
		{
			running_ = false;
			cond_.notify();
			thread_.join();
		}

	private:

		// ������������̵߳���(������д����־�ļ�)
		void threadFunc();

		typedef muduo::detail::FixedBuffer<muduo::detail::kLargeBuffer> Buffer;	// ���������ͣ��̶���С�Ļ�����
		typedef std::vector<std::unique_ptr<Buffer>> BufferVector;	// �������б�
		typedef BufferVector::value_type BufferPtr;	// �������ΪBuffer������ָ�룬���Թ���Buffer��������
													// ������C++11�е�unique_ptr���߱��ƶ�����
													// ������unique_ptr����ָ��һ�����󣬲��ܽ��и��Ʋ���ֻ�ܽ����ƶ�������

		const int flushInterval_;	// ��ʱʱ�䣬��flushInterval_���ڣ�������ûд�����Խ��������е�����д���ļ���
		std::atomic<bool> running_;
		const string basename_;	// ��־�ļ���
		const off_t rollSize_;	// ��־�ļ��Ĺ�����С������־�ļ�����һ����С�ʹ���һ���µ���־�ļ�
		muduo::Thread thread_;
		muduo::CountDownLatch latch_;	// ���ڵȴ��߳�����
		muduo::MutexLock mutex_;
		muduo::Condition cond_ GUARDED_BY(mutex_);
		BufferPtr currentBuffer_ GUARDED_BY(mutex_);	// ��ǰ������
		BufferPtr nextBuffer_ GUARDED_BY(mutex_);		// Ԥ��������
		BufferVector buffers_ GUARDED_BY(mutex_);		// ��д���ļ����������Ļ�����
	};

}  // namespace muduo

#endif  // MUDUO_BASE_ASYNCLOGGING_H
