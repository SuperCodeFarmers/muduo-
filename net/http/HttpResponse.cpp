#include "muduo/net/http/HttpResponse.h"
#include "muduo/net/Buffer.h"

#include <stdio.h>

using namespace muduo;
using namespace muduo::net;

void HttpResponse::appendToBuffer(Buffer* output) const
{
  char buf[32];
  //先添加响应头
  snprintf(buf, sizeof buf, "HTTP/1.1 %d ", statusCode_);
  output->append(buf);
  //添加响应信息，例如 200 OK
  output->append(statusMessage_);
  //HTTP每行都以\r\n结尾
  output->append("\r\n");

  //如果closeConnection为true
  if (closeConnection_)
  {
	  //如果是短链接，不需要告诉浏览器Content-Length(实体长度),浏览器也能正常处理
	  /*
	  HTTP/1.1 版本的默认连接都是持久连接。
	  当服务器端想明确断开连接时，
	  则指定 Connection 首部字段的值为 close。
	  */
    output->append("Connection: close\r\n");
  }
  else
  {
	  //长连接，要把实体长度告诉浏览器
    snprintf(buf, sizeof buf, "Content-Length: %zd\r\n", body_.size());
    output->append(buf);
	//Connection字段设置为长连接
    output->append("Connection: Keep-Alive\r\n");
  }

  //把相应头部字段都添加进去
  for (const auto& header : headers_)
  {
	//HTTP首部字段是由`首部字段名`和`字段值`构成的，中间用“：”分割。
	//头部名称字段
    output->append(header.first);
    output->append(": ");
	//头部值
    output->append(header.second);
	//每行都以\r\n结尾
    output->append("\r\n");
  }

  //头部与实体之间也需要有一个空行
  output->append("\r\n");
  //把实体加入
  output->append(body_);
}
