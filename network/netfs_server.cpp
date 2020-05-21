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

#include "network.hpp"
#include "logging.hpp"
#include "netfs.hpp"
#include "filesystem.hpp"
#include "event.hpp"
#include <unordered_set>
#include <queue>

using namespace Granite;
using namespace std;

struct FSHandler;

struct FilesystemHandler : LooperHandler
{
	FilesystemHandler(unique_ptr<Socket> socket_, FilesystemBackend &backend_)
		: LooperHandler(move(socket_)), backend(backend_)
	{
	}

	bool handle(Looper &, EventFlags flags) override
	{
		if (flags & EVENT_IN)
			Global::filesystem()->poll_notifications();

		return true;
	}

	FileNotifyHandle install_notification(const std::string &path, FSHandler *handler);
	void uninstall_notification(FSHandler *handler, FileNotifyHandle handle);
	void uninstall_all_notifications(FSHandler *handler);

	std::unordered_map<FSHandler *, std::unordered_set<FileNotifyHandle>> handler_to_handles;
	FilesystemBackend &backend;
};

struct NotificationSystem : EventHandler
{
	explicit NotificationSystem(Looper &looper_)
		: looper(looper_)
	{
		EVENT_MANAGER_REGISTER(NotificationSystem, on_filesystem, FilesystemProtocolEvent);
		for (auto &proto : Global::filesystem()->get_protocols())
		{
			auto &fs = proto.second;
			if (fs->get_notification_fd() >= 0)
			{
				auto socket = unique_ptr<Socket>(new Socket(fs->get_notification_fd(), false));
				auto handler = unique_ptr<FilesystemHandler>(new FilesystemHandler(move(socket), *fs));
				auto *ptr = handler.get();
				looper.register_handler(EVENT_IN, move(handler));
				protocols[proto.first] = ptr;
			}
		}
	}

	bool on_filesystem(const FilesystemProtocolEvent &fs)
	{
		if (fs.get_backend().get_notification_fd() >= 0)
		{
			auto socket = unique_ptr<Socket>(new Socket(fs.get_backend().get_notification_fd(), false));
			auto handler = unique_ptr<FilesystemHandler>(new FilesystemHandler(move(socket), fs.get_backend()));
			auto *ptr = handler.get();
			looper.register_handler(EVENT_IN, move(handler));
			protocols[fs.get_protocol()] = ptr;
		}
		return true;
	}

	void uninstall_all_notifications(FSHandler *handler)
	{
		for (auto &proto : protocols)
			proto.second->uninstall_all_notifications(handler);
	}

	FileNotifyHandle install_notification(FSHandler *handler, const string &protocol, const string &path)
	{
		auto *proto = protocols[protocol];
		if (!proto)
			return -1;

		return proto->install_notification(path, handler);
	}

	void uninstall_notification(FSHandler *handler, const string &protocol, FileNotifyHandle handle)
	{
		auto *proto = protocols[protocol];
		if (!proto)
			return;

		proto->uninstall_notification(handler, handle);
	}

	Looper &looper;
	std::unordered_map<std::string, FilesystemHandler *> protocols;
};

struct FSHandler : LooperHandler
{
	FSHandler(NotificationSystem &notify_system_, unique_ptr<Socket> socket_)
		: LooperHandler(move(socket_)), notify_system(notify_system_)
	{
		reply_builder.begin(4);
		command_reader.start(reply_builder.get_buffer());
		state = ReadCommand;
	}

	~FSHandler()
	{
		if (is_notify_fs)
		{
			LOGE("Tearing down Notification system ...\n");
			notify_system.uninstall_all_notifications(this);
		}
	}

	void notify(const FileNotifyInfo &info)
	{
		LOGI("Notification for path: %s\n", info.path.c_str());
		if (reply_queue.empty() && socket->get_parent_looper())
			socket->get_parent_looper()->modify_handler(EVENT_IN | EVENT_OUT, *this);

		reply_queue.emplace();
		auto &reply = reply_queue.back();
		reply.builder.add_u32(NETFS_BEGIN_CHUNK_NOTIFICATION);
		reply.builder.add_u32(NETFS_ERROR_OK);
		reply.builder.add_u64(info.path.size() + 8 + 8 + 4);
		reply.builder.add_string(info.path);
		reply.builder.add_u64(uint64_t(info.handle));

		switch (info.type)
		{
		case FileNotifyType::FileCreated:
			reply.builder.add_u32(NETFS_FILE_CREATED);
			break;
		case FileNotifyType::FileDeleted:
			reply.builder.add_u32(NETFS_FILE_DELETED);
			break;
		case FileNotifyType::FileChanged:
			reply.builder.add_u32(NETFS_FILE_CHANGED);
			break;
		}
		reply.writer.start(reply.builder.get_buffer());
	}

