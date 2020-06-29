#ifndef MUDUO_BASE_COPYABLE_H
#define MUDUO_BASE_COPYABLE_H

namespace muduo
{

/// A tag class emphasises the objects are copyable.
/// The empty base class optimization applies.
/// Any derived class of copyable should be a value type.

///空基类，用于标识的类，值类型
class copyable
{
 protected:
  copyable() = default;
  ~copyable() = default;
};

}  // namespace muduo

#endif  // MUDUO_BASE_COPYABLE_H
