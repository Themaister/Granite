#include "network.hpp"
#include <arpa/inet.h>
#include <unistd.h>

using namespace Granite;
using namespace std;

struct FSClient : LooperHandler
{
	FSClient(unique_ptr<Socket> socket)
		: LooperHandler(move(socket))
	{
		static const uint32_t command = htonl(1);
		command_writer.start(&command, sizeof(command));
	}

	bool write_command(Looper &looper)
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
			if (command_writer.complete())
			{
				state = ReadReply;
				command_reader.start(reply, sizeof(reply));
				looper.modify_handler(EVENT_IN, *this);
			}
			return true;
		}
	}

	bool read_reply(Looper &)
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
			{
				::write(1, reply, sizeof(reply));
			}
			return false;
		}
	}

	bool handle(Looper &looper, EventFlags) override
	{
		if (state == WriteCommand)
			return write_command(looper);
		else if (state == ReadReply)
			return read_reply(looper);
		else
			return false;
	}

	enum State
	{
		WriteCommand,
		ReadReply
	};
	char reply[7];
	State state = WriteCommand;
	SocketReader command_reader;
	SocketWriter command_writer;
};

int main()
{
	Looper looper;
	auto client = Socket::connect("127.0.0.1", 7070);
	if (!client)
		return 1;

	looper.register_handler(EVENT_OUT, unique_ptr<FSClient>(new FSClient(move(client))));
	while (looper.wait(-1) >= 0);
}