// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_THREADPOOL_H
#define MUDUO_BASE_THREADPOOL_H

#include "muduo/base/Condition.h"
#include "muduo/base/Mutex.h"
#include "muduo/base/Thread.h"
#include "muduo/base/Types.h"

#include <deque>
#include <vector>

namespace muduo
{
//该线程池的个数是固定的
class ThreadPool : noncopyable
{
 public:
  typedef std::function<void ()> Task;//这个函数用来执行任务

  explicit ThreadPool(const string& nameArg = string("ThreadPool"));
  ~ThreadPool();

  // Must be called before start().
  //必须先调用start().
  //设置队列大小
  void setMaxQueueSize(int maxSize) { maxQueueSize_ = maxSize; }
  void setThreadInitCallback(const Task& cb)
  { threadInitCallback_ = cb; }

  //启动线程池，线程池个数是固定的
  void start(int numThreads);
  //关闭线程
  void stop();

  const string& name() const
  { return name_; }

  size_t queueSize() const;

  // Could block if maxQueueSize > 0
  // There is no move-only version of std::function in C++ as of C++14.
  // So we don't need to overload a const& and an && versions
  // as we do in (Bounded)BlockingQueue.
  // https://stackoverflow.com/a/25408989
  
  //往线程池中添加任务
  void run(Task f);

 private:
  bool isFull() const REQUIRES(mutex_);
  void runInThread();
  Task take();//获取任务

  
  mutable MutexLock mutex_;//可变的锁
  Condition notEmpty_ GUARDED_BY(mutex_);
  Condition notFull_ GUARDED_BY(mutex_);
  
  string name_;//线程名称
  Task threadInitCallback_;
  std::vector<std::unique_ptr<muduo::Thread>> threads_;//线程队列
  std::deque<Task> queue_ GUARDED_BY(mutex_);//任务队列
  size_t maxQueueSize_;
  bool running_;//标识线程是否处于运行的状态
};

}  // namespace muduo

#endif  // MUDUO_BASE_THREADPOOL_H
