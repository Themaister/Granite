#pragma once
#include "filesystem.hpp"
#include <unordered_map>

namespace Granite
{
class MMapFile : public File
{
public:
	MMapFile(const std::string &path, Filesystem::Mode mode);
	~MMapFile();
	void *map() override;
	void *map_write(size_t size) override;
	void unmap() override;
	size_t get_size() const override;
	bool reopen() override;

private:
	int fd = -1;
	void *mapped = nullptr;
	size_t size = 0;
};

class OSFilesystem : public Filesystem
{
public:
	OSFilesystem(const std::string &base);
	~OSFilesystem();
	std::vector<Entry> list(const std::string &path) override;
	std::unique_ptr<File> open(const std::string &path, Mode mode) override;
	bool stat(const std::string &path, Stat &stat) override;
	NotifyHandle install_notification(const std::string &path, std::function<void (const Filesystem::NotifyInfo &)> func) override;
	NotifyHandle find_notification(const std::string &path) const override;
	void uninstall_notification(NotifyHandle handle) override;
	void poll_notifications() override;

private:
	std::string base;

	struct Handler
	{
		std::string path;
		std::function<void (const Filesystem::NotifyInfo &)> func;
		bool directory;
	};
	std::unordered_map<NotifyHandle, Handler> handlers;
	std::unordered_map<std::string, NotifyHandle> path_to_handler;
	int notify_fd;
};
}