	bool parse_command(Looper &)
	{
		command_id = reply_builder.read_u32();

		if (command_id == NETFS_NOTIFICATION)
			is_notify_fs = true;

		switch (command_id)
		{
		case NETFS_WALK:
		case NETFS_LIST:
		case NETFS_READ_FILE:
		case NETFS_WRITE_FILE:
		case NETFS_STAT:
		case NETFS_NOTIFICATION:
			state = ReadChunkSize;
			reply_builder.begin(3 * sizeof(uint32_t));
			command_reader.start(reply_builder.get_buffer());
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
			if (reply_builder.read_u32() != NETFS_BEGIN_CHUNK_REQUEST)
			{
				LOGE("Failed in read_chunk_size().\n");
				return false;
			}

			uint64_t chunk_size = reply_builder.read_u64();
			if (!chunk_size)
			{
				LOGE("Got zero chunk_size in read_chunk_size().\n");
				return false;
			}

			reply_builder.begin(chunk_size);
			command_reader.start(reply_builder.get_buffer());
			state = ReadChunkData;
			return true;
		}

		return ret == Socket::ErrorWouldBlock;
	}

	bool read_chunk_data2(Looper &looper)
	{
		auto ret = command_reader.process(*socket);
		if (command_reader.complete())
		{
			reply_builder.begin();
			reply_builder.add_u32(NETFS_BEGIN_CHUNK_REPLY);
			reply_builder.add_u32(NETFS_ERROR_OK);
			reply_builder.add_u64(file->get_size());
			command_writer.start(reply_builder.get_buffer());
			state = WriteReplyChunk;
			looper.modify_handler(EVENT_OUT, *this);
			return true;
		}

		return (ret > 0) || (ret == Socket::ErrorWouldBlock);
	}

	bool read_chunk_size2(Looper &looper)
	{
		auto ret = command_reader.process(*socket);
		if (command_reader.complete())
		{
			if (reply_builder.read_u32() != NETFS_BEGIN_CHUNK_REQUEST)
			{
				LOGE("Got wrong request in read_chunk_size2().\n");
				return false;
			}

			uint64_t chunk_size = reply_builder.read_u64();
			if (!chunk_size)
			{
				LOGE("Got zero chunk size in read_chunk_size2().\n");
				return false;
			}

			mapped = file->map_write(chunk_size);
			if (!mapped)
			{
				reply_builder.begin();
				reply_builder.add_u32(NETFS_BEGIN_CHUNK_REPLY);
				reply_builder.add_u32(NETFS_ERROR_IO);
				reply_builder.add_u64(0);
				command_writer.start(reply_builder.get_buffer());
				state = WriteReplyChunk;
				looper.modify_handler(EVENT_OUT, *this);
			}
			else
			{
				reply_builder.begin(chunk_size);
				command_reader.start(mapped, chunk_size);
				state = ReadChunkData2;
			}
			return true;
		}

		return (ret > 0) || (ret == Socket::ErrorWouldBlock);
	}

	bool begin_write_file(Looper &looper, const string &arg)
	{
		file = Global::filesystem()->open(arg, FileMode::WriteOnly);
		if (!file)
		{
			reply_builder.begin();
			reply_builder.add_u32(NETFS_BEGIN_CHUNK_REPLY);
			reply_builder.add_u32(NETFS_ERROR_IO);
			reply_builder.add_u64(0);
			command_writer.start(reply_builder.get_buffer());
			state = WriteReplyChunk;
			looper.modify_handler(EVENT_OUT, *this);
		}
		else
		{
			reply_builder.begin(3 * sizeof(uint32_t));
			command_reader.start(reply_builder.get_buffer());
			state = ReadChunkSize2;
		}
		return true;
	}

