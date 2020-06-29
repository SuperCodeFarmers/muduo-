#include "muduo/net/Buffer.h"
#include "muduo/net/http/HttpContext.h"

using namespace muduo;
using namespace muduo::net;

//解析请求行
bool HttpContext::processRequestLine(const char* begin, const char* end)
{
	bool succeed = false;
	const char* start = begin;
	//先查找空格的位置
	const char* space = std::find(start, end, ' ');

	//请求行格式：GET / HTTP/1.1,先找出第一个空格之前的GET
	if (space != end && request_.setMethod(start, space))//解析请求方法
	{
		//调到路径的位置,即:/
		start = space + 1;
		//解析出路径
		space = std::find(start, end, ' ');
		if (space != end)
		{
			const char* question = std::find(start, space, '?');
			if (question != space)
			{
				request_.setPath(start, question);
				request_.setQuery(question, space);
			}
			else
			{
				request_.setPath(start, space);
			}
			//再跳过一个空格
			start = space + 1;
			//查找到HTTP/1.版本
			succeed = end - start == 8 && std::equal(start, end - 1, "HTTP/1.");
			if (succeed)
			{
				if (*(end - 1) == '1')
				{
					request_.setVersion(HttpRequest::kHttp11);//HTTP 1.1
				}
				else if (*(end - 1) == '0')
				{
					request_.setVersion(HttpRequest::kHttp10);//HTTP 1.0
				}
				else
				{
					succeed = false;
				}
			}
		}
	}
	return succeed;
}

//解析请求
bool HttpContext::parseRequest(Buffer* buf, Timestamp receiveTime)
{
	//设置开始解析标识
	bool ok = true;
	bool hasMore = true;

	while (hasMore)
	{
		//初始状态应该处于请求行的状态
		if (state_ == kExpectRequestLine)
		{
			//先查找\r\n,这一行的数据都放到buf中
			const char* crlf = buf->findCRLF();
			if (crlf)
			{
				//解析请求行			//start     end
				ok = processRequestLine(buf->peek(), crlf);

				if (ok)//请求行解析成功
				{
					//设置请求时间
					request_.setReceiveTime(receiveTime);
					//将请求行冲buf中取回，包括\r\n
					buf->retrieveUntil(crlf + 2);
					state_ = kExpectHeaders;//将HTTP状态改为kExpectHeaders，接下来期望接收首部字段

					//因为我们还有更多的信息没有解析，不应该更改hasMore的状态

				}
				else
				{
					hasMore = false;
				}
			}
			else
			{
				hasMore = false;
			}
		}
		else if (state_ == kExpectHeaders)//解析header，即首部
		{
			//把一行一行的首部取出，然后查找‘:’key-value
			const char* crlf = buf->findCRLF();
			if (crlf)
			{
				const char* colon = std::find(buf->peek(), crlf, ':');
				if (colon != crlf)
				{
					request_.addHeader(buf->peek(), colon, crlf);
				}
				else
				{
					//到达空行，说明请求首部字段已经处理完毕
					  //这时请求已经解析完毕，把状态改为kGotAll，并把hasMore置为false，结束解析循环
					state_ = kGotAll;
					hasMore = false;
				}
				buf->retrieveUntil(crlf + 2);//将header从buf中取回，包括\r\n
			}
			else
			{
				hasMore = false;
			}
		}
		else if (state_ == kExpectBody)
		{
			//因为body的内容复杂不定，所以不能具体实现
		}
	}
	return ok;
}
