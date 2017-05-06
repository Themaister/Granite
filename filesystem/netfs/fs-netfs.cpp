#include "fs-netfs.hpp"
#include "../path.hpp"
#include "util.hpp"

using namespace std;

namespace Granite
{
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

	~FSList()
	{
		if (!got_reply)
			result.set_exception(make_exception_ptr(runtime_error("List failed")));
	}

	void parse_reply() override
	{
		uint32_t entries = reply_builder.read_u32();
		vector<ListEntry> list;
		for (uint32_t i = 0; i < entries; i++)
		{
			auto path = reply_builder.read_string();
			auto type = reply_builder.read_u32();

			switch (type)
			{
			case NETFS_FILE_TYPE_PLAIN:
				list.push_back({ move(path), PathType::File });
				break;
			case NETFS_FILE_TYPE_DIRECTORY:
				list.push_back({ move(path), PathType::Directory });
				break;
			case NETFS_FILE_TYPE_SPECIAL:
				list.push_back({ move(path), PathType::Special });
				break;
			}
		}
		result.set_value(move(list));
		got_reply = true;
	}

	promise<vector<ListEntry>> result;
	bool got_reply = false;
};

struct FSStat : FSReadCommand
{
	FSStat(const string &path, unique_ptr<Socket> socket)
		: FSReadCommand(path, NETFS_STAT, move(socket))
	{
	}

	~FSStat()
	{
		// Throw exception instead in calling thread.
		if (!got_reply)
			result.set_exception(make_exception_ptr(runtime_error("Failed stat")));
	}

	void parse_reply() override
	{
		uint64_t size = reply_builder.read_u64();
		uint32_t type = reply_builder.read_u32();
		FileStat s;
		s.size = size;

		switch (type)
		{
		case NETFS_FILE_TYPE_PLAIN:
			s.type = PathType::File;
			break;
		case NETFS_FILE_TYPE_DIRECTORY:
			s.type = PathType::Directory;
			break;
		case NETFS_FILE_TYPE_SPECIAL:
			s.type = PathType::Special;
			break;
		}

		result.set_value(s);
		got_reply = true;
	}

	std::promise<FileStat> result;
	bool got_reply = false;
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

NetworkFilesystem::NetworkFilesystem(const std::string &base)
	: base(base)
{
	looper_thread = thread(&NetworkFilesystem::looper_entry, this);
}

void NetworkFilesystem::looper_entry()
{
	while (looper.wait_idle(-1) >= 0);
}

vector<ListEntry> NetworkFilesystem::list(const std::string &path)
{
	auto joined = protocol + "://" + path;
	auto socket = Socket::connect("127.0.0.1", 7070);
	if (!socket)
		return {};

	unique_ptr<FSList> handler(new FSList(joined, move(socket)));
	auto fut = handler->result.get_future();

	looper.run_in_looper([&]() {
		looper.register_handler(EVENT_OUT, move(handler));
	});

	try
	{
		return fut.get();
	}
	catch (...)
	{
		return {};
	}
}

unique_ptr<File> NetworkFilesystem::open(const std::string &, FileMode)
{
	return {};
}

bool NetworkFilesystem::stat(const std::string &path, FileStat &stat)
{
	auto joined = protocol + "://" + path;
	auto socket = Socket::connect("127.0.0.1", 7070);
	if (!socket)
		return false;

	unique_ptr<FSStat> handler(new FSStat(joined, move(socket)));
	auto fut = handler->result.get_future();

	looper.run_in_looper([&]() {
		looper.register_handler(EVENT_OUT, move(handler));
	});

	try
	{
		stat = fut.get();
		return true;
	}
	catch (...)
	{
		return false;
	}
}

NetworkFilesystem::~NetworkFilesystem()
{
	looper.kill();
	if (looper_thread.joinable())
		looper_thread.join();
}
}