	bool begin_read_file(const string &arg)
	{
		file = Global::filesystem()->open(arg);
		mapped = nullptr;
		if (file)
			mapped = file->map();

		reply_builder.begin();
		if (mapped)
		{
			reply_builder.add_u32(NETFS_BEGIN_CHUNK_REPLY);
			reply_builder.add_u32(NETFS_ERROR_OK);
			reply_builder.add_u64(file->get_size());
		}
		else
		{
			reply_builder.add_u32(NETFS_BEGIN_CHUNK_REPLY);
			reply_builder.add_u32(NETFS_ERROR_IO);
			reply_builder.add_u64(0);
		}
		command_writer.start(reply_builder.get_buffer());
		return true;
	}

	void write_string_list(const vector<ListEntry> &list)
	{
		reply_builder.begin();
		reply_builder.add_u32(NETFS_BEGIN_CHUNK_REPLY);
		reply_builder.add_u32(NETFS_ERROR_OK);
		auto offset = reply_builder.add_u64(0);
		reply_builder.add_u32(list.size());
		for (auto &l : list)
		{
			reply_builder.add_string(l.path);
			switch (l.type)
			{
			case PathType::File:
				reply_builder.add_u32(NETFS_FILE_TYPE_PLAIN);
				break;
			case PathType::Directory:
				reply_builder.add_u32(NETFS_FILE_TYPE_DIRECTORY);
				break;
			case PathType::Special:
				reply_builder.add_u32(NETFS_FILE_TYPE_SPECIAL);
				break;
			}
		}
		reply_builder.poke_u64(offset, reply_builder.get_buffer().size() - (offset + 8));
		command_writer.start(reply_builder.get_buffer());
	}

	bool begin_stat(const string &arg)
	{
		FileStat s;
		reply_builder.begin();
		reply_builder.add_u32(NETFS_BEGIN_CHUNK_REPLY);
		if (Global::filesystem()->stat(arg, s))
		{
			reply_builder.add_u32(NETFS_ERROR_OK);
			reply_builder.add_u64(8 + 4 + 8);
			reply_builder.add_u64(s.size);
			switch (s.type)
			{
			case PathType::File:
				reply_builder.add_u32(NETFS_FILE_TYPE_PLAIN);
				break;
			case PathType::Directory:
				reply_builder.add_u32(NETFS_FILE_TYPE_DIRECTORY);
				break;
			case PathType::Special:
				reply_builder.add_u32(NETFS_FILE_TYPE_SPECIAL);
				break;
			}
			reply_builder.add_u64(s.last_modified);
		}
		else
		{
			reply_builder.add_u32(NETFS_ERROR_IO);
			reply_builder.add_u64(0);
		}
		command_writer.start(reply_builder.get_buffer());
		return true;
	}

	bool begin_list(const string &arg)
	{
		auto list = Global::filesystem()->list(arg);
		write_string_list(list);
		return true;
	}

	bool begin_walk(const string &arg)
	{
		auto list = Global::filesystem()->walk(arg);
		write_string_list(list);
		return true;
	}

	bool read_chunk_data(Looper &looper)
	{
		auto ret = command_reader.process(*socket);
		if (command_reader.complete())
		{
			auto str = reply_builder.read_string_implicit_count();

			switch (command_id)
			{
			case NETFS_READ_FILE:
				looper.modify_handler(EVENT_OUT, *this);
				state = WriteReplyChunk;
				begin_read_file(str);
				break;

			case NETFS_WRITE_FILE:
				begin_write_file(looper, str);
				break;

			case NETFS_STAT:
				looper.modify_handler(EVENT_OUT, *this);
				state = WriteReplyChunk;
				begin_stat(str);
				break;

			case NETFS_LIST:
				looper.modify_handler(EVENT_OUT, *this);
				state = WriteReplyChunk;
				begin_list(str);
				break;

			case NETFS_WALK:
				looper.modify_handler(EVENT_OUT, *this);
				state = WriteReplyChunk;
				begin_walk(str);
				break;

			case NETFS_NOTIFICATION:
				protocol = move(str);
				looper.modify_handler(EVENT_IN, *this);
				reply_builder.begin(3 * sizeof(uint32_t));
				command_reader.start(reply_builder.get_buffer());
				state = NotificationLoop;
				break;

			default:
				return false;
			}

			return true;
		}

		return ret == Socket::ErrorWouldBlock;
	}

