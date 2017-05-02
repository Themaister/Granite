#include "network.hpp"
#include "netfs.hpp"
#include "util.hpp"
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <vector>

using namespace Granite;
using namespace std;

struct FSNotifyCommand : LooperHandler
{
	FSNotifyCommand(const string &protocol, const string &path, unique_ptr<Socket> socket)
		: LooperHandler(move(socket))
	{
		reply_builder.add_u32(NETFS_NOTIFICATION);
		reply_builder.add_u32(NETFS_BEGIN_CHUNK_REQUEST);
		reply_builder.add_string(protocol);

		reply_builder.add_u32(NETFS_REGISTER_NOTIFICATION);
		reply_builder.add_string(path);
		command_writer.start(reply_builder.get_buffer());
		state = WriteCommand;
	}

	bool write_command(Looper &looper, EventFlags)
	{
		auto ret = command_writer.process(*socket);
		if (command_writer.complete())
		{
			looper.modify_handler(EVENT_IN, *this);
			result_reply.begin(4 * sizeof(uint32_t));
			command_reader.start(result_reply.get_buffer());
			state = ReadReply;
			return true;
		}

		return (ret > 0) || (ret == Socket::ErrorWouldBlock);
	}

	bool read_reply_data(Looper &)
	{
		auto ret = command_reader.process(*socket);
		if (command_reader.complete())
		{
			if (last_cmd == NETFS_BEGIN_CHUNK_NOTIFICATION)
			{
				auto path = result_reply.read_string();
				auto type = result_reply.read_u32();
				const char *notification = "";
				switch (type)
				{
				case NETFS_FILE_CHANGED:
					notification = "changed";
					break;
				case NETFS_FILE_DELETED:
					notification = "deleted";
					break;
				case NETFS_FILE_CREATED:
					notification = "created";
					break;
				}

				LOGI("Notification: %s %s!\n", path.c_str(), notification);
				result_reply.begin(4 * sizeof(uint32_t));
				command_reader.start(result_reply.get_buffer());
				state = ReadReply;
				return true;
			}
			else if (last_cmd == NETFS_BEGIN_CHUNK_REPLY)
			{
				auto handle = int(result_reply.read_u64());
				LOGI("Got notification handle: %d!\n", handle);
				result_reply.begin(4 * sizeof(uint32_t));
				command_reader.start(result_reply.get_buffer());
				state = ReadReply;
				return true;
			}
			else
				return false;
		}

		return (ret > 0) || (ret == Socket::ErrorWouldBlock);
	}

	bool read_reply(Looper &)
	{
		auto ret = command_reader.process(*socket);
		if (command_reader.complete())
		{
			auto cmd = result_reply.read_u32();
			if (cmd == NETFS_BEGIN_CHUNK_NOTIFICATION || cmd == NETFS_BEGIN_CHUNK_REPLY)
			{
				if (result_reply.read_u32() != NETFS_ERROR_OK)
					return false;

				last_cmd = cmd;
				auto size = result_reply.read_u64();
				result_reply.begin(size);
				command_reader.start(result_reply.get_buffer());
				state = ReadReplyData;
				return true;
			}
			else
				return false;
		}

		return (ret > 0) || (ret == Socket::ErrorWouldBlock);
	}

	bool handle(Looper &looper, EventFlags flags) override
	{
		if (state == WriteCommand)
			return write_command(looper, flags);
		else if (state == ReadReply)
			return read_reply(looper);
		else if (state == ReadReplyData)
			return read_reply_data(looper);
		else
			return false;
	}

	enum State
	{
		WriteCommand,
		ReadReply,
		ReadReplyData
	};

	State state = WriteCommand;
	SocketReader command_reader;
	SocketWriter command_writer;
	ReplyBuilder reply_builder;
	ReplyBuilder result_reply;
	uint32_t last_cmd = 0;
};

struct FSWriteCommand : LooperHandler
{
	FSWriteCommand(const string &path, const vector<uint8_t> &buffer, unique_ptr<Socket> socket)
		: LooperHandler(move(socket))
	{
		target_size = buffer.size();

		reply_builder.begin();
		result_reply.begin(4 * sizeof(uint32_t));

		reply_builder.add_u32(NETFS_WRITE_FILE);
		reply_builder.add_u32(NETFS_BEGIN_CHUNK_REQUEST);
		reply_builder.add_string(path);
		reply_builder.add_u32(NETFS_BEGIN_CHUNK_REQUEST);
		reply_builder.add_u64(buffer.size());
		reply_builder.add_buffer(buffer);
		command_writer.start(reply_builder.get_buffer());
		command_reader.start(result_reply.get_buffer());
		state = WriteCommand;
	}

	bool write_command(Looper &looper, EventFlags flags)
	{
		if (flags & EVENT_IN)
		{
			auto ret = command_reader.process(*socket);
			// Received message before we completed the write, must be an error.
			if (command_reader.complete())
				return false;

			return (ret > 0) || (ret == Socket::ErrorWouldBlock);
		}
		else if (flags & EVENT_OUT)
		{
			auto ret = command_writer.process(*socket);
			if (command_writer.complete())
			{
				// Done writing, wait for reply.
				looper.modify_handler(EVENT_IN, *this);
				state = ReadReply;
			}

			return (ret > 0) || (ret == Socket::ErrorWouldBlock);
		}

		return true;
	}

	bool read_reply(Looper &)
	{
		auto ret = command_reader.process(*socket);
		if (command_reader.complete())
		{
			if (result_reply.read_u32() != NETFS_BEGIN_CHUNK_REPLY)
				return false;
			if (result_reply.read_u32() != NETFS_ERROR_OK)
				return false;
			if (result_reply.read_u64() != target_size)
				return false;

			LOGI("Write success!\n");
			return false;
		}

		return (ret > 0) || (ret == Socket::ErrorWouldBlock);
	}

