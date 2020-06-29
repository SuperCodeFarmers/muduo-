#include "muduo/net/EventLoopThread.h"
#include "muduo/net/EventLoop.h"
#include "muduo/base/Thread.h"
#include "muduo/base/CountDownLatch.h"

#include <stdio.h>
#include <unistd.h>

using namespace muduo;
using namespace muduo::net;

void print(EventLoop* p = NULL)
{
  printf("print: pid = %d, tid = %d, loop = %p\n",
         getpid(), CurrentThread::tid(), p);
}

void quit(EventLoop* p)
{
  print(p);
  p->quit();
}

int main()
{
  print();

  {
  EventLoopThread thr1;  // never start
  }

  {
  // dtor calls quit()
  EventLoopThread thr2;//创建一个IO线程对象
  EventLoop* loop = thr2.startLoop();//调用startLoop才创建了一个IO线程，loop指向了一个栈上的对象
  
  //异步调用runInLoop，即将runInThread添加到loop对象所在的IO线程中，让该IO线程执行
  loop->runInLoop(std::bind(print, loop));
  CurrentThread::sleepUsec(500 * 1000);
  }

  {
  // quit() before dtor
  EventLoopThread thr3;
  EventLoop* loop = thr3.startLoop();
  loop->runInLoop(std::bind(quit, loop));
  CurrentThread::sleepUsec(500 * 1000);
  }
}

