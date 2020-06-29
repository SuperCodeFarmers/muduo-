// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_CONDITION_H
#define MUDUO_BASE_CONDITION_H

#include "muduo/base/Mutex.h"

#include <pthread.h>

namespace muduo
{
	//条件对象
class Condition : noncopyable
{
 public:
  explicit Condition(MutexLock& mutex)
    : mutex_(mutex)
  {
	  //初始化条件变量
    MCHECK(pthread_cond_init(&pcond_, NULL));
  }

  ~Condition()
  {
	  //销毁条件变量
    MCHECK(pthread_cond_destroy(&pcond_));
  }

  //等待
  void wait()
  {
    MutexLock::UnassignGuard ug(mutex_);
    MCHECK(pthread_cond_wait(&pcond_, mutex_.getPthreadMutex()));
  }

  // returns true if time out, false otherwise.
  //等待一定时间
  bool waitForSeconds(double seconds);

  //通知
  void notify()
  {
    MCHECK(pthread_cond_signal(&pcond_));
  }

  void notifyAll()
  {
    MCHECK(pthread_cond_broadcast(&pcond_));
  }

 private:
  MutexLock& mutex_;//互斥锁对象
  pthread_cond_t pcond_;//条件变量
};

}  // namespace muduo

#endif  // MUDUO_BASE_CONDITION_H
