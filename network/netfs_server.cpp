#include "network.hpp"
#include "util.hpp"
#include "netfs.hpp"
#include <arpa/inet.h>
#include "filesystem.hpp"

using namespace Granite;
using namespace std;

struct FSHandler : LooperHandler
{
	FSHandler(unique_ptr<Socket> socket)
		: LooperHandler(move(socket))
	{
		command_reader.start(&command_id, sizeof(command_id));
		state = ReadCommand;
	}

	bool parse_command(Looper &)
	{
		command_id = ntohl(command_id);
		switch (command_id)
		{
		case NETFS_READ_FILE:
			state = ReadChunkSize;
			command_reader.start(reply, 3 * sizeof(uint32_t));
			return true;

		default:
			return false;
		}
	}

	bool read_chunk_size(Looper &)
	{
		auto ret = command_reader.process(*socket);
		if (command_reader.complete())
		{
			for (auto &n : reply)
				n = ntohl(n);

			if (reply[0] != NETFS_BEGIN_CHUNK)
				return false;

			uint64_t chunk_size = (uint64_t(reply[1]) << 32) | reply[2];
			if (!chunk_size)
				return false;

			reply_buffer.resize(chunk_size);
			command_reader.start(reply_buffer.data(), reply_buffer.size());
			state = ReadChunkData;
			return true;
		}

		return ret == Socket::ErrorWouldBlock;
	}

	bool read_chunk_data(Looper &looper)
	{
		auto ret = command_reader.process(*socket);
		if (command_reader.complete())
		{
			looper.modify_handler(EVENT_OUT, *this);
			state = WriteReplyChunk;

			string str(reinterpret_cast<const char *>(reply_buffer.data()),
			           reinterpret_cast<const char *>(reply_buffer.data()) + reply_buffer.size());

			file = Filesystem::get().open(str);
			mapped = nullptr;
			if (file)
				mapped = file->map();

			if (mapped)
			{
				reply[0] = htonl(NETFS_BEGIN_CHUNK);
				reply[1] = htonl(NETFS_ERROR_OK);
				reply[2] = htonl(uint32_t(uint64_t(file->get_size()) >> 32));
				reply[3] = htonl(uint32_t(file->get_size()));
			}
			else
			{
				reply[0] = htonl(NETFS_BEGIN_CHUNK);
				reply[1] = htonl(NETFS_ERROR_IO);
				reply[2] = htonl(0);
				reply[3] = htonl(0);
			}
			command_writer.start(reply, sizeof(reply));

			return true;
		}

		return ret == Socket::ErrorWouldBlock;
	}

	bool read_command(Looper &looper)
	{
		auto ret = command_reader.process(*socket);
		if (command_reader.complete())
			return parse_command(looper);

		return ret == Socket::ErrorWouldBlock;
	}

	bool write_reply_chunk(Looper &)
	{
		auto ret = command_writer.process(*socket);
		if (command_writer.complete())
		{
			if (mapped)
			{
				command_writer.start(mapped, file->get_size());
				state = WriteReplyData;
				return true;
			}
			else
				return false;
		}

		return ret == Socket::ErrorWouldBlock;
	}

	bool write_reply_data(Looper &)
	{
		auto ret = command_writer.process(*socket);
		if (command_writer.complete())
			return false;

		return ret == Socket::ErrorWouldBlock;
	}

	bool handle(Looper &looper, EventFlags) override
	{
		if (state == ReadCommand)
			return read_command(looper);
		else if (state == ReadChunkSize)
			return read_chunk_size(looper);
		else if (state == ReadChunkData)
			return read_chunk_data(looper);
		else if (state == WriteReplyChunk)
			return write_reply_chunk(looper);
		else if (state == WriteReplyData)
			return write_reply_data(looper);
		else
			return false;
	}

	enum State
	{
		ReadCommand,
		ReadChunkSize,
		ReadChunkData,
		WriteReplyChunk,
		WriteReplyData
	};
	uint32_t reply[4];
	vector<uint8_t> reply_buffer;
	State state = ReadCommand;
	SocketReader command_reader;
	SocketWriter command_writer;
	uint32_t command_id = 0;

	unique_ptr<File> file;
	void *mapped = nullptr;
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
		return true;
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