// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/base/CountDownLatch.h"

using namespace muduo;

//传递一个计数器进来
CountDownLatch::CountDownLatch(int count)
  : mutex_(),
    condition_(mutex_),
    count_(count)
{
}

void CountDownLatch::wait()
{
	//当计数器大于0时，我们要等待
  MutexLockGuard lock(mutex_);
  while (count_ > 0)
  {
    condition_.wait();
  }
}

void CountDownLatch::countDown()
{
  MutexLockGuard lock(mutex_);
  --count_;
  //如果count_等于0，我们通知所有线程
  if (count_ == 0)
  {
    condition_.notifyAll();
  }
}

int CountDownLatch::getCount() const
{
//返回count_的值，因为可能有多个线程访问该变量
	/*
	由于我们前面声明mutex的时候加上了mutable(可变的)关键字，
	所以在有const修饰的函数中我们可以改变该mutex的状态。	
	*/
  MutexLockGuard lock(mutex_);
  return count_;
}

