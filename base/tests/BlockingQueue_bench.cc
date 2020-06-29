#include "muduo/base/BlockingQueue.h"
#include "muduo/base/CountDownLatch.h"
#include "muduo/base/Thread.h"
#include "muduo/base/Timestamp.h"

#include <map>
#include <string>
#include <vector>
#include <stdio.h>
#include <unistd.h>

class Bench
{
 public:
  Bench(int numThreads)
    : latch_(numThreads)
  {
	  //创建线程并启动
    threads_.reserve(numThreads);
    for (int i = 0; i < numThreads; ++i)
    {
      char name[32];
      snprintf(name, sizeof name, "work thread %d", i);
      threads_.emplace_back(new muduo::Thread(
            std::bind(&Bench::threadFunc, this), muduo::string(name)));
    }
    for (auto& thr : threads_)
    {
      thr->start();
    }
  }

  //run用来生产产品，生产的产品就是当前时间，threadFunc用来消费产品
  void run(int times)
  {
    printf("waiting for count down latch\n");
    latch_.wait();
    printf("all threads started\n");
    for (int i = 0; i < times; ++i)
    {
      muduo::Timestamp now(muduo::Timestamp::now());
      queue_.put(now);
	  //生产的产品的速度是1000微妙一次
      usleep(1000);
    }
  }

  void joinAll()
  {
    for (size_t i = 0; i < threads_.size(); ++i)
    {
      queue_.put(muduo::Timestamp::invalid());
    }

    for (auto& thr : threads_)
    {
      thr->join();
    }
  }

 private:
	 //消费产品
  void threadFunc()
  {
    printf("tid=%d, %s started\n",
           muduo::CurrentThread::tid(),
           muduo::CurrentThread::name());

	//map存的是一个延迟，统计重生产到消费之间所用的时间相同的个数
    std::map<int, int> delays;
    latch_.countDown();
    bool running = true;
    while (running)
    {
      muduo::Timestamp t(queue_.take());
      muduo::Timestamp now(muduo::Timestamp::now());
	  //判断时间t是否合法
      if (t.valid())
      {
		  //delay是当前时间于t的事件差，乘1000000是为了转成秒
        int delay = static_cast<int>(timeDifference(now, t) * 1000000);
        // printf("tid=%d, latency = %d us\n",
        //        muduo::CurrentThread::tid(), delay);
        ++delays[delay];
      }
	  //当t中的时间是一个非法的时间时，我们才跳出循环
      running = t.valid();
    }

    printf("tid=%d, %s stopped\n",
           muduo::CurrentThread::tid(),
           muduo::CurrentThread::name());
	//遍历map容器
    for (const auto& delay : delays)
    {
      printf("tid = %d, delay = %d, count = %d\n",
             muduo::CurrentThread::tid(),
             delay.first, delay.second);
    }
  }

  muduo::BlockingQueue<muduo::Timestamp> queue_;
  muduo::CountDownLatch latch_;
  std::vector<std::unique_ptr<muduo::Thread>> threads_;
};

int main(int argc, char* argv[])
{
  int threads = argc > 1 ? atoi(argv[1]) : 1;
  //创建线程
  Bench t(threads);
  t.run(10000);
  t.joinAll();
}
