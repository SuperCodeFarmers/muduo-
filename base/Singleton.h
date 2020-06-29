// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_SINGLETON_H
#define MUDUO_BASE_SINGLETON_H

#include "muduo/base/noncopyable.h"

#include <assert.h>
#include <pthread.h>
#include <stdlib.h> // atexit

namespace muduo
{

namespace detail
{
// This doesn't detect inherited member functions!
// http://stackoverflow.com/questions/1966362/sfinae-to-check-for-inherited-member-functions
template<typename T>
struct has_no_destroy
{
  template <typename C> static char test(decltype(&C::no_destroy));
  template <typename C> static int32_t test(...);
  const static bool value = sizeof(test<T>(0)) == 1;
};
}  // namespace detail

//单例对象
template<typename T>
class Singleton : noncopyable
{
 public:
  Singleton() = delete;
  ~Singleton() = delete;

  //返回单例对象
  static T& instance()
  {
	//保证运行一次init函数，进行创建函数
	//如果多个线程同时调用instance创建对象，有可能不能保证只创建一个对象
	//pthread_once能够保证多个线程同时调用instance，instance只调用一次
	//也可以用锁的方式来保证线程安全
    pthread_once(&ponce_, &Singleton::init);
    assert(value_ != NULL);
    return *value_;
  }

 private:
  static void init()
  {
    value_ = new T();
    if (!detail::has_no_destroy<T>::value)
    {
		//注册一个销毁函数即:对象的析构函数
		//该析构函数会在程序exit时自动调用。atexit()注册的函数类型应为不接受任何参数的void函数。
      ::atexit(destroy);
    }
  }

  static void destroy()
  {
	/*
	在进行delete时，我们要先判断T是一个完整的class类型，
	例如：class A; A *value;delete value;在编译的时候不会出现错误
	为了让编译器能更高效的运行，我们要识别T是否是一个完整类型

	typedef char T_must_be_complete_type[sizeof(T) == 0 ? -1 : 1];
	我们定义一个数组类型，当计算T的大小的时候为0的时候，
						数组为T_must_be_complete_type[-1]，因为数组不能为-1所以在编译的时候会报错
	*/
    typedef char T_must_be_complete_type[sizeof(T) == 0 ? -1 : 1];
    T_must_be_complete_type dummy; (void) dummy;

    delete value_;
    value_ = NULL;
  }

 private:
  //pthread_once_t 能够保证函数只执行一次
  static pthread_once_t ponce_;
  static T*             value_;
};

template<typename T>
pthread_once_t Singleton<T>::ponce_ = PTHREAD_ONCE_INIT;

template<typename T>
T* Singleton<T>::value_ = NULL;

}  // namespace muduo

#endif  // MUDUO_BASE_SINGLETON_H
