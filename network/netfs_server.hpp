#pragma once

#include <stdint.h>
#include <functional>
#include <memory>
#include <unordered_map>

namespace Granite
{
class Looper;

class Socket
{
public:
	Socket(int fd);
	~Socket();

	Socket(Socket &&) = delete;
	void operator=(Socket &&) = delete;

	void set_parent_looper(Looper *looper)
	{
		this->looper = looper;
	}

	int get_fd() const
	{
		return fd;
	}

	ssize_t write(const void *data, size_t size);
	ssize_t read(void *data, size_t size);

	enum
	{
		ErrorWouldBlock = -1,
		ErrorIO = -2
	};

private:
	int fd;
	Looper *looper = nullptr;
};

class SocketGlobal
{
public:
	static SocketGlobal &get();
private:
	SocketGlobal();
};

class TCPListener
{
public:
	TCPListener() = default;
	~TCPListener();

	TCPListener(TCPListener &&) = delete;
	void operator=(TCPListener &&) = delete;

	bool init(uint16_t port);
	std::unique_ptr<Socket> accept();

private:
	int fd = -1;
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
	virtual ~LooperHandler() = default;
	virtual bool handle(EventFlags flags) = 0;

	Socket *get_socket()
	{
		return socket.get();
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

	bool register_handler(Socket &sock, EventFlags events, std::unique_ptr<LooperHandler> handler);
	void unregister_handler(Socket &sock);
	int wait(int timeout = -1);

private:
	int fd;
	std::unordered_map<int, std::unique_ptr<LooperHandler>> handlers;
};
}