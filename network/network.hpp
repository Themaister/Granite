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

#pragma once

#include <stdint.h>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace Granite
{
class Looper;
class Socket;

class SocketReader
{
public:
	void start(void *data, size_t size);
	int process(Socket &socket);

	void start(std::vector<uint8_t> &buffer)
	{
		start(buffer.data(), buffer.size());
	}

	bool complete() const
	{
		return size != 0 && offset == size;
	}

private:
	void *data = nullptr;
	size_t offset = 0;
	size_t size = 0;
};

class SocketWriter
{
public:
	void start(const void *data, size_t size);

	void start(const std::vector<uint8_t> &buffer)
	{
		start(buffer.data(), buffer.size());
	}

	int process(Socket &socket);

	bool complete() const
	{
		return offset == size;
	}

private:
	const void *data = nullptr;
	size_t offset = 0;
	size_t size = 0;
};

class Socket
{
public:
	Socket(int fd, bool owned = true);
	~Socket();

	Socket(Socket &&) = delete;
	void operator=(Socket &&) = delete;

	void set_parent_looper(Looper *looper_)
	{
		looper = looper_;
	}

	Looper *get_parent_looper()
	{
		return looper;
	}

	int get_fd() const
	{
		return fd;
	}

	int write(const void *data, size_t size);
	int read(void *data, size_t size);

	enum Error
	{
		ErrorWouldBlock = -1,
		ErrorIO = -2
	};

	static std::unique_ptr<Socket> connect(const char *addr, uint16_t port);

private:
	Looper *looper = nullptr;
	int fd;
	bool owned;
};

class SocketGlobal
{
public:
	static SocketGlobal &get();
private:
	SocketGlobal();
};


enum EventFlagBits
{
	EVENT_IN = 1 << 0,
	EVENT_OUT = 1 << 1,
	EVENT_HANGUP = 1 << 2,
	EVENT_ERROR = 1 << 3
};
using EventFlags = uint32_t;

class LooperHandler
{
public:
	LooperHandler(std::unique_ptr<Socket> socket);
	LooperHandler() = default;

	virtual ~LooperHandler() = default;
	virtual bool handle(Looper &looper, EventFlags flags) = 0;

	Socket &get_socket()
	{
		return *socket.get();
	}

protected:
	std::unique_ptr<Socket> socket;
};

class Looper
{
public:
	Looper();
	~Looper();

	Looper(Looper &&) = delete;
	void operator=(Looper &&) = delete;

	bool modify_handler(EventFlags events, LooperHandler &handler);
	bool register_handler(EventFlags events, std::unique_ptr<LooperHandler> handler);
	void unregister_handler(Socket &sock);
	int wait(int timeout = -1);
	int wait_idle(int timeout = -1);
	void run_in_looper(std::function<void ()> func);
	void kill();

private:
	int fd;
	std::unordered_map<int, std::unique_ptr<LooperHandler>> handlers;

	int event_fd;
	std::mutex queue_lock;
	std::vector<std::function<void ()>> func_queue;
	void handle_deferred_funcs();

	bool dead = false;
};

class TCPListener : public LooperHandler
{
public:
	TCPListener(TCPListener &&) = delete;
	void operator=(TCPListener &&) = delete;
	std::unique_ptr<Socket> accept();

protected:
	TCPListener(uint16_t port);
};

}