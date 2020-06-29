// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_BLOCKINGQUEUE_H
#define MUDUO_BASE_BLOCKINGQUEUE_H

#include "muduo/base/Condition.h"
#include "muduo/base/Mutex.h"

#include <deque>
#include <assert.h>

/*
队列相当于一个生产者消费者模型
在BlockingQueue中使用条件变量实现，是无界的缓冲区即没有容量限制
*/

namespace muduo
{
//阻塞队列
//无界缓冲区
template<typename T>
class BlockingQueue : noncopyable
{
 public:
  BlockingQueue()
    : mutex_(),
      notEmpty_(mutex_),
      queue_()
  {
  }

  //生产产品
  void put(const T& x)
  {
	//生产产品时，我们要对产品队列进行保护
    MutexLockGuard lock(mutex_);
    queue_.push_back(x);
    notEmpty_.notify(); // wait morphing saves us
    // http://www.domaigne.com/blog/computing/condvars-signal-with-mutex-locked-or-not/
  }
    
  void put(T&& x)
  {
    MutexLockGuard lock(mutex_);
    queue_.push_back(std::move(x));
    notEmpty_.notify();
  }

  //消费产品
  T take()
  {
    MutexLockGuard lock(mutex_);
    // always use a while-loop, due to spurious wakeup
	//如果队列为空，则等待队列不为空
    while (queue_.empty())
    {
      notEmpty_.wait();
    }
    assert(!queue_.empty());
	//把队列中的第一个元素移出来
    T front(std::move(queue_.front()));
    queue_.pop_front();
    return std::move(front);
  }

  size_t size() const
  {
    MutexLockGuard lock(mutex_);
    return queue_.size();
  }

 private:
  mutable MutexLock mutex_;
  //使用条件变量实现队列
  Condition         notEmpty_ GUARDED_BY(mutex_);//条件变量
  //队列直接使用STL中的deque队列
  std::deque<T>     queue_ GUARDED_BY(mutex_);
};

}  // namespace muduo

#endif  // MUDUO_BASE_BLOCKINGQUEUE_H
