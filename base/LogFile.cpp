// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/base/LogFile.h"

#include "muduo/base/FileUtil.h"
#include "muduo/base/ProcessInfo.h"

#include <assert.h>
#include <stdio.h>
#include <time.h>

using namespace muduo;

LogFile::LogFile(const string& basename,
                 off_t rollSize,
                 bool threadSafe,
                 int flushInterval,
                 int checkEveryN)
  : basename_(basename),
    rollSize_(rollSize),
    flushInterval_(flushInterval),
    checkEveryN_(checkEveryN),
    count_(0),
	//如果是需要线程安全的，就构造一个互斥锁
    mutex_(threadSafe ? new MutexLock : NULL),
    startOfPeriod_(0),
    lastRoll_(0),
    lastFlush_(0)
{
	//断言判断这个日志文件名字是不包含目录的
  assert(basename.find('/') == string::npos);
  //接下来调用rollFile是为了产生一个日志文件
  rollFile();
}

LogFile::~LogFile() = default;

void LogFile::append(const char* logline, int len)
{
  if (mutex_)
  {
    MutexLockGuard lock(*mutex_);
    append_unlocked(logline, len);
  }
  else
  {
    append_unlocked(logline, len);
  }
}

void LogFile::flush()
{
  if (mutex_)
  {
    MutexLockGuard lock(*mutex_);
    file_->flush();
  }
  else
  {
    file_->flush();
  }
}

void LogFile::append_unlocked(const char* logline, int len)
{
  file_->append(logline, len);

  //每当我们写入文件时，都要判断一下我们写入的数据是否超过最大写入大小
  //如果超出就重新创建一个日志文件
  if (file_->writtenBytes() > rollSize_)
  {
    rollFile();
  }
  else
  {
	//当没有超过的时候我们是否需要rollFile呢，因为我们还有一个条件是时间
	//我们先判断一下count_计数值是否超过了checkEveryN_
	//如果超过了，count_清零，我们再去判断一下当前时间
    ++count_;
    if (count_ >= checkEveryN_)
    {
      count_ = 0;
      time_t now = ::time(NULL);
      time_t thisPeriod_ = now / kRollPerSeconds_ * kRollPerSeconds_;
	  //看一下thisPeriod_是否超过了0点，如果不等说明是第二天的零点
      if (thisPeriod_ != startOfPeriod_)
      {
        rollFile();
      }
	  //我们判断一下是否超过了flush的间隔时间
      else if (now - lastFlush_ > flushInterval_)
      {
        lastFlush_ = now;
        file_->flush();
      }
    }
  }
}

bool LogFile::rollFile()
{
  time_t now = 0;
  //首先获取日志文件名称，并把时间返回回来
  string filename = getLogFileName(basename_, &now);
  //这里先除以kRollPerSeconds_后，乘以kRollPerSeconds_表示
  //对其至kRollPerSeconds_的整数被，也就是时间调整到当天零点
  time_t start = now / kRollPerSeconds_ * kRollPerSeconds_;

  //如果now > lastRoll_我们就要调整一个新的日志文件
  if (now > lastRoll_)
  {
    lastRoll_ = now;
    lastFlush_ = now;
    startOfPeriod_ = start;
    file_.reset(new FileUtil::AppendFile(filename));
    return true;
  }
  return false;
}

string LogFile::getLogFileName(const string& basename, time_t* now)
{
  string filename;
  //先分配basename的空间，再加64字节空间
  filename.reserve(basename.size() + 64);
  filename = basename;

  char timebuf[32];
  struct tm tm;
  *now = time(NULL);
  //线程安全的获取时间
  gmtime_r(now, &tm); // FIXME: localtime_r ?
  //将时间格式化
  strftime(timebuf, sizeof timebuf, ".%Y%m%d-%H%M%S.", &tm);
  filename += timebuf;

  //主机名
  filename += ProcessInfo::hostname();

  char pidbuf[32];
  //进程号
  snprintf(pidbuf, sizeof pidbuf, ".%d", ProcessInfo::pid());
  filename += pidbuf;

  filename += ".log";

  return filename;
}

