#ifndef MUDUO_NET_HTTP_HTTPREQUEST_H
#define MUDUO_NET_HTTP_HTTPREQUEST_H

#include "muduo/base/copyable.h"
#include "muduo/base/Timestamp.h"
#include "muduo/base/Types.h"

#include <map>
#include <assert.h>
#include <stdio.h>

namespace muduo
{
	namespace net
	{

		//HTTP请求类封装
		class HttpRequest : public muduo::copyable
		{
		public:

			//HTTP请求方法
			enum Method
			{
				kInvalid, kGet, kPost, kHead, kPut, kDelete
			};

			//HTTP协议版本
			enum Version
			{
				kUnknown, kHttp10, kHttp11
			};

			//构造函数
			HttpRequest()
				: method_(kInvalid),
				version_(kUnknown)
			{
				//构造函数先初始化，请求方法和协议版本
			}

			//设置协议版本
			void setVersion(Version v)
			{
				version_ = v;
			}

			//获取版本
			Version getVersion() const
			{
				return version_;
			}

			//设置方法
			bool setMethod(const char* start, const char* end)
			{
				//start和end是用来构造一个字符串，不包含end
				//字符串的值是从start到end-1

				assert(method_ == kInvalid);
				string m(start, end);
				if (m == "GET")
				{
					method_ = kGet;
				}
				else if (m == "POST")
				{
					method_ = kPost;
				}
				else if (m == "HEAD")
				{
					method_ = kHead;
				}
				else if (m == "PUT")
				{
					method_ = kPut;
				}
				else if (m == "DELETE")
				{
					method_ = kDelete;
				}
				else
				{
					//不在以上方法，则设置为kInvalid
					method_ = kInvalid;
				}
				//method_ ！= kInvalid说明设置成功
				return method_ != kInvalid;
			}

			//返回请求方法
			Method method() const
			{
				return method_;
			}

			//将请求方法转为字符串
			const char* methodString() const
			{
				const char* result = "UNKNOWN";
				switch (method_)
				{
				case kGet:
					result = "GET";
					break;
				case kPost:
					result = "POST";
					break;
				case kHead:
					result = "HEAD";
					break;
				case kPut:
					result = "PUT";
					break;
				case kDelete:
					result = "DELETE";
					break;
				default:
					break;
				}
				return result;
			}

			//设置路径
			void setPath(const char* start, const char* end)
			{
				path_.assign(start, end);
			}

			//返回路径
			const string& path() const
			{
				return path_;
			}

			void setQuery(const char* start, const char* end)
			{
				query_.assign(start, end);
			}

			const string& query() const
			{
				return query_;
			}

			//设置接收时间
			void setReceiveTime(Timestamp t)
			{
				receiveTime_ = t;
			}

			//获取接收时间
			Timestamp receiveTime() const
			{
				return receiveTime_;
			}

			//添加头部信息
			void addHeader(const char* start, const char* colon, const char* end)
			{
				string field(start, colon);//header域
				++colon;
				//去除左空格
				while (colon < end && isspace(*colon))
				{
					++colon;
				}
				string value(colon, end);//header值
				//去除右空格
				while (!value.empty() && isspace(value[value.size() - 1]))
				{
					value.resize(value.size() - 1);
				}
				headers_[field] = value;
			}

			//根据头域返回header的值
			string getHeader(const string& field) const
			{
				string result;
				std::map<string, string>::const_iterator it = headers_.find(field);
				if (it != headers_.end())
				{
					result = it->second;
				}
				return result;
			}

			const std::map<string, string>& headers() const
			{
				return headers_;
			}

			//交换
			void swap(HttpRequest& that)
			{
				std::swap(method_, that.method_);
				std::swap(version_, that.version_);
				path_.swap(that.path_);
				query_.swap(that.query_);
				receiveTime_.swap(that.receiveTime_);
				headers_.swap(that.headers_);
			}

		private:
			Method method_;			//请求方法
			Version version_;			//协议版本
			string path_;				//请求路径
			string query_;			//查询
			Timestamp receiveTime_;	//请求时间
			std::map<string, string> headers_;	//header列表
		};

	}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_HTTP_HTTPREQUEST_H
