// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/base/ThreadPool.h"

#include "muduo/base/Exception.h"

#include <assert.h>
#include <stdio.h>

using namespace muduo;

ThreadPool::ThreadPool(const string& nameArg)
  : mutex_(),
    notEmpty_(mutex_),
    notFull_(mutex_),
    name_(nameArg),
    maxQueueSize_(0),
    running_(false)
{
}

ThreadPool::~ThreadPool()
{
	//如果线程还是在运行状态我们先暂停线程任务
  if (running_)
  {
    stop();
  }
}

//启动线程
void ThreadPool::start(int numThreads)
{
  assert(threads_.empty());
  running_ = true;
  //预先分配numThreads个空间
  threads_.reserve(numThreads);
  for (int i = 0; i < numThreads; ++i)
  {
    char id[32];
    snprintf(id, sizeof id, "%d", i+1);
	//创建线程，线程绑定的函数是runInThread
    threads_.emplace_back(new muduo::Thread(
          std::bind(&ThreadPool::runInThread, this), name_+id));
	//启动线程
    threads_[i]->start();
  }
  if (numThreads == 0 && threadInitCallback_)
  {
    threadInitCallback_();
  }
}

//停止线程池
void ThreadPool::stop()
{
  {
  MutexLockGuard lock(mutex_);
  running_ = false;
  /*
  通知所有的等待线程,runInThread()中的while就会结束阻塞
  */
  notEmpty_.notifyAll();
  }
  for (auto& thr : threads_)
  {
    thr->join();
  }
}

size_t ThreadPool::queueSize() const
{
  MutexLockGuard lock(mutex_);
  return queue_.size();
}
//往线程池中添加任务
void ThreadPool::run(Task task)
{
	//如果线程池为空，我们就直接执行这个任务，不用再进行往线程池中添加的操作了
  if (threads_.empty())
  {
    task();
  }
  else
  {
	/*
	如果线程池不为空，我们要
	第一步：对线程池进行加锁操作
	第二步：判断线程池是否已满，已满就等待线程池不满
	第三步：线程池不满了，就往队列中添加任务
	第四步：通知任务队列中有任务了，用条件变量来发送任务队列不空信号
	*/
    MutexLockGuard lock(mutex_);
    while (isFull())
    {
      notFull_.wait();
    }
    assert(!isFull());

    queue_.push_back(std::move(task));
    notEmpty_.notify();
  }
}

ThreadPool::Task ThreadPool::take()
{
  MutexLockGuard lock(mutex_);
  // always use a while-loop, due to spurious wakeup
  //如果任务队列为空并且处于运行的状态，那么我们要等待任务或者是等待线程退出，当线程退出了runing=false,就会退出
  //等待的条件有两个，任务队列为空或线程池运行结束
  while (queue_.empty() && running_)
  {
    notEmpty_.wait();
  }
  Task task;
  //如果队列不为空，获取任务对象
  if (!queue_.empty())
  {
    task = queue_.front();
    queue_.pop_front();
    if (maxQueueSize_ > 0)
    {
      notFull_.notify();
    }
  }
  return task;
}

bool ThreadPool::isFull() const
{
  mutex_.assertLocked();
  return maxQueueSize_ > 0 && queue_.size() >= maxQueueSize_;
}

//运行线程
void ThreadPool::runInThread()
{
  try
  {
    if (threadInitCallback_)
    {
      threadInitCallback_();
    }
	//线程在运行状态
    while (running_)
    {
	//获取任务对象
      Task task(take());
	//获取对象不为空则运行任务，因为有可能线程中没有任务
      if (task)
      {
		//运行任务
        task();
      }
    }
  }
  catch (const Exception& ex)
  {
    fprintf(stderr, "exception caught in ThreadPool %s\n", name_.c_str());
    fprintf(stderr, "reason: %s\n", ex.what());
    fprintf(stderr, "stack trace: %s\n", ex.stackTrace());
    abort();
  }
  catch (const std::exception& ex)
  {
    fprintf(stderr, "exception caught in ThreadPool %s\n", name_.c_str());
    fprintf(stderr, "reason: %s\n", ex.what());
    abort();
  }
  catch (...)
  {
    fprintf(stderr, "unknown exception caught in ThreadPool %s\n", name_.c_str());
    throw; // rethrow
  }
}

