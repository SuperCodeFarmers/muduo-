// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_CURRENTTHREAD_H
#define MUDUO_BASE_CURRENTTHREAD_H

#include "muduo/base/Types.h"

namespace muduo
{
namespace CurrentThread
{
  // internal
	//__thread修饰的变量是线程局部存储的。
  extern __thread int t_cachedTid;	//线程真实的pid(tid)的缓存，是为了减少syscall(SYS_gettid)系统函数的调用次数，提高获取tid的速度
  extern __thread char t_tidString[32];	//tid的字符串表示形式
  extern __thread int t_tidStringLength;
  extern __thread const char* t_threadName;
  void cacheTid();

  inline int tid()
  {
	  //如果t_cachedTid=0，所以我们还没有缓存过线程id，那么我们就缓存它
    if (__builtin_expect(t_cachedTid == 0, 0))
    {
      cacheTid();
    }
    return t_cachedTid;
  }

  inline const char* tidString() // for logging
  {
    return t_tidString;
  }

  inline int tidStringLength() // for logging
  {
    return t_tidStringLength;
  }

  inline const char* name()
  {
    return t_threadName;
  }

  bool isMainThread();

  void sleepUsec(int64_t usec);  // for testing

  string stackTrace(bool demangle);
}  // namespace CurrentThread
}  // namespace muduo

#endif  // MUDUO_BASE_CURRENTTHREAD_H
