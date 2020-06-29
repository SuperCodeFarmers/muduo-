// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/base/AsyncLogging.h"
#include "muduo/base/LogFile.h"
#include "muduo/base/Timestamp.h"

#include <stdio.h>

using namespace muduo;

AsyncLogging::AsyncLogging(const string& basename,
	off_t rollSize,
	int flushInterval)
	: flushInterval_(flushInterval),
	running_(false),
	basename_(basename),
	rollSize_(rollSize),
	thread_(std::bind(&AsyncLogging::threadFunc, this), "Logging"),
	latch_(1),
	mutex_(),
	cond_(mutex_),
	currentBuffer_(new Buffer),
	nextBuffer_(new Buffer),
	buffers_()
{
	currentBuffer_->bzero();
	nextBuffer_->bzero();
	buffers_.reserve(16);
}

// ��ǰ���������̵߳���(��־����д��������)
void AsyncLogging::append(const char* logline, int len)
{
	// ��Ϊ���ж���߳�����������д���ݣ�����Ҫʹ�û��������б���
	muduo::MutexLockGuard lock(mutex_);
	if (currentBuffer_->avail() > len)
	{
		// ��ǰ������δ����������׷�ӵ�ĩβ
		currentBuffer_->append(logline, len);
	}
	else
	{
		// ��ǰ����������������ǰ��������ӵ���д���ļ����������Ļ������б�
		buffers_.push_back(std::move(currentBuffer_));

		// ����ǰ����������ΪԤ��������
		if (nextBuffer_)
		{
			currentBuffer_ = std::move(nextBuffer_);// ʹ���ƶ�����
		}
		else
		{
			// ����������ٷ�����ǰ��д��̫�죬һ���Ӱ����黺������д����
			// ��ôֻ�÷���һ���µĻ�����
			currentBuffer_.reset(new Buffer); // Rarely happens
		}
		currentBuffer_->append(logline, len);
		cond_.notify();	// ֪ͨ��˿�ʼд����־��������д���˲�֪ͨ��˿�ʼд��־�������Ӻ�˲���Ƶ���Ľ���IO����
	}
}

// ��˵���
void AsyncLogging::threadFunc()
{
	assert(running_ == true);
	latch_.countDown();
	LogFile output(basename_, rollSize_, false);

	// ׼�����黺����
	BufferPtr newBuffer1(new Buffer);
	BufferPtr newBuffer2(new Buffer);
	newBuffer1->bzero();
	newBuffer2->bzero();
	BufferVector buffersToWrite;
	buffersToWrite.reserve(16); // Ԥ��16�ֽڿռ�
	while (running_)
	{
		assert(newBuffer1 && newBuffer1->length() == 0);
		assert(newBuffer2 && newBuffer2->length() == 0);
		assert(buffersToWrite.empty());

		{
			muduo::MutexLockGuard lock(mutex_);
			/*
			������������һ��ʹ��while��������if
			��Ϊif�޷������ٻ��ѡ�waitForSeconds���ܻᱻ�źŴ���ˣ���û������buffers_Ϊ�գ�ֻ�Ǳ��ź������
			Ҳ�����������������㣬������ȻҪ�ж����������Ƿ����㣬��ʹ��whileѭ�����д���
			*/
			
			if (buffers_.empty())  // unusual usage!��ע�⣬������һ���ǳ����÷���
			{
				// buffers_Ϊ�յ�ʱ��Ž��еȴ�
				cond_.waitForSeconds(flushInterval_);	// �ȴ�ǰ��д��һ�����߶��buffer������һ����ʱʱ�䵽��
			}
			buffers_.push_back(std::move(currentBuffer_));	// ����ǰ������һ��buffers_
			currentBuffer_ = std::move(newBuffer1); // �����е�newBuffer1��Ϊ��ǰ������
			buffersToWrite.swap(buffers_);	//����ֻ�ǽ�����ָ�룬���������ݵĿ��������ԱȽϿ�
											// buffers_��buffersToWrite��������������Ĵ���������ٽ���֮�ⰲȫ�ķ���buffersToWrite
			if (!nextBuffer_)
			{
				nextBuffer_ = std::move(newBuffer2);	// ȷ��ǰ��ʼ����һ��Ԥ��buffer�ɹ�����
														// �������Լ���ǰ���ٽ��������ڴ�ĸ��ʣ�����ǰ���ٽ�������
			}
		}

		assert(!buffersToWrite.empty());


		// ��Ϣ�ѻ�
		// ǰ��������ѭ����ƴ��������־��Ϣ��������˵Ĵ�������������ǵ��͵������ٶ�
		// ���������ٶ����⣬������������ڴ��жѻ�������ʱ������������(�����ڴ治��)
		// ���߳������(�����ڴ�ʧ��)

		// ��������ĸ������Ϊ25������������ˣ���ֻ����������������Ѷ����ɾ�����Ա�֤ϵͳ���㹻���ڴ�
		if (buffersToWrite.size() > 25)
		{
			char buf[256];
			snprintf(buf, sizeof buf, "Dropped log messages at %s, %zd larger buffers\n",
				Timestamp::now().toFormattedString().c_str(),
				buffersToWrite.size() - 2);
			fputs(buf, stderr);
			output.append(buf, static_cast<int>(strlen(buf)));
			buffersToWrite.erase(buffersToWrite.begin() + 2, buffersToWrite.end()); // ����������־�����ڳ��ڴ棬���������黺����
		}

		for (const auto& buffer : buffersToWrite)
		{
			// FIXME: use unbuffered stdio FILE ? or use ::writev ?
			// д���ļ�����
			output.append(buffer->data(), buffer->length());
		}

		// д���Ժ����Ǿ��ó����黺������
		if (buffersToWrite.size() > 2)
		{
			// drop non-bzero-ed buffers, avoid trashing
			buffersToWrite.resize(2); // ����������buffer,����newBuffer1��newBuffer2
		}

		if (!newBuffer1)
		{
			assert(!buffersToWrite.empty());
			newBuffer1 = std::move(buffersToWrite.back());
			buffersToWrite.pop_back();
			newBuffer1->reset();
		}

		if (!newBuffer2)
		{
			assert(!buffersToWrite.empty());
			newBuffer2 = std::move(buffersToWrite.back());
			buffersToWrite.pop_back();
			newBuffer2->reset();
		}

		// ����Ļ������鶼���ͷŵ�
		buffersToWrite.clear();
		output.flush();
	}
	output.flush();
}

