#ifndef MUDUO_NET_HTTP_HTTPRESPONSE_H
#define MUDUO_NET_HTTP_HTTPRESPONSE_H

#include "muduo/base/copyable.h"
#include "muduo/base/Types.h"

#include <map>

namespace muduo
{
namespace net
{

class Buffer;

//HTTP响应类封装
class HttpResponse : public muduo::copyable
{
 public:
	 //HTTP的响应状态码
  enum HttpStatusCode
  {
    kUnknown,
    k200Ok = 200,//成功
    k301MovedPermanently = 301,//永久性重定向，请求的页面转移到另外一个地址
    k400BadRequest = 400,//错误的请求，语法格式有错，服务器无法处理此请求
    k404NotFound = 404,//请求的页面不存在
  };

  //构造函数
  explicit HttpResponse(bool close)
    : statusCode_(kUnknown),
      closeConnection_(close)
  {
	  //构造函数初始化：statusCode_、closeConnection_
  }

  //设置状态码
  void setStatusCode(HttpStatusCode code)
  { statusCode_ = code; }

  //设置状态文本消息
  void setStatusMessage(const string& message)
  { statusMessage_ = message; }

  //设置关闭连接
  void setCloseConnection(bool on)
  { closeConnection_ = on; }

  //获取是否关闭连接状态
  bool closeConnection() const
  { return closeConnection_; }

  //设置实体主体的文档媒体类型
  void setContentType(const string& contentType)
  { addHeader("Content-Type", contentType); }

  // FIXME: replace string with StringPiece
  //添加首部字段
  void addHeader(const string& key, const string& value)
  { headers_[key] = value; }

  //报文主体，数据
  void setBody(const string& body)
  { body_ = body; }

  //将HttpResponse对象中的数据转成字符串，以方便发送给客户端
  //即把数据进行打包处理
  void appendToBuffer(Buffer* output) const;

 private:
  std::map<string, string> headers_;//header列表
  HttpStatusCode statusCode_;		//状态响应码
  // FIXME: add http version
  string statusMessage_;			//状态响应码对应的文本呢信息
  bool closeConnection_;			//是否关闭连接
  string body_;						//实体
};

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_HTTP_HTTPRESPONSE_H
