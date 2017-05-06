#include "network.hpp"

#include <stdexcept>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/eventfd.h>

using namespace std;

namespace Granite
{
LooperHandler::LooperHandler(std::unique_ptr<Socket> socket)
	: socket(move(socket))
{
}

Looper::Looper()
{
	fd = epoll_create1(0);
	if (fd < 0)
		throw runtime_error("Failed to create epoller.");

	event_fd = ::eventfd(0, EFD_NONBLOCK);
	if (event_fd < 0)
		throw runtime_error("Failed to create eventfd.");

	epoll_event event = {};
	event.events = EPOLLIN;
	event.data.ptr = nullptr;
	if (epoll_ctl(fd, EPOLL_CTL_ADD, event_fd, &event) < 0)
		throw runtime_error("Failed to add event fd to epoll.");
}

Looper::~Looper()
{
	if (event_fd >= 0)
		close(event_fd);
	if (fd >= 0)
		close(fd);
}

bool Looper::modify_handler(EventFlags events, LooperHandler &handler)
{
	int flags = 0;
	if (events & EVENT_IN)
		flags |= EPOLLIN;
	if (events & EVENT_OUT)
		flags |= EPOLLOUT;
	flags |= EPOLLHUP | EPOLLERR;

	epoll_event event = {};
	event.events = flags;
	event.data.ptr = &handler;
	if (epoll_ctl(fd, EPOLL_CTL_MOD, handler.get_socket().get_fd(), &event) < 0)
		return false;

	return true;
}

bool Looper::register_handler(EventFlags events, unique_ptr<LooperHandler> handler)
{
	int flags = 0;
	if (events & EVENT_IN)
		flags |= EPOLLIN;
	if (events & EVENT_OUT)
		flags |= EPOLLOUT;

	epoll_event event = {};
	event.events = flags;
	event.data.ptr = handler.get();
	if (epoll_ctl(fd, EPOLL_CTL_ADD, handler->get_socket().get_fd(), &event) < 0)
		return false;

	handler->get_socket().set_parent_looper(this);
	handlers[handler->get_socket().get_fd()] = move(handler);
	return true;
}

void Looper::unregister_handler(Socket &sock)
{
	epoll_ctl(fd, EPOLL_CTL_DEL, sock.get_fd(), nullptr);
	sock.set_parent_looper(nullptr);

	auto itr = handlers.find(sock.get_fd());
	handlers.erase(itr);
}

void Looper::run_in_looper(std::function<void()> func)
{
	{
		lock_guard<mutex> holder{queue_lock};
		func_queue.push_back(move(func));
	}

	uint64_t one = 1;
	::write(event_fd, &one, sizeof(one));
}

void Looper::kill()
{
	{
		lock_guard<mutex> holder{queue_lock};
		func_queue.push_back([this]() {
			dead = true;
		});
	}
	uint64_t one = 1;
	::write(event_fd, &one, sizeof(one));
}

void Looper::handle_deferred_funcs()
{
	uint64_t count = 0;
	if (::read(event_fd, &count, sizeof(count)) < 0)
		return;
	if (!count)
		return;

	lock_guard<mutex> holder{queue_lock};
	for (auto &func : func_queue)
		func();
	func_queue.clear();
}

int Looper::wait_idle(int timeout)
{
	if (dead)
		return -1;

	epoll_event events[64];
	int ret;
	int handled = 0;

	while ((ret = epoll_wait(fd, events, 64, handled ? 0 : timeout)) > 0)
	{
		handled += ret;
		for (int i = 0; i < ret; i++)
		{
			auto *handler = static_cast<LooperHandler *>(events[i].data.ptr);

			if (!handler)
			{
				handle_deferred_funcs();
				continue;
			}

			EventFlags flags = 0;
			if (events[i].events & EPOLLIN)
				flags |= EVENT_IN;
			if (events[i].events & EPOLLOUT)
				flags |= EVENT_OUT;
			if (events[i].events & EPOLLHUP)
				flags |= EVENT_HANGUP;
			if (events[i].events & EPOLLERR)
				flags |= EVENT_ERROR;

			//fprintf(stderr, "Handling event (0x%x)!\n", events[i].events);
			auto done = !handler->handle(*this, flags);
			if (done)
			{
				auto &socket = handler->get_socket();
				unregister_handler(socket);
			}
		}
	}

	return handled;
}

int Looper::wait(int timeout)
{
	if (handlers.empty())
		return -1;

	return wait_idle(timeout);
}

}