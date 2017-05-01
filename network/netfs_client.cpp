#include "network.hpp"
#include "netfs.hpp"
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <vector>

using namespace Granite;
using namespace std;

struct FSReader : LooperHandler
{
	FSReader(const string &path, unique_ptr<Socket> socket)
		: LooperHandler(move(socket))
	{
		reply_builder.begin();
		reply_builder.add_u32(NETFS_READ_FILE);
		reply_builder.add_u32(NETFS_BEGIN_CHUNK);
		reply_builder.add_string(path);
		command_writer.start(reply_builder.get_buffer());
		state = WriteCommand;
	}

	bool write_command(Looper &looper)
	{
		auto ret = command_writer.process(*socket);
		if (command_writer.complete())
		{
			state = ReadReplySize;
			reply_builder.begin(4 * sizeof(uint32_t));
			command_reader.start(reply_builder.get_buffer());
			looper.modify_handler(EVENT_IN, *this);
			return true;
		}

		return ret == Socket::ErrorWouldBlock;
	}

	bool read_reply_size(Looper &)
	{
		auto ret = command_reader.process(*socket);
		if (command_reader.complete())
		{
			if (reply_builder.read_u32() != NETFS_BEGIN_CHUNK)
				return false;

			if (reply_builder.read_u32() != NETFS_ERROR_OK)
				return false;

			uint64_t reply_size = reply_builder.read_u64();
			if (reply_size == 0)
				return false;

			reply_builder.begin(reply_size);
			command_reader.start(reply_builder.get_buffer());
			state = ReadReply;
			return true;
		}

		return ret == Socket::ErrorWouldBlock;
	}

	bool read_reply(Looper &)
	{
		auto ret = command_reader.process(*socket);
		if (command_reader.complete())
		{
			fwrite(reply_builder.get_buffer().data(), reply_builder.get_buffer().size(), 1, stdout);
			return false;
		}

		return ret == Socket::ErrorWouldBlock;
	}

	bool handle(Looper &looper, EventFlags) override
	{
		if (state == WriteCommand)
			return write_command(looper);
		else if (state == ReadReplySize)
			return read_reply_size(looper);
		else if (state == ReadReply)
			return read_reply(looper);
		else
			return false;
	}

	enum State
	{
		WriteCommand,
		ReadReplySize,
		ReadReply
	};
	State state = WriteCommand;
	SocketReader command_reader;
	SocketWriter command_writer;
	ReplyBuilder reply_builder;
};

int main(int argc, char *argv[])
{
	if (argc != 2)
		return 1;

	Looper looper;
	auto client = Socket::connect("127.0.0.1", 7070);
	if (!client)
		return 1;

	looper.register_handler(EVENT_OUT, unique_ptr<FSReader>(new FSReader(argv[1], move(client))));
	while (looper.wait(-1) >= 0);
}