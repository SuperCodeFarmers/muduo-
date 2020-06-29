#include "muduo/net/Buffer.h"

#include "muduo/net/SocketsOps.h"
#include "Buffer.h"
#include "SocketsOps.h"

#include <errno.h>
#include <sys/uio.h>

using namespace muduo;
using namespace muduo::net;

// 初始化/r/n
const char Buffer::kCRLF[] = "\r\n";

const size_t Buffer::kCheapPrepend;
const size_t Buffer::kInitialSize;

// 结合詹上的空间，避免内存使用过大，提高内存使用率
// 如果有10K个连接，每个连接就分配64K的缓冲区，将占用640M内存
// 而大多数时候，这些缓冲区的使用率很低
ssize_t Buffer::readFd(int fd, int* savedErrno)
{
	// saved an ioctl()/FIONREAD call to tell how much to read
	// 我们预先分配64M内存,这样可以节省一次ioctl系统调用
	char extrabuf[65536];
	struct iovec vec[2]; // 准备两块缓冲区
	const size_t writable = writableBytes();
	// 第一块缓冲区指向写的位置
	vec[0].iov_base = begin() + writerIndex_;
	vec[0].iov_len = writable;
	// 第二款缓冲区指向了栈上的空间
	vec[1].iov_base = extrabuf;
	vec[1].iov_len = sizeof extrabuf;
	// when there is enough space in this buffer, don't read into extrabuf.
	// when extrabuf is used, we read 128k-1 bytes at most.
	const int iovcnt = (writable < sizeof extrabuf) ? 2 : 1;
	const ssize_t n = sockets::readv(fd, vec, iovcnt);
	if (n < 0)
	{
		*savedErrno = errno;
	}
	else if (implicit_cast<size_t>(n) <= writable)  // 第一块缓冲区足够容纳
	{
		writerIndex_ += n;
	}
	else    // 当前缓冲区不够容纳，因而数据被接收到了第二块缓冲区extrabuf,将其append至buffer
	{
		writerIndex_ = buffer_.size();
		append(extrabuf, n - writable);
	}
	// if (n == writable + sizeof extrabuf)
	// {
	//   goto line_30;
	// }
	return n;
}

