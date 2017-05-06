#pragma once
#include "fs-netfs.hpp"
#include "network.hpp"
#include "../filesystem.hpp"
#include "netfs.hpp"
#include <unordered_map>
#include <future>
#include <thread>

namespace Granite
{
class NetworkFile : public File
{
public:
	NetworkFile(Looper &looper, const std::string &path, FileMode mode);
	~NetworkFile();
	void *map() override;
	void *map_write(size_t size) override;
	void unmap() override;
	size_t get_size() const override;
	bool reopen() override;

	struct Result
	{
		std::vector<uint8_t> buffer;
		NetFSError error;
	};

private:
	std::future<Result> future;
	std::vector<uint8_t> buffer;
};

class NetworkFilesystem : public FilesystemBackend
{
public:
	NetworkFilesystem(const std::string &base);
	~NetworkFilesystem();
	std::vector<ListEntry> list(const std::string &path) override;
	std::unique_ptr<File> open(const std::string &path, FileMode mode) override;
	bool stat(const std::string &path, FileStat &stat) override;

	FileNotifyHandle install_notification(const std::string &, std::function<void (const FileNotifyInfo &)>) override
	{
		return -1;
	}

	FileNotifyHandle find_notification(const std::string &) const override
	{
		return -1;
	}

	void uninstall_notification(FileNotifyHandle) override
	{

	}

	void poll_notifications() override
	{
	}

	int get_notification_fd() const override
	{
		return -1;
	}

private:
	std::string base;
	std::thread looper_thread;
	Looper looper;
	void looper_entry();
};
}
