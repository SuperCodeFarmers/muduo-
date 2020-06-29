// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_BOUNDEDBLOCKINGQUEUE_H
#define MUDUO_BASE_BOUNDEDBLOCKINGQUEUE_H

#include "muduo/base/Condition.h"
#include "muduo/base/Mutex.h"

#include <boost/circular_buffer.hpp>
#include <assert.h>

namespace muduo
{
//阻塞队列
//有界缓冲区
template<typename T>
class BoundedBlockingQueue : noncopyable
{
 public:
  explicit BoundedBlockingQueue(int maxSize)
    : mutex_(),
      notEmpty_(mutex_),
      notFull_(mutex_),
      queue_(maxSize)
  {
  }

  //生产产品
  void put(const T& x)
  {
    MutexLockGuard lock(mutex_);
	//生产的时候我们先判断队列中是否满了，如果满了就等待不满
    while (queue_.full())
    {
      notFull_.wait();
    }
    assert(!queue_.full());
	//生产产品
    queue_.push_back(x);
    notEmpty_.notify();
  }

  void put(T&& x)
  {
    MutexLockGuard lock(mutex_);
    while (queue_.full())
    {
      notFull_.wait();
    }
    assert(!queue_.full());
    queue_.push_back(std::move(x));
    notEmpty_.notify();
  }


  //消费产品
  T take()
  {
    MutexLockGuard lock(mutex_);
	//判断队列中产品数量不为空
    while (queue_.empty())
    {
      notEmpty_.wait();
    }
    assert(!queue_.empty());
	//消费产品
    T front(std::move(queue_.front()));
    queue_.pop_front();
	//一旦我们消费了产品，导致队列不满，我们就要发起一个队列不满的通知
    notFull_.notify();
    return std::move(front);
  }

  bool empty() const
  {
    MutexLockGuard lock(mutex_);
    return queue_.empty();
  }

  bool full() const
  {
    MutexLockGuard lock(mutex_);
    return queue_.full();
  }

  size_t size() const
  {
    MutexLockGuard lock(mutex_);
    return queue_.size();
  }

  size_t capacity() const
  {
    MutexLockGuard lock(mutex_);
    return queue_.capacity();
  }

 private:
  mutable MutexLock          mutex_;
  Condition                  notEmpty_ GUARDED_BY(mutex_);
  Condition                  notFull_ GUARDED_BY(mutex_);
  //为了实现缓冲区是有界限的，我们使用一个环形缓冲区，我们使用boost中的环形缓冲区
  boost::circular_buffer<T>  queue_ GUARDED_BY(mutex_);
};

}  // namespace muduo

#endif  // MUDUO_BASE_BOUNDEDBLOCKINGQUEUE_H
