// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/base/Thread.h"
#include "muduo/base/CurrentThread.h"
#include "muduo/base/Exception.h"
#include "muduo/base/Logging.h"

#include <type_traits>

#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <linux/unistd.h>

namespace muduo
{
namespace detail
{

pid_t gettid()
{
	//使用系统调用来获取
  return static_cast<pid_t>(::syscall(SYS_gettid));
}

void afterFork()
{
	//把线程id缓存等于0，然后设置线程名称为main，再重新缓存线程id
  muduo::CurrentThread::t_cachedTid = 0;
  muduo::CurrentThread::t_threadName = "main";
  CurrentThread::tid();
  // no need to call pthread_atfork(NULL, NULL, &afterFork);
}

class ThreadNameInitializer
{
 public:
  ThreadNameInitializer()
  {
	  //将线程名称等于main
    muduo::CurrentThread::t_threadName = "main";
	//缓存当前线程的tid
    CurrentThread::tid();
	//如果我们调用了fork函数，成功后，子进程会调用afterFork
	//fork可能是在主线程中调用，也可能再子线程中调用
	//fork得到一个新进程，新进程只有一个执行序列，只有一个线程（调用fork的线程被继承下来）
	//新线程应该是主线程，所以线程id要改一下，线程名称要改变一下
    pthread_atfork(NULL, NULL, &afterFork);
  }
};
//init对象的构造先于main函数，它在一开始就构造了
ThreadNameInitializer init;

struct ThreadData
{
  typedef muduo::Thread::ThreadFunc ThreadFunc;
  ThreadFunc .func_;
  string name_;
  pid_t* tid_;
  CountDownLatch* latch_;

  ThreadData(ThreadFunc func,
             const string& name,
             pid_t* tid,
             CountDownLatch* latch)
    : func_(std::move(func)),
      name_(name),
      tid_(tid),
      latch_(latch)
  { }

  void runInThread()
  {
	  //获取当前线程的tid
    *tid_ = muduo::CurrentThread::tid();
    tid_ = NULL;
    latch_->countDown();
    latch_ = NULL;

	//缓存线程的名字
    muduo::CurrentThread::t_threadName = name_.empty() ? "muduoThread" : name_.c_str();
    ::prctl(PR_SET_NAME, muduo::CurrentThread::t_threadName);
    try
    {
		//调用回调函数
      func_();
      muduo::CurrentThread::t_threadName = "finished";
    }
    catch (const Exception& ex)
    {
      muduo::CurrentThread::t_threadName = "crashed";
      fprintf(stderr, "exception caught in Thread %s\n", name_.c_str());
      fprintf(stderr, "reason: %s\n", ex.what());
      fprintf(stderr, "stack trace: %s\n", ex.stackTrace());
      abort();
    }
    catch (const std::exception& ex)
    {
      muduo::CurrentThread::t_threadName = "crashed";
      fprintf(stderr, "exception caught in Thread %s\n", name_.c_str());
      fprintf(stderr, "reason: %s\n", ex.what());
      abort();
    }
    catch (...)
    {
      muduo::CurrentThread::t_threadName = "crashed";
      fprintf(stderr, "unknown exception caught in Thread %s\n", name_.c_str());
      throw; // rethrow
    }
  }
};

//所启动的线程的入口函数
void* startThread(void* obj)
{
	//第三步：
	//把void*参数进行强制类型转换，转成我们需要的类型
  ThreadData* data = static_cast<ThreadData*>(obj);
  //第四步：运行真正的函数
  data->runInThread();
  delete data;
  return NULL;
}

}  // namespace detail






void CurrentThread::cacheTid()
{
  if (t_cachedTid == 0)
  {
	  //获取线程id，调用detail名称空间中的gettid
	  //得到线程的真实pid，把他缓存到t_cachedTid，并把它格式化到t_tidString
    t_cachedTid = detail::gettid();
	//t_tidStringLength的长度为6，"%5d "：中还有一个空格
    t_tidStringLength = snprintf(t_tidString, sizeof t_tidString, "%5d ", t_cachedTid);
  }
}

//判断当前线程是否是主线程
bool CurrentThread::isMainThread()
{
	//查看一下tid是否等于当前进程id，如果等于当前进行id，他就是主线程
  return tid() == ::getpid();
}

void CurrentThread::sleepUsec(int64_t usec)
{
  struct timespec ts = { 0, 0 };
  ts.tv_sec = static_cast<time_t>(usec / Timestamp::kMicroSecondsPerSecond);
  ts.tv_nsec = static_cast<long>(usec % Timestamp::kMicroSecondsPerSecond * 1000);
  ::nanosleep(&ts, NULL);
}

AtomicInt32 Thread::numCreated_;

Thread::Thread(ThreadFunc func, const string& n)
  : started_(false),
    joined_(false),
    pthreadId_(0),
    tid_(0),
    func_(std::move(func)),
    name_(n),
    latch_(1)
{
  setDefaultName();
}

Thread::~Thread()
{
  if (started_ && !joined_)
  {
    pthread_detach(pthreadId_);
  }
}

void Thread::setDefaultName()
{
	//创建的线程个数加一，这是使用原子性操作进行加一
  int num = numCreated_.incrementAndGet();
  if (name_.empty())
  {
    char buf[32];
    snprintf(buf, sizeof buf, "Thread%d", num);
    name_ = buf;
  }
}

//第一步：启动线程
void Thread::start()
{
  assert(!started_);
  started_ = true;
  // FIXME: move(func_)
  //data是线程所需要的参数，以指针方式传入
  detail::ThreadData* data = new detail::ThreadData(func_, name_, &tid_, &latch_);
  //第二步：使用系统语句，创建线程，startThread是线程入口，data是线程所需的参数
  if (pthread_create(&pthreadId_, NULL, &detail::startThread, data))
  {
    started_ = false;
    delete data; // or no delete?
    LOG_SYSFATAL << "Failed in pthread_create";
  }
  else
  {
    latch_.wait();
    assert(tid_ > 0);
  }
}

//关闭线程
int Thread::join()
{
  assert(started_);
  assert(!joined_);
  joined_ = true;
  return pthread_join(pthreadId_, NULL);
}

}  // namespace muduo
