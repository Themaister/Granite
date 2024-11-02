/* Copyright (c) 2017-2024 Hans-Kristian Arntzen
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
#include <functional>
#include <stdio.h>
#include "global_managers.hpp"
#include "intrusive.hpp"

namespace Granite
{
class FileMapping;

class File : public Util::ThreadSafeIntrusivePtrEnabled<File>
{
public:
	virtual ~File() = default;
	virtual Util::IntrusivePtr<FileMapping> map_subset(uint64_t offset, size_t range) = 0;
	virtual Util::IntrusivePtr<FileMapping> map_write(size_t size) = 0;
	virtual uint64_t get_size() = 0;

	// Only called by FileMapping.
	virtual void unmap(void *mapped, size_t range) = 0;

	Util::IntrusivePtr<FileMapping> map();
};
using FileHandle = Util::IntrusivePtr<File>;

class FileMapping : public Util::ThreadSafeIntrusivePtrEnabled<FileMapping>
{
public:
	template <typename T = void>
	inline const T *data() const
	{
		void *ptr = static_cast<uint8_t *>(mapped) + map_offset;
		return static_cast<const T *>(ptr);
	}

	template <typename T = void>
	inline T *mutable_data()
	{
		void *ptr = static_cast<uint8_t *>(mapped) + map_offset;
		return static_cast<T *>(ptr);
	}

	uint64_t get_file_offset() const;
	uint64_t get_size() const;

	~FileMapping();
	FileMapping(FileHandle handle,
	            uint64_t file_offset,
	            void *mapped, size_t mapped_size,
	            size_t map_offset, size_t accessible_size);

private:
	FileHandle handle;
	uint64_t file_offset;
	void *mapped;
	size_t mapped_size;
	// For non-page aligned maps.
	size_t map_offset;
	size_t accessible_size;
};
using FileMappingHandle = Util::IntrusivePtr<FileMapping>;

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
	ReadWrite,
	WriteOnlyTransactional
};

class FilesystemBackend
{
public:
	virtual ~FilesystemBackend() = default;
	std::vector<ListEntry> walk(const std::string &path);
	virtual std::vector<ListEntry> list(const std::string &path) = 0;
	virtual FileHandle open(const std::string &path, FileMode mode = FileMode::ReadOnly) = 0;
	virtual bool stat(const std::string &path, FileStat &stat) = 0;

	virtual FileNotifyHandle
	install_notification(const std::string &path, std::function<void(const FileNotifyInfo &)> func) = 0;

	virtual void uninstall_notification(FileNotifyHandle handle) = 0;
	virtual void poll_notifications() = 0;
	virtual int get_notification_fd() const = 0;

	virtual bool remove(const std::string &path);
	virtual bool move_replace(const std::string &dst, const std::string &src);
	virtual bool move_yield(const std::string &dst, const std::string &src);

	inline virtual std::string get_filesystem_path(const std::string &)
	{
		return "";
	}

	void set_protocol(const std::string &proto)
	{
		protocol = proto;
	}

protected:
	std::string protocol;
};

class Filesystem final : public FilesystemInterface
{
public:
	Filesystem();

	void register_protocol(const std::string &proto, std::unique_ptr<FilesystemBackend> fs);
	FilesystemBackend *get_backend(const std::string &proto);
	std::vector<ListEntry> walk(const std::string &path);
	std::vector<ListEntry> list(const std::string &path);
	FileHandle open(const std::string &path, FileMode mode = FileMode::ReadOnly);
	std::string get_filesystem_path(const std::string &path);

	bool read_file_to_string(const std::string &path, std::string &str);
	bool write_string_to_file(const std::string &path, const std::string &str);
	bool write_buffer_to_file(const std::string &path, const void *data, size_t size);
	FileMappingHandle open_readonly_mapping(const std::string &path);
	FileMappingHandle open_writeonly_mapping(const std::string &path, size_t size);
	FileMappingHandle open_transactional_mapping(const std::string &path, size_t size);

	bool remove(const std::string &path);
	bool move_replace(const std::string &dst, const std::string &src);
	bool move_yield(const std::string &dst, const std::string &src);

	bool stat(const std::string &path, FileStat &stat);

	void poll_notifications();

	const std::unordered_map<std::string, std::unique_ptr<FilesystemBackend>> &get_protocols() const
	{
		return protocols;
	}

	static void setup_default_filesystem(Filesystem *fs, const char *default_asset_directory);

private:
	std::unordered_map<std::string, std::unique_ptr<FilesystemBackend>> protocols;

	bool load_text_file(const std::string &path, std::string &str) override;
};

class ScratchFilesystem final : public FilesystemBackend
{
public:
	std::vector<ListEntry> list(const std::string &path) override;

	FileHandle open(const std::string &path, FileMode mode = FileMode::ReadOnly) override;

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

class ConstantMemoryFile final : public Granite::File
{
public:
	ConstantMemoryFile(const void *mapped_, size_t size_)
		: mapped(static_cast<const uint8_t *>(mapped_)), size(size_)
	{
	}

	FileMappingHandle map_subset(uint64_t offset, size_t range) override
	{
		if (offset + range > size)
			return {};

		return Util::make_handle<FileMapping>(
			FileHandle{}, offset,
			const_cast<uint8_t *>(mapped) + offset, range,
		    0, range);
	}

	FileMappingHandle map_write(size_t) override
	{
		return {};
	}

	void unmap(void *, size_t) override
	{
	}

	uint64_t get_size() override
	{
		return size;
	}

private:
	const uint8_t *mapped;
	size_t size;
};

class FileSlice final : public File
{
public:
	FileSlice(FileHandle handle, uint64_t offset, uint64_t range);
	FileMappingHandle map_subset(uint64_t offset, size_t range) override;
	FileMappingHandle map_write(size_t) override;
	void unmap(void *, size_t) override;
	uint64_t get_size() override;

private:
	FileHandle handle;
	uint64_t offset;
	uint64_t range;
};

class BlobFilesystem final : public FilesystemBackend
{
public:
	BlobFilesystem(FileHandle file);

	std::vector<ListEntry> list(const std::string &path) override;

	FileHandle open(const std::string &path, FileMode mode) override;

	bool stat(const std::string &path, FileStat &stat) override;

	FileNotifyHandle install_notification(const std::string &path, std::function<void(const FileNotifyInfo &)> func) override;

	void uninstall_notification(FileNotifyHandle handle) override;

	void poll_notifications() override;

	int get_notification_fd() const override;

private:
	FileHandle file;
	size_t blob_base_offset = 0;

	struct BlobFile
	{
		std::string path;
		size_t offset;
		size_t size;
	};

	struct Directory
	{
		std::string path;
		std::vector<std::unique_ptr<Directory>> dirs;
		std::vector<BlobFile> files;
	};
	std::unique_ptr<Directory> root;
	BlobFile *find_file(const std::string &path);
	Directory *find_directory(const std::string &path);
	Directory *make_directory(const std::string &path);
	void parse();

	static uint8_t read_u8(const uint8_t *&buf, size_t &size);
	static uint64_t read_u64(const uint8_t *&buf, size_t &size);
	static std::string read_string(const uint8_t *&buf, size_t &size, size_t len);
	void add_entry(const std::string &path, size_t offset, size_t size);
};

}