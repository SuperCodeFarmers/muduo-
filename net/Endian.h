#ifndef MUDUO_NET_ENDIAN_H
#define MUDUO_NET_ENDIAN_H

#include <stdint.h>
#include <endian.h>

namespace muduo
{
	namespace net
	{
		namespace sockets
		{

			// the inline assembler code makes type blur,
			// so we disable warnings for a while.

			/*
			字节序转换函数
			*/

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wold-style-cast"

			//主机字节序转化为网络字节序，对64位整数进行转化
			inline uint64_t hostToNetwork64(uint64_t host64)
			{
				//使用htonl也是可以的
				return htobe64(host64);
			}
			//主机字节序转化为网络字节序，对32位整数进行转化
			inline uint32_t hostToNetwork32(uint32_t host32)
			{
				return htobe32(host32);
			}
			//主机字节序转化为网络字节序，对16位整数进行转化
			inline uint16_t hostToNetwork16(uint16_t host16)
			{
				return htobe16(host16);
			}
			//网络字节序转化为主机字节序，对64位整数进行转化
			inline uint64_t networkToHost64(uint64_t net64)
			{
				return be64toh(net64);
			}
			//网络字节序转化为主机字节序，对32位整数进行转化
			inline uint32_t networkToHost32(uint32_t net32)
			{
				return be32toh(net32);
			}
			//网络字节序转化为主机字节序，对16位整数进行转化
			inline uint16_t networkToHost16(uint16_t net16)
			{
				return be16toh(net16);
			}

#pragma GCC diagnostic pop

		}  // namespace sockets
	}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_ENDIAN_H
