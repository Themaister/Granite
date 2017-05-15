#include "network.hpp"

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