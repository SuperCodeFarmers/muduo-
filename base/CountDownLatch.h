// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_COUNTDOWNLATCH_H
#define MUDUO_BASE_COUNTDOWNLATCH_H

#include "muduo/base/Condition.h"
#include "muduo/base/Mutex.h"

namespace muduo
{
/*
	倒计时锁存器

	既可以用于所有子线程等待主线程发起“起跑”
	也可以用于主线程等待子线程初始化完毕才开始工作
*/
class CountDownLatch : noncopyable
{
 public:

  explicit CountDownLatch(int count);

  //等待
  void wait();

  //计数器减一
  void countDown();

  //获取当前计数器的值
  int getCount() const;

 private:
	 //把mutex设置为可变的，在const函数中，我们才可以对mutex进行加锁和解锁
  mutable MutexLock mutex_;
  Condition condition_ GUARDED_BY(mutex_);
  int count_ GUARDED_BY(mutex_);//计数器
};

}  // namespace muduo
#endif  // MUDUO_BASE_COUNTDOWNLATCH_H
