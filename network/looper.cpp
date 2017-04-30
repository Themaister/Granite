#include "netfs_server.hpp"

#include <stdexcept>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

using namespace std;

namespace Granite
{
Looper::Looper()
{
	fd = epoll_create1(0);
	if (fd < 0)
		throw runtime_error("Failed to create epoller.");
}

Looper::~Looper()
{
	if (fd >= 0)
		close(fd);
}

bool Looper::register_handler(Socket &sock, EventFlags events, unique_ptr<LooperHandler> handler)
{
	int flags = 0;
	if (events & EVENT_IN)
		flags |= EPOLLIN;
	if (events & EVENT_OUT)
		flags |= EPOLLOUT;
	flags |= EPOLLHUP | EPOLLERR;

	epoll_event event = {};
	event.events = flags;
	event.data.fd = sock.get_fd();
	if (epoll_ctl(fd, EPOLL_CTL_ADD, sock.get_fd(), &event) < 0)
		return false;

	sock.set_parent_looper(this);
	handlers[sock.get_fd()] = move(handler);
	return true;
}

void Looper::unregister_handler(Socket &sock)
{
	epoll_ctl(fd, EPOLL_CTL_DEL, sock.get_fd(), nullptr);
	sock.set_parent_looper(nullptr);

	auto itr = handlers.find(sock.get_fd());
	handlers.erase(itr);
}

int Looper::wait(int timeout)
{
	epoll_event events[64];
	int ret;
	int handled = 0;

	if (handlers.empty())
		return -1;

	while ((ret = epoll_wait(fd, events, 64, handled ? 0 : timeout)) > 0)
	{
		handled += ret;
		for (int i = 0; i < ret; i++)
		{
			auto itr = handlers.find(events[i].data.fd);
			if (itr == end(handlers))
				throw logic_error("Unknown handler.");

			int event_fd = events[i].data.fd;

			EventFlags flags = 0;
			if (events[i].events & EPOLLIN)
				flags |= EVENT_IN;
			if (events[i].events & EPOLLOUT)
				flags |= EVENT_OUT;
			if (events[i].events & EPOLLHUP)
				flags |= EVENT_HANGUP;
			if (events[i].events & EPOLLERR)
				flags |= EVENT_ERROR;

			fprintf(stderr, "Handling event (0x%x)!\n", events[i].events);
			auto done = itr->second->handle(flags);
			if (done)
			{
				auto *socket = itr->second->get_socket();
				if (socket)
					unregister_handler(*socket);
				else
				{
					epoll_ctl(fd, EPOLL_CTL_DEL, event_fd, nullptr);
					handlers[event_fd].reset();
				}
			}
		}
	}

	return handled;
}

}