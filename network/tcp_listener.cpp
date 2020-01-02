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

#ifdef _WIN32
namespace Granite
{
SocketGlobal::SocketGlobal()
{
}

SocketGlobal &SocketGlobal::get()
{
	static SocketGlobal global;
	return global;
}

std::unique_ptr<Socket> TCPListener::accept()
{
	return {};
}

TCPListener::TCPListener(uint16_t port)
{
	throw std::runtime_error("Unimplemented feature on Windows.");
}
}
#else
#include <string>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

using namespace std;

namespace Granite
{
SocketGlobal::SocketGlobal()
{
}

SocketGlobal &SocketGlobal::get()
{
	static SocketGlobal global;
	return global;
}

unique_ptr<Socket> TCPListener::accept()
{
	sockaddr_storage their;
	socklen_t their_size = sizeof(their);
	int new_fd = ::accept(socket->get_fd(),
                          reinterpret_cast<sockaddr *>(&their), &their_size);

	int old = fcntl(new_fd, F_GETFL);
	if (fcntl(new_fd, F_SETFL, old | O_NONBLOCK) < 0)
	{
		close(new_fd);
		return {};
	}

	return unique_ptr<Socket>(new Socket(new_fd));
}

TCPListener::TCPListener(uint16_t port)
{
	SocketGlobal::get();

	addrinfo hints = {};
	addrinfo *servinfo;

	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	int res = getaddrinfo(nullptr, std::to_string(port).c_str(), &hints, &servinfo);
	if (res < 0)
		throw runtime_error("getaddrinfo");

	int fd = -1;

	addrinfo *walk;
	for (walk = servinfo; walk; walk = walk->ai_next)
	{
		fd = ::socket(walk->ai_family, walk->ai_socktype,
		              walk->ai_protocol);

		if (fd < 0)
			continue;

		int yes = 1;
		if (setsockopt(fd, SOL_SOCKET,
		               SO_REUSEADDR, &yes, sizeof(int)) < 0)
		{
			close(fd);
			continue;
		}

		if (::bind(fd, walk->ai_addr, walk->ai_addrlen) < 0)
		{
			close(fd);
			break;
		}

		break;
	}

	freeaddrinfo(servinfo);

	if (!walk)
		throw runtime_error("bind");

	if (listen(fd, 64) < 0)
	{
		close(fd);
		throw runtime_error("listen");
	}

	socket = unique_ptr<Socket>(new Socket(fd));
}
}
#endif