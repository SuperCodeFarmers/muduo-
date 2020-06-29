#include "muduo/net/http/HttpServer.h"

#include "muduo/base/Logging.h"
#include "muduo/net/http/HttpContext.h"
#include "muduo/net/http/HttpRequest.h"
#include "muduo/net/http/HttpResponse.h"

using namespace muduo;
using namespace muduo::net;

namespace muduo
{
	namespace net
	{
		namespace detail
		{

			void defaultHttpCallback(const HttpRequest&, HttpResponse* resp)
			{
				resp->setStatusCode(HttpResponse::k404NotFound);
				resp->setStatusMessage("Not Found");
				resp->setCloseConnection(true);
			}

		}  // namespace detail
	}  // namespace net
}  // namespace muduo

HttpServer::HttpServer(EventLoop* loop,
	const InetAddress& listenAddr,
	const string& name,
	TcpServer::Option option)
	: server_(loop, listenAddr, name, option),
	httpCallback_(detail::defaultHttpCallback)
{
	//初始化回调函数，当消息到来时会回调这两个函数
	server_.setConnectionCallback(
		std::bind(&HttpServer::onConnection, this, _1));
	server_.setMessageCallback(
		std::bind(&HttpServer::onMessage, this, _1, _2, _3));
}

//启动
void HttpServer::start()
{
	LOG_WARN << "HttpServer[" << server_.name()
		<< "] starts listenning on " << server_.ipPort();
	server_.start();
}

//当一个HTTP连接到来时
void HttpServer::onConnection(const TcpConnectionPtr& conn)
{
	//当一个HTTP连接到来时，我们调用一个setContext()
	//将TcpConnection与一个HttpContext绑定
	//因为TcpConnectionPtr中有一个boost::any类型的对象，可以绑定任意类型的对象
	if (conn->connected())
	{
		conn->setContext(HttpContext());
	}
}

//http请求到来，取出http请求的内容
void HttpServer::onMessage(const TcpConnectionPtr& conn,
	Buffer* buf,
	Timestamp receiveTime)
{
	HttpContext* context = boost::any_cast<HttpContext>(conn->getMutableContext());
	//解析http请求
	//如果解析成功返回true，失败返回false
	if (!context->parseRequest(buf, receiveTime))
	{
		//解析失败，则发送400错误，并且断开连接
		conn->send("HTTP/1.1 400 Bad Request\r\n\r\n");
		conn->shutdown();
	}

	//解析成功
	if (context->gotAll())
	{
		onRequest(conn, context->request());
		//本次请求处理完毕，重置httpContext对象，清空旧的请求数据
		//因为contex是跟一个tcpconnection绑定的
		context->reset();
	}
}

//请求
void HttpServer::onRequest(const TcpConnectionPtr& conn, const HttpRequest& req)
{
	//取出首部
	const string& connection = req.getHeader("Connection");
	//是否是长连接或短链接

	//如果为close或者版本不是HTTP/1.0，并且connection != "Keep-Alive"。因为HTTP1.0版本不支持长连接
	//就是短链接，要关闭连接
	bool close = connection == "close" ||
		(req.getVersion() == HttpRequest::kHttp10 && connection != "Keep-Alive");

	//将处理完请求是否要关闭连接的信息传给HttpResponse的对象
	HttpResponse response(close);

	//回调用户函数httpCallback_对http请求做相应处理，一旦我们处理完就会返回HttpResponse对象
	httpCallback_(req, &response);
	Buffer buf;
	//将这个对象转换成要给字符串，添加到buf中
	response.appendToBuffer(&buf);
	//将这个缓冲区发送给客户端
	conn->send(&buf);

	//判断一下是否是短链接，如果是短链接，就需要关闭TcpConnection连接
	if (response.closeConnection())
	{
		conn->shutdown();
	}
}