	bool handle(Looper &looper, EventFlags flags) override
	{
		if (state == WriteCommand)
			return write_command(looper, flags);
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

	State state = WriteCommand;
	SocketReader command_reader;
	SocketWriter command_writer;
	ReplyBuilder reply_builder;
	ReplyBuilder result_reply;
	size_t target_size = 0;
};

struct FSReadCommand : LooperHandler
{
	virtual ~FSReadCommand() = default;

	FSReadCommand(const string &path, NetFSCommand command, unique_ptr<Socket> socket)
		: LooperHandler(move(socket))
	{
		reply_builder.begin();
		reply_builder.add_u32(command);
		reply_builder.add_u32(NETFS_BEGIN_CHUNK_REQUEST);
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

		return (ret > 0) || (ret == Socket::ErrorWouldBlock);
	}

	bool read_reply_size(Looper &)
	{
		auto ret = command_reader.process(*socket);
		if (command_reader.complete())
		{
			if (reply_builder.read_u32() != NETFS_BEGIN_CHUNK_REPLY)
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

		return (ret > 0) || (ret == Socket::ErrorWouldBlock);
	}

	bool read_reply(Looper &)
	{
		auto ret = command_reader.process(*socket);
		if (command_reader.complete())
		{
			parse_reply();
			return false;
		}

		return (ret > 0) || (ret == Socket::ErrorWouldBlock);
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

	virtual void parse_reply() = 0;
};

struct FSReader : FSReadCommand
{
	FSReader(const string &path, unique_ptr<Socket> socket)
		: FSReadCommand(path, NETFS_READ_FILE, move(socket))
	{
	}

	void parse_reply() override
	{
		LOGI("Read success!\n");
		fwrite(reply_builder.get_buffer().data(), reply_builder.get_buffer().size(), 1, stdout);
	}
};

struct FSList : FSReadCommand
{
	FSList(const string &path, unique_ptr<Socket> socket)
		: FSReadCommand(path, NETFS_LIST, move(socket))
	{
	}

	void parse_reply() override
	{
		uint32_t entries = reply_builder.read_u32();
		for (uint32_t i = 0; i < entries; i++)
		{
			auto path = reply_builder.read_string();
			auto type = reply_builder.read_u32();

			const char *file_type = "";
			switch (type)
			{
			case NETFS_FILE_TYPE_PLAIN:
				file_type = "plain";
				break;
			case NETFS_FILE_TYPE_DIRECTORY:
				file_type = "directory";
				break;
			case NETFS_FILE_TYPE_SPECIAL:
				file_type = "special";
				break;
			}
			LOGI("Path: %s (%s)\n", path.c_str(), file_type);
		}
		LOGI("List success!\n");
	}
};

struct FSStat : FSReadCommand
{
	FSStat(const string &path, unique_ptr<Socket> socket)
		: FSReadCommand(path, NETFS_STAT, move(socket))
	{
	}

	void parse_reply() override
	{
		uint64_t size = reply_builder.read_u64();
		uint32_t type = reply_builder.read_u32();
		const char *file_type = "";
		switch (type)
		{
		case NETFS_FILE_TYPE_PLAIN:
			file_type = "plain";
			break;
		case NETFS_FILE_TYPE_DIRECTORY:
			file_type = "directory";
			break;
		case NETFS_FILE_TYPE_SPECIAL:
			file_type = "special";
			break;
		}
		LOGI("File type: %s, size: %llu.\nStat success!\n", file_type, static_cast<unsigned long long>(size));
	}
};

struct FSWalk : FSReadCommand
{
	FSWalk(const string &path, unique_ptr<Socket> socket)
		: FSReadCommand(path, NETFS_WALK, move(socket))
	{
	}

	void parse_reply() override
	{
		uint32_t entries = reply_builder.read_u32();
		for (uint32_t i = 0; i < entries; i++)
		{
			auto path = reply_builder.read_string();
			auto type = reply_builder.read_u32();

			const char *file_type = "";
			switch (type)
			{
			case NETFS_FILE_TYPE_PLAIN:
				file_type = "plain";
				break;
			case NETFS_FILE_TYPE_DIRECTORY:
				file_type = "directory";
				break;
			case NETFS_FILE_TYPE_SPECIAL:
				file_type = "special";
				break;
			}
			LOGI("Path: %s (%s)\n", path.c_str(), file_type);
		}
		LOGI("Walk success!\n");
	}
};

int main(int argc, char *argv[])
{
	if (argc < 2)
		return 1;

	Looper looper;
	auto client = Socket::connect("127.0.0.1", 7070);
	if (!client)
		return 1;

#if 0
	looper.register_handler(EVENT_OUT, unique_ptr<FSReadCommand>(new FSReader(argv[1], move(client))));
	if (argc >= 3)
	{
		client = Socket::connect("127.0.0.1", 7070);
		looper.register_handler(EVENT_OUT, unique_ptr<FSReadCommand>(new FSWalk(argv[2], move(client))));
	}

	client = Socket::connect("127.0.0.1", 7070);
	looper.register_handler(EVENT_OUT | EVENT_IN, unique_ptr<FSWriteCommand>(new FSWriteCommand("assets://test.bin", { 0, 1, 2, 3, 4 }, move(client))));

	client = Socket::connect("127.0.0.1", 7070);
	looper.register_handler(EVENT_OUT, unique_ptr<FSReadCommand>(new FSStat(argv[1], move(client))));
#endif

	client = Socket::connect("127.0.0.1", 7070);
	looper.register_handler(EVENT_OUT, unique_ptr<FSNotifyCommand>(new FSNotifyCommand("assets", "notify.me", move(client))));

	while (looper.wait(-1) >= 0);
}