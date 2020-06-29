#ifndef MUDUO_NET_INSPECT_PERFORMANCEINSPECTOR_H
#define MUDUO_NET_INSPECT_PERFORMANCEINSPECTOR_H

#include "muduo/net/inspect/Inspector.h"

namespace muduo
{
namespace net
{

class PerformanceInspector : noncopyable
{
 public:
  void registerCommands(Inspector* ins);

  static string heap(HttpRequest::Method, const Inspector::ArgList&);
  static string growth(HttpRequest::Method, const Inspector::ArgList&);
  static string profile(HttpRequest::Method, const Inspector::ArgList&);
  static string cmdline(HttpRequest::Method, const Inspector::ArgList&);
  static string memstats(HttpRequest::Method, const Inspector::ArgList&);
  static string memhistogram(HttpRequest::Method, const Inspector::ArgList&);
  static string releaseFreeMemory(HttpRequest::Method, const Inspector::ArgList&);

  static string symbol(HttpRequest::Method, const Inspector::ArgList&);
};

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_INSPECT_PERFORMANCEINSPECTOR_H
