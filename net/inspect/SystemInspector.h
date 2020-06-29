#ifndef MUDUO_NET_INSPECT_SYSTEMINSPECTOR_H
#define MUDUO_NET_INSPECT_SYSTEMINSPECTOR_H

#include "muduo/net/inspect/Inspector.h"

namespace muduo
{
namespace net
{

class SystemInspector : noncopyable
{
 public:
  void registerCommands(Inspector* ins);

  static string overview(HttpRequest::Method, const Inspector::ArgList&);
  static string loadavg(HttpRequest::Method, const Inspector::ArgList&);
  static string version(HttpRequest::Method, const Inspector::ArgList&);
  static string cpuinfo(HttpRequest::Method, const Inspector::ArgList&);
  static string meminfo(HttpRequest::Method, const Inspector::ArgList&);
  static string stat(HttpRequest::Method, const Inspector::ArgList&);
};

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_INSPECT_SYSTEMINSPECTOR_H
