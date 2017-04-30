#include "network.hpp"
#include "util.hpp"
#include <arpa/inet.h>

using namespace Granite;
using namespace std;

struct FSHandler : LooperHandler
{
	FSHandler(unique_ptr<Socket> socket)
		: LooperHandler(move(socket))
	{
		command_reader.start(&command_id, sizeof(command_id));
	}

	bool parse_command(Looper &looper)
	{
		command_id = ntohl(command_id);
		switch (command_id)
		{
		case 1:
			looper.modify_handler(EVENT_OUT, *this);
			state = WritingReply;
			command_writer.start("haithar", 7);
			return true;

		default:
			return false;
		}
	}

	bool read_command(Looper &looper)
	{
		auto ret = command_reader.process(*socket);
		switch (ret)
		{
		case 0:
		case Socket::ErrorIO:
			return false;

		case Socket::ErrorWouldBlock:
			return true;

		default:
			if (command_reader.complete())
				return parse_command(looper);
			else
				return true;
		}
	}

	bool write_reply(Looper &)
	{
		auto ret = command_writer.process(*socket);
		switch (ret)
		{
		case 0:
		case Socket::ErrorIO:
			return false;

		case Socket::ErrorWouldBlock:
			return true;

		default:
			return false;
		}
	}

	bool handle(Looper &looper, EventFlags) override
	{
		if (state == ReadCommand)
			return read_command(looper);
		else if (state == WritingReply)
			return write_reply(looper);
		else
			return false;
	}

	enum State
	{
		ReadCommand,
		WritingReply
	};
	State state = ReadCommand;
	SocketReader command_reader;
	SocketWriter command_writer;
	uint32_t command_id = 0;
};

struct ListenerHandler : TCPListener
{
	ListenerHandler(uint16_t port)
		: TCPListener(port)
	{
	}

	bool handle(Looper &looper, EventFlags) override
	{
		auto client = accept();
		if (client)
			looper.register_handler(EVENT_IN, unique_ptr<FSHandler>(new FSHandler(move(client))));
		return false;
	}
};

int main()
{
	Looper looper;
	auto listener = TCPListener::bind<ListenerHandler>(7070);
	if (!listener)
	{
		LOGE("Failed to listen to port 7070.");
		return 1;
	}

	looper.register_handler(EVENT_IN, move(listener));
	while (looper.wait(-1) >= 0);
}