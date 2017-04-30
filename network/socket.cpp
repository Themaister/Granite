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
void SocketReader::start(void *data, size_t size)
{
	this->data = data;
	this->size = size;
	offset = 0;
}

void SocketWriter::start(const void *data, size_t size)
{
	this->data = data;
	this->size = size;
	offset = 0;
}

ssize_t SocketReader::process(Socket &socket)
{
	size_t to_read = size - offset;
	auto res = socket.read(static_cast<uint8_t *>(data) + offset, to_read);
	if (res <= 0)
		return res;

	offset += res;
	return offset;
}

ssize_t SocketWriter::process(Socket &socket)
{
	size_t to_write = size - offset;
	auto res = socket.write(static_cast<const uint8_t *>(data) + offset, to_write);
	if (res <= 0)
		return res;

	offset += res;
	return offset;
}

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
