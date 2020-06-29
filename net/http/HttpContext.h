#ifndef MUDUO_NET_HTTP_HTTPCONTEXT_H
#define MUDUO_NET_HTTP_HTTPCONTEXT_H

#include "muduo/base/copyable.h"

#include "muduo/net/http/HttpRequest.h"

namespace muduo
{
	namespace net
	{

		class Buffer;

		//HTTP协议解析类
		class HttpContext : public muduo::copyable
		{
		public:
			enum HttpRequestParseState
			{
				kExpectRequestLine,	//处于解析请求行状态
				kExpectHeaders,		//处于解析首部字段状态
				kExpectBody,		//处于解析实体状态
				kGotAll,			//全部解析完毕
			};

			HttpContext()
				: state_(kExpectRequestLine)
			{
				//构造函数初始化状态为：kExpectRequestLine
				//期望收到的是一个请求行
			}

			// default copy-ctor, dtor and assignment are fine

			//解析请求
			bool parseRequest(Buffer* buf, Timestamp receiveTime);

			bool gotAll() const
			{
				return state_ == kGotAll;
			}

			//重置为初始状态
			void reset()
			{
				state_ = kExpectRequestLine;
				//并且把当前对象给置空
				HttpRequest dummy;
				request_.swap(dummy);
			}

			//获取http请求
			const HttpRequest& request() const
			{
				return request_;
			}

			HttpRequest& request()
			{
				return request_;
			}

		private:
			bool processRequestLine(const char* begin, const char* end);

			HttpRequestParseState state_;	//请求解析状态
			HttpRequest request_;			//http请求
		};

	}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_HTTP_HTTPCONTEXT_H