	bool read_command(Looper &looper)
	{
		auto ret = command_reader.process(*socket);
		if (command_reader.complete())
			return parse_command(looper);

		return (ret > 0) || (ret == Socket::ErrorWouldBlock);
	}

	bool write_reply_chunk(Looper &)
	{
		auto ret = command_writer.process(*socket);
		if (command_writer.complete())
		{
			switch (command_id)
			{
			case NETFS_READ_FILE:
				if (mapped)
				{
					command_writer.start(mapped, file->get_size());
					state = WriteReplyData;
					return true;
				}
				else
					return false;

			case NETFS_WRITE_FILE:
				if (file && mapped)
					file->unmap();
				return false;

			default:
				return false;
			}
		}

		return (ret > 0) || (ret == Socket::ErrorWouldBlock);
	}

	bool write_reply_data(Looper &)
	{
		auto ret = command_writer.process(*socket);
		if (command_writer.complete())
			return false;

		return (ret > 0) || (ret == Socket::ErrorWouldBlock);
	}

	bool notification_loop_register_notification(Looper &looper)
	{
		auto ret = command_reader.process(*socket);
		if (command_reader.complete())
		{
			auto path = reply_builder.read_string_implicit_count();
			auto handle = notify_system.install_notification(this, protocol, path);

			reply_queue.emplace();
			auto &reply = reply_queue.back();
			reply.builder.add_u32(NETFS_BEGIN_CHUNK_REPLY);
			reply.builder.add_u32(NETFS_ERROR_OK);
			reply.builder.add_u64(8);
			reply.builder.add_u64(uint64_t(handle));
			reply.writer.start(reply.builder.get_buffer());
			looper.modify_handler(EVENT_IN | EVENT_OUT, *this);

			reply_builder.begin(3 * sizeof(uint32_t));
			command_reader.start(reply_builder.get_buffer());
			state = NotificationLoop;
			return true;
		}

		return (ret > 0) || (ret == Socket::ErrorWouldBlock);
	}

	bool notification_loop_unregister_notification(Looper &looper)
	{
		auto ret = command_reader.process(*socket);
		if (command_reader.complete())
		{
			auto handle = reply_builder.read_u64();
			LOGI("Got unregister request for handle %d.\n", int(handle));
			notify_system.uninstall_notification(this, protocol, handle);

			reply_queue.emplace();
			auto &reply = reply_queue.back();
			reply.builder.add_u32(NETFS_BEGIN_CHUNK_REPLY);
			reply.builder.add_u32(NETFS_ERROR_OK);
			reply.builder.add_u64(0);
			reply.writer.start(reply.builder.get_buffer());
			looper.modify_handler(EVENT_IN | EVENT_OUT, *this);

			reply_builder.begin(3 * sizeof(uint32_t));
			command_reader.start(reply_builder.get_buffer());
			state = NotificationLoop;
			return true;
		}

		return (ret > 0) || (ret == Socket::ErrorWouldBlock);
	}

	void modify_looper(Looper &looper)
	{
		uint32_t mask = reply_queue.empty() ? EVENT_IN : (EVENT_IN | EVENT_OUT);
		looper.modify_handler(mask, *this);
	}

