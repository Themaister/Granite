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
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/eventfd.h>
#endif

using namespace std;

namespace Granite
{
LooperHandler::LooperHandler(std::unique_ptr<Socket> socket_)
	: socket(move(socket_))
{
}

Looper::Looper()
{
#ifdef __linux__
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
#else
	throw std::runtime_error("Unimplemented feature on Windows.");
#endif
}

Looper::~Looper()
{
#ifdef __linux__
	for (auto &handler : handlers)
		handler.second->get_socket().set_parent_looper(nullptr);

	if (event_fd >= 0)
		close(event_fd);
	if (fd >= 0)
		close(fd);
#endif
}

bool Looper::modify_handler(EventFlags events, LooperHandler &handler)
{
#ifdef __linux__
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
#else
	return false;
#endif
}

bool Looper::register_handler(EventFlags events, unique_ptr<LooperHandler> handler)
{
#ifdef __linux__
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
#else
	return false;
#endif
}

void Looper::unregister_handler(Socket &sock)
{
#ifdef __linux__
	epoll_ctl(fd, EPOLL_CTL_DEL, sock.get_fd(), nullptr);
	sock.set_parent_looper(nullptr);

	auto itr = handlers.find(sock.get_fd());
	handlers.erase(itr);
#endif
}

void Looper::run_in_looper(std::function<void()> func)
{
#ifdef __linux__
	{
		lock_guard<mutex> holder{queue_lock};
		func_queue.push_back(move(func));
	}

	uint64_t one = 1;
	::write(event_fd, &one, sizeof(one));
#endif
}

void Looper::kill()
{
#ifdef __linux__
	{
		lock_guard<mutex> holder{queue_lock};
		func_queue.push_back([this]() {
			dead = true;
		});
	}
	uint64_t one = 1;
	::write(event_fd, &one, sizeof(one));
#endif
}

void Looper::handle_deferred_funcs()
{
#ifdef __linux__
	uint64_t count = 0;
	if (::read(event_fd, &count, sizeof(count)) < 0)
		return;
	if (!count)
		return;

	lock_guard<mutex> holder{queue_lock};
	for (auto &func : func_queue)
		func();
	func_queue.clear();
#endif
}

int Looper::wait_idle(int timeout)
{
#ifdef __linux__
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
#else
	return -1;
#endif
}

int Looper::wait(int timeout)
{
#ifdef __linux__
	if (handlers.empty())
		return -1;

	return wait_idle(timeout);
#else
	return -1;
#endif
}

}
