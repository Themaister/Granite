#pragma once

#include <stdint.h>
#include <functional>
#include <memory>
#include <unordered_map>

namespace Granite
{
class Looper;
class Socket;

class SocketReader
{
public:
	void start(void *data, size_t size);
	ssize_t process(Socket &socket);

	bool complete() const
	{
		return offset == size;
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
	ssize_t process(Socket &socket);

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
	Socket(int fd);
	~Socket();

	Socket(Socket &&) = delete;
	void operator=(Socket &&) = delete;

	void set_parent_looper(Looper *looper)
	{
		this->looper = looper;
	}

	Looper *get_parent_looper()
	{
		return looper;
	}

	int get_fd() const
	{
		return fd;
	}

	ssize_t write(const void *data, size_t size);
	ssize_t read(void *data, size_t size);

	enum Error
	{
		ErrorWouldBlock = -1,
		ErrorIO = -2
	};

	static std::unique_ptr<Socket> connect(const char *addr, uint16_t port);

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

private:
	int fd;
	std::unordered_map<int, std::unique_ptr<LooperHandler>> handlers;
};

class TCPListener : public LooperHandler
{
public:
	TCPListener(TCPListener &&) = delete;
	void operator=(TCPListener &&) = delete;
	std::unique_ptr<Socket> accept();

	template <typename T>
	static std::unique_ptr<T> bind(uint16_t port)
	{
		try
		{
			return std::unique_ptr<T>(new T(port));
		}
		catch (...)
		{
			return {};
		}
	}

protected:
	TCPListener(uint16_t port);
};

}