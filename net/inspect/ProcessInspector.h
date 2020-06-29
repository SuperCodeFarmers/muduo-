#ifndef MUDUO_NET_INSPECT_PROCESSINSPECTOR_H
#define MUDUO_NET_INSPECT_PROCESSINSPECTOR_H

#include "muduo/net/inspect/Inspector.h"

namespace muduo
{
namespace net
{

// 过程检查类
class ProcessInspector : noncopyable
{
 public:
  void registerCommands(Inspector* ins);//注册命令接口

  //回调函数
  static string overview(HttpRequest::Method, const Inspector::ArgList&);
  static string pid(HttpRequest::Method, const Inspector::ArgList&);
  static string procStatus(HttpRequest::Method, const Inspector::ArgList&);
  static string openedFiles(HttpRequest::Method, const Inspector::ArgList&);
  static string threads(HttpRequest::Method, const Inspector::ArgList&);

  static string username_;
};

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_INSPECT_PROCESSINSPECTOR_H