	bool notification_loop(Looper &looper, EventFlags flags)
	{
		if (flags & EVENT_IN)
		{
			auto ret = command_reader.process(*socket);
			if (command_reader.complete())
			{
				auto cmd = reply_builder.read_u32();
				if (cmd == NETFS_REGISTER_NOTIFICATION)
				{
					auto size = reply_builder.read_u64();
					state = NotificationLoopRegister;
					reply_builder.begin(size);
					command_reader.start(reply_builder.get_buffer());
					looper.modify_handler(EVENT_IN, *this);
					return true;
				}
				else if (cmd == NETFS_UNREGISTER_NOTIFICATION)
				{
					LOGI("Got unregister request.\n");
					auto size = reply_builder.read_u64();
					state = NotificationLoopUnregister;
					reply_builder.begin(size);
					command_reader.start(reply_builder.get_buffer());
					looper.modify_handler(EVENT_IN, *this);
					return true;
				}
				else
				{
					LOGE("Wrong request type %u in notification loop.\n", cmd);
					return false;
				}
			}

			return (ret > 0) || (ret == Socket::ErrorWouldBlock);
		}

		if (flags & EVENT_OUT)
		{
			if (reply_queue.empty())
			{
				looper.modify_handler(EVENT_IN, *this);
				return true;
			}

			auto ret = reply_queue.front().writer.process(*socket);
			if (reply_queue.front().writer.complete())
				reply_queue.pop();

			if (reply_queue.empty())
			{
				looper.modify_handler(EVENT_IN, *this);
				return true;
			}
			else
				return (ret > 0) || (ret == Socket::ErrorWouldBlock);
		}

		return true;
	}

	bool handle(Looper &looper, EventFlags flags) override
	{
		if (state == ReadCommand)
			return read_command(looper);
		else if (state == ReadChunkSize)
			return read_chunk_size(looper);
		else if (state == ReadChunkData)
			return read_chunk_data(looper);
		else if (state == ReadChunkSize2)
			return read_chunk_size2(looper);
		else if (state == ReadChunkData2)
			return read_chunk_data2(looper);
		else if (state == WriteReplyChunk)
			return write_reply_chunk(looper);
		else if (state == WriteReplyData)
			return write_reply_data(looper);
		else if (state == NotificationLoop)
			return notification_loop(looper, flags);
		else if (state == NotificationLoopRegister)
			return notification_loop_register_notification(looper);
		else if (state == NotificationLoopUnregister)
			return notification_loop_unregister_notification(looper);
		else
			return false;
	}

	enum State
	{
		ReadCommand,
		ReadChunkSize,
		ReadChunkData,
		ReadChunkSize2,
		ReadChunkData2,
		WriteReplyChunk,
		WriteReplyData,
		NotificationLoop,
		NotificationLoopRegister,
		NotificationLoopUnregister
	};

	NotificationSystem &notify_system;
	State state = ReadCommand;
	SocketReader command_reader;
	SocketWriter command_writer;
	ReplyBuilder reply_builder;
	uint32_t command_id = 0;

	struct NotificationReply
	{
		SocketWriter writer;
		ReplyBuilder builder;
	};
	std::queue<NotificationReply> reply_queue;
	std::string protocol;

	unique_ptr<File> file;
	void *mapped = nullptr;

	bool is_notify_fs = false;
};

FileNotifyHandle FilesystemHandler::install_notification(const std::string &path, FSHandler *handler)
{
	auto handle = backend.install_notification(path, [=](const FileNotifyInfo &info) {
		handler->notify(info);
	});

	if (handle >= 0)
		handler_to_handles[handler].insert(handle);
	return handle;
}

void FilesystemHandler::uninstall_notification(FSHandler *handler, FileNotifyHandle handle)
{
	auto &handles = handler_to_handles[handler];
	if (handles.count(handle))
	{
		backend.uninstall_notification(handle);
		handles.erase(handles.find(handle));
	}
}

void FilesystemHandler::uninstall_all_notifications(FSHandler *handler)
{
	auto &handles = handler_to_handles[handler];
	for (auto &handle : handles)
		backend.uninstall_notification(handle);
	handles.clear();
	handler_to_handles.erase(handler_to_handles.find(handler));
}

struct ListenerHandler : TCPListener
{
	ListenerHandler(NotificationSystem &notify_system_, uint16_t port)
		: TCPListener(port), notify_system(notify_system_)
	{
	}

	bool handle(Looper &looper, EventFlags) override
	{
		auto client = accept();
		if (client)
			looper.register_handler(EVENT_IN, unique_ptr<FSHandler>(new FSHandler(notify_system, move(client))));
		return true;
	}

	NotificationSystem &notify_system;
};

int main()
{
	Looper looper;
	auto notify = unique_ptr<NotificationSystem>(new NotificationSystem(looper));
	auto listener = unique_ptr<LooperHandler>(new ListenerHandler(*notify, 7070));

	looper.register_handler(EVENT_IN, move(listener));
	while (looper.wait(-1) >= 0);
}
