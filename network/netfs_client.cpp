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
		static const uint32_t command = htonl(NETFS_READ_FILE);
		static const uint32_t chunk = htonl(NETFS_BEGIN_CHUNK);

		uint32_t s0 = htonl(uint32_t(uint64_t(path.size()) >> 32));
		uint32_t s1 = htonl(uint32_t(path.size()));

		reply_buffer.resize(4 * sizeof(uint32_t) + path.size());
		memcpy(reply_buffer.data() + 0 * sizeof(uint32_t), &command, sizeof(command));
		memcpy(reply_buffer.data() + 1 * sizeof(uint32_t), &chunk, sizeof(chunk));
		memcpy(reply_buffer.data() + 2 * sizeof(uint32_t), &s0, sizeof(s0));
		memcpy(reply_buffer.data() + 3 * sizeof(uint32_t), &s1, sizeof(s1));
		memcpy(reply_buffer.data() + 4 * sizeof(uint32_t), path.data(), path.size());

		command_writer.start(reply_buffer.data(), reply_buffer.size());
		state = WriteCommand;
	}

	bool write_command(Looper &looper)
	{
		auto ret = command_writer.process(*socket);
		if (command_writer.complete())
		{
			state = ReadReplySize;
			command_reader.start(reply, sizeof(reply));
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
			for (auto &e : reply)
				e = ntohl(e);

			if (reply[0] != NETFS_BEGIN_CHUNK)
				return false;

			if (reply[1] != NETFS_ERROR_OK)
				return false;

			uint64_t reply_size = (uint64_t(reply[2]) << 32) | reply[3];
			if (reply_size == 0)
				return false;

			reply_buffer.resize(reply_size);
			command_reader.start(reply_buffer.data(), reply_buffer.size());
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
			fwrite(reply_buffer.data(), reply_buffer.size(), 1, stdout);
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
	uint32_t reply[4];
	vector<uint8_t> reply_buffer;
	State state = WriteCommand;
	SocketReader command_reader;
	SocketWriter command_writer;
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