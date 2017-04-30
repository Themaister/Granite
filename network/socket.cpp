#include "netfs_server.hpp"

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

namespace Granite
{
Socket::Socket(int fd)
	: fd(fd)
{
}

Socket::~Socket()
{
	if (looper)
		looper->unregister_handler(*this);

	if (fd >= 0)
		close(fd);
}

ssize_t Socket::read(void *data, size_t size)
{
	auto ret = ::recv(fd, data, size, 0);
	if (ret < 0)
	{
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return ErrorWouldBlock;
		else
			return ErrorIO;
	}
	return ret;
}

ssize_t Socket::write(const void *data, size_t size)
{
	auto ret = ::send(fd, data, size, MSG_NOSIGNAL);
	if (ret < 0)
	{
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return ErrorWouldBlock;
		else
			return ErrorIO;
	}
	return ret;
}
}
