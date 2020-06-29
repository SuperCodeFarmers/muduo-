// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_LOGFILE_H
#define MUDUO_BASE_LOGFILE_H

#include "muduo/base/Mutex.h"
#include "muduo/base/Types.h"

#include <memory>

namespace muduo
{

	namespace FileUtil
	{
		class AppendFile;
	}

	class LogFile : noncopyable
	{
	public:
		LogFile(const string& basename,
			off_t rollSize,
			bool threadSafe = true,
			int flushInterval = 3,
			int checkEveryN = 1024);
		~LogFile();

		void append(const char* logline, int len);
		void flush();
		bool rollFile();			//滚动日志

	private:
		//不加锁方式追加日志
		void append_unlocked(const char* logline, int len);

		static string getLogFileName(const string& basename, time_t* now);

		const string basename_;	//日志文件 basename
		const off_t rollSize_;	//日志文件达到rollSize_就换一个新文件
		const int flushInterval_;	//日志写入间隔时间
		const int checkEveryN_;

		int count_;				//计数器初始值等于0，
								  //当他达到checkEveryN_的时候会去检测一下是否需要换一个新的日志文件，或者是否要将日志写入日志文件当中

		std::unique_ptr<MutexLock> mutex_;
		time_t startOfPeriod_;	//开始记录日志的时间(调整至零点)
		time_t lastRoll_;			//上一次滚动日志文件时间
		time_t lastFlush_;		//上一次日志写入文件时间
		std::unique_ptr<FileUtil::AppendFile> file_;

		const static int kRollPerSeconds_ = 60 * 60 * 24;//一天时间
	};

}  // namespace muduo
#endif  // MUDUO_BASE_LOGFILE_H
