#pragma once
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>

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

enum class PathType
{
	File,
	Directory,
	Special
};

struct ListEntry
{
	std::string path;
	PathType type;
};

struct FileStat
{
	uint64_t size;
	PathType type;
};

using FileNotifyHandle = int;

enum class FileNotifyType
{
	FileChanged,
	FileDeleted,
	FileCreated,
};

struct FileNotifyInfo
{
	std::string path;
	FileNotifyType type;
};

enum class FileMode
{
	ReadOnly,
	WriteOnly,
	ReadWrite
};

class FilesystemBackend
{
public:
	virtual ~FilesystemBackend() = default;

	std::vector<ListEntry> walk(const std::string &path);
	virtual std::vector<ListEntry> list(const std::string &path) = 0;
	virtual std::unique_ptr<File> open(const std::string &path, FileMode mode = FileMode::ReadOnly) = 0;
	virtual bool stat(const std::string &path, FileStat &stat) = 0;
	virtual FileNotifyHandle install_notification(const std::string &path, std::function<void (const FileNotifyInfo &)> func) = 0;
	virtual FileNotifyHandle find_notification(const std::string &path) const = 0;
	virtual void uninstall_notification(FileNotifyHandle handle) = 0;
	virtual void poll_notifications() = 0;

private:
};

class Filesystem
{
public:
	static Filesystem &get();
	void register_protocol(const std::string &proto, std::unique_ptr<FilesystemBackend> fs);
	FilesystemBackend *get_backend(const std::string &proto);

	std::vector<ListEntry> walk(const std::string &path);
	std::vector<ListEntry> list(const std::string &path);
	std::unique_ptr<File> open(const std::string &path, FileMode mode = FileMode::ReadOnly);
	bool stat(const std::string &path, FileStat &stat);
	void poll_notifications();

private:
	Filesystem();
	std::unordered_map<std::string, std::unique_ptr<FilesystemBackend>> protocols;
};
}