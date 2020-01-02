/* Copyright (c) 2017-2020 Hans-Kristian Arntzen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "network.hpp"

#ifdef __linux__
#include <string>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#endif

using namespace std;

namespace Granite
{
void SocketReader::start(void *data_, size_t size_)
{
	data = data_;
	size = size_;
	offset = 0;
}

void SocketWriter::start(const void *data_, size_t size_)
{
	data = data_;
	size = size_;
	offset = 0;
}

int SocketReader::process(Socket &socket)
{
	size_t to_read = size - offset;
	auto res = socket.read(static_cast<uint8_t *>(data) + offset, to_read);
	if (res <= 0)
		return res;

	offset += res;
	return offset;
}

int SocketWriter::process(Socket &socket)
{
	size_t to_write = size - offset;
	auto res = socket.write(static_cast<const uint8_t *>(data) + offset, to_write);
	if (res <= 0)
		return res;

	offset += res;
	return offset;
}

Socket::Socket(int fd_, bool owned_)
	: fd(fd_), owned(owned_)
{
}

unique_ptr<Socket> Socket::connect(const char *addr, uint16_t port)
{
#ifdef __linux__
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
		fd = socket(walk->ai_family, walk->ai_socktype, walk->ai_protocol);

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
#else
	return {};
#endif
}

Socket::~Socket()
{
	if (looper)
		looper->unregister_handler(*this);

#ifdef __linux__
	if (owned && fd >= 0)
		close(fd);
#endif
}

int Socket::read(void *data, size_t size)
{
#ifdef __linux__
	auto ret = ::recv(fd, data, size, 0);
	if (ret < 0)
	{
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return ErrorWouldBlock;
		else
			return ErrorIO;
	}
	return ret;
#else
	return -1;
#endif
}

int Socket::write(const void *data, size_t size)
{
#ifdef __linux__
	auto ret = ::send(fd, data, size, MSG_NOSIGNAL);
	if (ret < 0)
	{
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return ErrorWouldBlock;
		else
			return ErrorIO;
	}
	return ret;
#else
	return -1;
#endif
}
}
