#pragma once
#include <vector>
#include <string>
#include <memory>

namespace Granite
{
class File
{
public:
	virtual ~File() = default;

	virtual void *map() = 0;
	virtual void *map_write(size_t size) = 0;
	virtual void unmap() = 0;
	virtual size_t get_size() const = 0;
	virtual bool reopen() = 0;
};

class Filesystem
{
public:
	virtual ~Filesystem() = default;

	static Filesystem &get();

	enum class PathType
	{
		File,
		Directory,
		Special
	};

	struct Entry
	{
		std::string path;
		PathType type;
	};

	struct Stat
	{
		uint64_t size;
		PathType type;
	};

	using NotifyHandle = int;

	enum class NotifyType
	{
		FileChanged,
		FileDeleted,
		FileCreated,
	};

	struct NotifyInfo
	{
		std::string path;
		NotifyType type;
	};

	enum class Mode
	{
		ReadOnly,
		WriteOnly,
		ReadWrite
	};

	std::vector<Entry> walk(const std::string &path);
	virtual std::vector<Entry> list(const std::string &path) = 0;
	virtual std::unique_ptr<File> open(const std::string &path, Mode mode = Mode::ReadOnly) = 0;
	virtual bool stat(const std::string &path, Stat &stat) = 0;
	virtual NotifyHandle install_notification(const std::string &path, std::function<void (const NotifyInfo &)> func) = 0;
	virtual NotifyHandle find_notification(const std::string &path) const = 0;
	virtual void uninstall_notification(NotifyHandle handle) = 0;
	virtual void poll_notifications() = 0;
};
}