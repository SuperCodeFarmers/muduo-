// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_THREADLOCALSINGLETON_H
#define MUDUO_BASE_THREADLOCALSINGLETON_H

#include "muduo/base/noncopyable.h"

#include <assert.h>
#include <pthread.h>

//线程本地存储  线程村粗的单例对象实现
//其每个线程都会有一个t_value_
namespace muduo
{

template<typename T>
class ThreadLocalSingleton : noncopyable
{
 public:
  ThreadLocalSingleton() = delete;
  ~ThreadLocalSingleton() = delete;

  //返回对象引用
  static T& instance()
  {
	  //这里不需要使用线程安全的方式来实现，因为每一个线程都有一个自己的t_value_
	  //如果这个指针为空，就创建该对象
    if (!t_value_)
    {
      t_value_ = new T();
      deleter_.set(t_value_);
    }
    return *t_value_;
  }

  //返回对象指针
  static T* pointer()
  {
    return t_value_;
  }

 private:
  static void destructor(void* obj)
  {
    assert(obj == t_value_);
    typedef char T_must_be_complete_type[sizeof(T) == 0 ? -1 : 1];
    T_must_be_complete_type dummy; (void) dummy;
    delete t_value_;
    t_value_ = 0;
  }

  //嵌套类，该嵌套类仅仅是为了能够自动的回调destructor()，销毁t_value_对象
  class Deleter
  {
   public:
    Deleter()
    {
      pthread_key_create(&pkey_, &ThreadLocalSingleton::destructor);
    }

    ~Deleter()
    {
      pthread_key_delete(pkey_);
    }

    void set(T* newObj)
    {
      assert(pthread_getspecific(pkey_) == NULL);
      pthread_setspecific(pkey_, newObj);
    }

    pthread_key_t pkey_;
  };

  //这个指针每一个线程都有一份
  static __thread T* t_value_;
  static Deleter deleter_;
};

template<typename T>
__thread T* ThreadLocalSingleton<T>::t_value_ = 0;

template<typename T>
typename ThreadLocalSingleton<T>::Deleter ThreadLocalSingleton<T>::deleter_;

}  // namespace muduo
#endif  // MUDUO_BASE_THREADLOCALSINGLETON_H
