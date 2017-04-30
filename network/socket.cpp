#include "network.hpp"

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

using namespace std;

namespace Granite
{
Socket::Socket(int fd)
	: fd(fd)
{
}

unique_ptr<Socket> Socket::connect(const char *addr, uint16_t port)
{
	SocketGlobal::get();

	int fd = -1;

	addrinfo hints = {};
	addrinfo *servinfo;

	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	int res = getaddrinfo(addr, std::to_string(port).c_str(), &hints, &servinfo);
	if (res < 0)
		return {};

	addrinfo *walk;
	for (walk = servinfo; walk; walk = walk->ai_next)
	{
		fd = socket(walk->ai_family, walk->ai_socktype,
		            walk->ai_protocol);

		if (fd < 0)
			continue;

		if (::connect(fd, walk->ai_addr, walk->ai_addrlen) < 0)
		{
			close(fd);
			continue;
		}

		break;
	}

	freeaddrinfo(servinfo);

	if (!walk)
		return {};

	int flags = fcntl(fd, F_GETFL);
	if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
	{
		close(fd);
		return {};
	}

	return unique_ptr<Socket>(new Socket(fd));
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
