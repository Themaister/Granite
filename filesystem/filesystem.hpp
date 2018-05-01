/* Copyright (c) 2017-2018 Hans-Kristian Arntzen
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

#pragma once
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include "event.hpp"
#include <functional>
#include <stdio.h>

namespace Granite
{
class File
{
public:
	virtual ~File() = default;

	virtual void *map() = 0;

	virtual void *map_write(size_t size) = 0;

	virtual void unmap() = 0;

	virtual size_t get_size() = 0;

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
	uint64_t last_modified;
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
	FileNotifyHandle handle;
};

enum class FileMode
{
	ReadOnly,
	WriteOnly,
	ReadWrite
};

class StdioFile : public File
{
public:
	StdioFile(const std::string &path, FileMode mode);

	~StdioFile();

	void *map() override;

	void *map_write(size_t size) override;

	void unmap() override;

	size_t get_size() override;

	bool reopen() override;

private:
	FILE *file = nullptr;
	size_t size = 0;
	FileMode mode;
	std::vector<uint8_t> buffer;
};

class FilesystemBackend
{
public:
	virtual ~FilesystemBackend() = default;

	std::vector<ListEntry> walk(const std::string &path);

	virtual std::vector<ListEntry> list(const std::string &path) = 0;

	virtual std::unique_ptr<File> open(const std::string &path, FileMode mode = FileMode::ReadOnly) = 0;

	virtual bool stat(const std::string &path, FileStat &stat) = 0;

	virtual FileNotifyHandle
	install_notification(const std::string &path, std::function<void(const FileNotifyInfo &)> func) = 0;

	virtual void uninstall_notification(FileNotifyHandle handle) = 0;

	virtual void poll_notifications() = 0;

	virtual int get_notification_fd() const = 0;

	void set_protocol(const std::string &proto)
	{
		protocol = proto;
	}

protected:
	std::string protocol;
};

class FilesystemProtocolEvent : public Event
{
public:
	FilesystemProtocolEvent(const std::string &protocol, FilesystemBackend &backend)
			: protocol(protocol), backend(backend)
	{
	}

	GRANITE_EVENT_TYPE_DECL(FilesystemProtocolEvent)

	const std::string &get_protocol() const
	{
		return protocol;
	}

	FilesystemBackend &get_backend() const
	{
		return backend;
	}

private:
	std::string protocol;
	FilesystemBackend &backend;
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

	bool read_file_to_string(const std::string &path, std::string &str);
	bool write_string_to_file(const std::string &path, const std::string &str);
	bool write_buffer_to_file(const std::string &path, const void *data, size_t size);

	bool stat(const std::string &path, FileStat &stat);

	void poll_notifications();

	const std::unordered_map<std::string, std::unique_ptr<FilesystemBackend>> &get_protocols() const
	{
		return protocols;
	}

private:
	Filesystem();

	std::unordered_map<std::string, std::unique_ptr<FilesystemBackend>> protocols;
};

class ScratchFilesystem : public FilesystemBackend
{
public:
	std::vector<ListEntry> list(const std::string &path) override;

	std::unique_ptr<File> open(const std::string &path, FileMode mode = FileMode::ReadOnly) override;

	bool stat(const std::string &path, FileStat &stat) override;

	FileNotifyHandle install_notification(const std::string &path, std::function<void(const FileNotifyInfo &)> func) override;

	void uninstall_notification(FileNotifyHandle handle) override;

	void poll_notifications() override;

	int get_notification_fd() const override;

private:
	struct ScratchFile
	{
		std::vector<uint8_t> data;
	};
	std::unordered_map<std::string, std::unique_ptr<ScratchFile>> scratch_files;
};
}