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

#define NOMINMAX
#include "filesystem.hpp"
#include "path_utils.hpp"
#include "logging.hpp"
#include "os_filesystem.hpp"
#include "string_helpers.hpp"
#include "environment.hpp"
#include <algorithm>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

namespace Granite
{
std::vector<ListEntry> FilesystemBackend::walk(const std::string &path)
{
	auto entries = list(path);
	std::vector<ListEntry> final_entries;
	for (auto &e : entries)
	{
		if (e.type == PathType::Directory)
		{
			auto subentries = walk(e.path);
			final_entries.push_back(std::move(e));
			for (auto &sub : subentries)
				final_entries.push_back(std::move(sub));
		}
		else if (e.type == PathType::File)
			final_entries.push_back(std::move(e));
	}
	return final_entries;
}

bool FilesystemBackend::remove(const std::string &)
{
	return false;
}

bool FilesystemBackend::move_replace(const std::string &, const std::string &)
{
	return false;
}

bool FilesystemBackend::move_yield(const std::string &, const std::string &)
{
	return false;
}

Filesystem::Filesystem()
{
	register_protocol("file", std::unique_ptr<FilesystemBackend>(new OSFilesystem(".")));
	register_protocol("memory", std::unique_ptr<FilesystemBackend>(new ScratchFilesystem));

#ifdef GRANITE_DEFAULT_ASSET_DIRECTORY
	auto asset_dir = Util::get_environment_string("GRANITE_DEFAULT_ASSET_DIRECTORY", GRANITE_DEFAULT_ASSET_DIRECTORY);
#else
	auto asset_dir = Util::get_environment_string("GRANITE_DEFAULT_ASSET_DIRECTORY", "");
#endif
	if (!asset_dir.empty())
		register_protocol("builtin", std::unique_ptr<FilesystemBackend>(new OSFilesystem(asset_dir)));

#ifdef GRANITE_DEFAULT_BUILTIN_DIRECTORY
	auto builtin_dir = Util::get_environment_string("GRANITE_DEFAULT_BUILTIN_DIRECTORY", GRANITE_DEFAULT_BUILTIN_DIRECTORY);
#else
	auto builtin_dir = Util::get_environment_string("GRANITE_DEFAULT_BUILTIN_DIRECTORY", "");
#endif
	if (!builtin_dir.empty())
		register_protocol("builtin", std::unique_ptr<FilesystemBackend>(new OSFilesystem(builtin_dir)));

#ifdef GRANITE_DEFAULT_CACHE_DIRECTORY
	auto cache_dir = Util::get_environment_string("GRANITE_DEFAULT_CACHE_DIRECTORY", GRANITE_DEFAULT_CACHE_DIRECTORY);
#else
	auto cache_dir = Util::get_environment_string("GRANITE_DEFAULT_CACHE_DIRECTORY", "");
#endif
	if (!cache_dir.empty())
		register_protocol("cache", std::unique_ptr<FilesystemBackend>(new OSFilesystem(cache_dir)));
}

void Filesystem::setup_default_filesystem(Filesystem *filesystem, const char *default_asset_directory)
{
	auto self_dir = Path::basedir(Path::get_executable_path());
	auto assets_dir = Path::join(self_dir, "assets");
	auto builtin_dir = Path::join(self_dir, "builtin/assets");

	if (default_asset_directory)
	{
#ifdef GRANITE_SHIPPING
		LOGW("Default asset directory %s was provided, but this is only intended for non-shipping configs.\n",
		     default_asset_directory);
#else
		filesystem->register_protocol("assets",
		                              std::unique_ptr<FilesystemBackend>(new OSFilesystem(default_asset_directory)));
#endif
	}

	FileStat s;
	if (filesystem->stat(assets_dir, s) && s.type == PathType::Directory)
	{
		filesystem->register_protocol("assets", std::make_unique<OSFilesystem>(assets_dir));
		LOGI("Redirecting filesystem \"assets\" to %s.\n", assets_dir.c_str());

		auto cache_dir = Path::join(self_dir, "cache");
		filesystem->register_protocol("cache", std::make_unique<OSFilesystem>(cache_dir));
		LOGI("Redirecting filesystem \"cache\" to %s.\n", cache_dir.c_str());
	}

	if (filesystem->stat(builtin_dir, s) && s.type == PathType::Directory)
	{
		filesystem->register_protocol("builtin", std::make_unique<OSFilesystem>(builtin_dir));
		LOGI("Redirecting filesystem \"builtin\" to %s.\n", builtin_dir.c_str());
	}

	// These filesystems are core functionality.
	if (!filesystem->get_backend("builtin"))
		throw std::runtime_error("builtin filesystem was not initialized.");
	if (!filesystem->get_backend("cache"))
		throw std::runtime_error("cache filesystem was not initialized.");
}

void Filesystem::register_protocol(const std::string &proto, std::unique_ptr<FilesystemBackend> fs)
{
	if (fs)
	{
		fs->set_protocol(proto);
		protocols[proto] = std::move(fs);
	}
	else
		protocols.erase(proto);
}

FilesystemBackend *Filesystem::get_backend(const std::string &proto)
{
	auto itr = protocols.find(proto);
	if (proto.empty())
		itr = protocols.find("file");

	if (itr != end(protocols))
		return itr->second.get();
	else
		return nullptr;
}

std::vector<ListEntry> Filesystem::walk(const std::string &path)
{
	auto paths = Path::protocol_split(path);
	auto *backend = get_backend(paths.first);
	if (!backend)
		return {};

	return backend->walk(paths.second);
}

std::vector<ListEntry> Filesystem::list(const std::string &path)
{
	auto paths = Path::protocol_split(path);
	auto *backend = get_backend(paths.first);
	if (!backend)
		return {};

	return backend->list(paths.second);
}

bool Filesystem::remove(const std::string &path)
{
	auto paths = Path::protocol_split(path);
	auto *backend = get_backend(paths.first);
	if (!backend)
		return false;

	return backend->remove(paths.second);
}

bool Filesystem::move_yield(const std::string &dst, const std::string &src)
{
	auto paths_dst = Path::protocol_split(dst);
	auto paths_src = Path::protocol_split(src);
	auto *backend_dst = get_backend(paths_dst.first);
	auto *backend_src = get_backend(paths_src.first);
	if (!backend_dst || !backend_src || backend_dst != backend_src)
		return false;

	return backend_dst->move_yield(paths_dst.second, paths_src.second);
}

bool Filesystem::move_replace(const std::string &dst, const std::string &src)
{
	auto paths_dst = Path::protocol_split(dst);
	auto paths_src = Path::protocol_split(src);
	auto *backend_dst = get_backend(paths_dst.first);
	auto *backend_src = get_backend(paths_src.first);
	if (!backend_dst || !backend_src || backend_dst != backend_src)
		return false;

	return backend_dst->move_replace(paths_dst.second, paths_src.second);
}

FileMappingHandle Filesystem::open_readonly_mapping(const std::string &path)
{
	auto file = open(path, FileMode::ReadOnly);
	if (!file)
		return {};
	return file->map();
}

FileMappingHandle Filesystem::open_writeonly_mapping(const std::string &path, size_t size)
{
	auto file = open(path, FileMode::WriteOnly);
	if (!file)
		return {};
	return file->map_write(size);
}

FileMappingHandle Filesystem::open_transactional_mapping(const std::string &path, size_t size)
{
	auto file = open(path, FileMode::WriteOnlyTransactional);
	if (!file)
		return {};
	return file->map_write(size);
}

bool Filesystem::read_file_to_string(const std::string &path, std::string &str)
{
	auto mapping = open_readonly_mapping(path);
	if (!mapping)
		return false;

	auto size = mapping->get_size();
	str = std::string(mapping->data<char>(), mapping->data<char>() + size);

	// Remove DOS EOL.
	str.erase(remove_if(begin(str), end(str), [](char c) { return c == '\r'; }), end(str));

	return true;
}

bool Filesystem::write_buffer_to_file(const std::string &path, const void *data, size_t size)
{
	auto file = open_transactional_mapping(path, size);
	if (!file)
		return false;
	memcpy(file->mutable_data(), data, size);
	return true;
}

bool Filesystem::write_string_to_file(const std::string &path, const std::string &str)
{
	return write_buffer_to_file(path, str.data(), str.size());
}

FileHandle Filesystem::open(const std::string &path, FileMode mode)
{
	auto paths = Path::protocol_split(path);
	auto *backend = get_backend(paths.first);
	if (!backend)
		return {};

	auto file = backend->open(paths.second, mode);
	return file;
}

std::string Filesystem::get_filesystem_path(const std::string &path)
{
	auto paths = Path::protocol_split(path);
	auto *backend = get_backend(paths.first);
	if (!backend)
		return "";

	return backend->get_filesystem_path(paths.second);
}

bool Filesystem::stat(const std::string &path, FileStat &stat)
{
	auto paths = Path::protocol_split(path);
	auto *backend = get_backend(paths.first);
	if (!backend)
		return false;

	return backend->stat(paths.second, stat);
}

void Filesystem::poll_notifications()
{
	for (auto &proto : protocols)
		proto.second->poll_notifications();
}

bool Filesystem::load_text_file(const std::string &path, std::string &str)
{
	return read_file_to_string(path, str);
}

int ScratchFilesystem::get_notification_fd() const
{
	return -1;
}

FileNotifyHandle ScratchFilesystem::install_notification(const std::string &,
                                                         std::function<void(const FileNotifyInfo &)>)
{
	return -1;
}

void ScratchFilesystem::poll_notifications()
{
}

void ScratchFilesystem::uninstall_notification(FileNotifyHandle)
{
}

bool ScratchFilesystem::stat(const std::string &path, FileStat &stat)
{
	auto itr = scratch_files.find(path);
	if (itr == end(scratch_files))
		return false;

	stat.size = itr->second->data.size();
	stat.type = PathType::File;
	return true;
}

std::vector<ListEntry> ScratchFilesystem::list(const std::string &)
{
	return {};
}

struct ScratchFilesystemFile final : File
{
	explicit ScratchFilesystemFile(std::vector<uint8_t> &data_)
		: data(data_)
	{
	}

	FileMappingHandle map_subset(uint64_t offset, size_t range) override
	{
		if (offset + range > data.size())
			return {};

		return Util::make_handle<FileMapping>(
		    FileHandle{}, offset,
			data.data() + offset, range,
			0, range);
	}

	FileMappingHandle map_write(size_t size) override
	{
		data.resize(size);
		return map_subset(0, size);
	}

	void unmap(void *, size_t) override
	{
	}

	uint64_t get_size() override
	{
		return data.size();
	}

	std::vector<uint8_t> &data;
};

FileHandle ScratchFilesystem::open(const std::string &path, FileMode)
{
	auto itr = scratch_files.find(path);
	if (itr == end(scratch_files))
	{
		auto &file = scratch_files[path];
		file = std::make_unique<ScratchFile>();
		return Util::make_handle<ScratchFilesystemFile>(file->data);
	}
	else
	{
		return Util::make_handle<ScratchFilesystemFile>(itr->second->data);
	}
}

BlobFilesystem::BlobFilesystem(FileHandle file_)
	: file(std::move(file_))
{
	if (!file)
		return;

	root = std::make_unique<Directory>();
	parse();
}

uint8_t BlobFilesystem::read_u8(const uint8_t *&buf, size_t &size)
{
	if (size < 1)
		throw std::range_error("Blob EOF.");

	uint8_t ret = *buf++;
	size--;
	return ret;
}

uint64_t BlobFilesystem::read_u64(const uint8_t *&buf, size_t &size)
{
	if (size < 8)
		throw std::range_error("Blob EOF.");

	uint64_t ret = 0;
	for (unsigned i = 0; i < 8; i++)
		ret |= uint64_t(buf[i]) << (8 * i);
	size -= 8;
	buf += 8;
	return ret;
}

std::string BlobFilesystem::read_string(const uint8_t *&buf, size_t &size, size_t len)
{
	if (size < len)
		throw std::range_error("Blob EOF.");

	std::string ret;
	ret.insert(ret.end(), reinterpret_cast<const char *>(buf), reinterpret_cast<const char *>(buf) + len);
	size -= len;
	buf += len;
	return ret;
}

void BlobFilesystem::add_entry(const std::string &path, size_t offset, size_t size)
{
	auto paths = Path::split(path);
	auto *dir = find_directory(paths.first);
	if (!dir)
		dir = make_directory(paths.first);
	dir->files.push_back({ Path::basename(path), offset, size });
}

void BlobFilesystem::parse()
{
	size_t mapped_size = file->get_size();
	if (mapped_size < 16)
		throw std::runtime_error("Blob archive too small.");

	auto mapped_handle = file->map();
	if (!mapped_handle)
		throw std::runtime_error("Failed to map blob archive.");

	auto *base_mapped = mapped_handle->data<uint8_t>();
	auto *mapped = base_mapped;

	if (memcmp(mapped, "BLOBBY01", 8) != 0)
		throw std::runtime_error("Invalid magic.");
	mapped += 8;
	mapped_size -= 8;

	uint64_t required_size = 0;

	while (mapped_size >= 4 && memcmp(mapped, "ENTR", 4) == 0)
	{
		mapped += 4;
		mapped_size -= 4;

		uint8_t len = read_u8(mapped, mapped_size);
		std::string path = Path::canonicalize_path(read_string(mapped, mapped_size, len));
		uint64_t blob_offset = read_u64(mapped, mapped_size);
		uint64_t blob_size = read_u64(mapped, mapped_size);
		required_size = std::max(required_size, blob_offset + blob_size);

		if (blob_offset + blob_size < blob_offset)
			throw std::range_error("Overflow for blob offset + size.");

		if (blob_offset > SIZE_MAX || blob_size > SIZE_MAX)
			throw std::range_error("Blob offset out of range.");

		add_entry(path, blob_offset, blob_size);
	}

	if (mapped_size >= 4 && memcmp(mapped, "DATA", 4) == 0)
	{
		blob_base_offset = size_t((mapped + 4) - base_mapped);
		mapped_size -= 4;
		if (mapped_size < required_size)
			throw std::range_error("Blob is not large enough for all files.");
	}
}

BlobFilesystem::Directory *BlobFilesystem::make_directory(const std::string &path)
{
	auto split = Util::split_no_empty(path, "/");
	auto *dir = root.get();

	for (const auto &subpath : split)
	{
		auto dir_itr = std::find_if(dir->dirs.begin(), dir->dirs.end(), [&](const std::unique_ptr<Directory> &dir_) {
			return subpath == dir_->path;
		});

		if (dir_itr != dir->dirs.end())
			dir = dir_itr->get();
		else
		{
			dir->dirs.emplace_back(new Directory);
			dir->dirs.back()->path = subpath;
			dir = dir->dirs.back().get();
		}
	}

	return dir;
}

BlobFilesystem::Directory *BlobFilesystem::find_directory(const std::string &path)
{
	auto split = Util::split_no_empty(path, "/");
	auto *dir = root.get();

	for (const auto &subpath : split)
	{
		auto dir_itr = std::find_if(dir->dirs.begin(), dir->dirs.end(), [&](const std::unique_ptr<Directory> &dir_) {
			return subpath == dir_->path;
		});

		if (dir_itr != dir->dirs.end())
			dir = dir_itr->get();
		else
			return nullptr;
	}

	return dir;
}

BlobFilesystem::BlobFile *BlobFilesystem::find_file(const std::string &path)
{
	auto paths = Path::split(path);
	auto *dir = find_directory(paths.first);
	if (!dir)
		return nullptr;

	auto file_itr = std::find_if(dir->files.begin(), dir->files.end(), [&](const BlobFile &zip_file) {
		return paths.second == zip_file.path;
	});

	if (file_itr != dir->files.end())
		return &*file_itr;
	else
		return nullptr;
}

std::vector<ListEntry> BlobFilesystem::list(const std::string &path)
{
	auto canon_path = Path::canonicalize_path(path);

	std::vector<ListEntry> entries;
	if (const auto *zip_dir = find_directory(canon_path))
	{
		entries.reserve(zip_dir->dirs.size() + zip_dir->files.size());
		for (auto &dir : zip_dir->dirs)
			entries.push_back({ Path::join(path, dir->path), PathType::Directory });
		for (auto &f : zip_dir->files)
			entries.push_back({ Path::join(path, f.path), PathType::File });
	}
	return entries;
}

bool BlobFilesystem::stat(const std::string &path, FileStat &stat)
{
	auto p = Path::canonicalize_path(path);

	if (const auto *zip_file = find_file(p))
	{
		stat.size = zip_file->size;
		stat.type = PathType::File;
		stat.last_modified = 0;
		return true;
	}
	else if (find_directory(p))
	{
		stat.size = 0;
		stat.last_modified = 0;
		stat.type = PathType::Directory;
		return true;
	}
	else
		return false;
}

FileHandle BlobFilesystem::open(const std::string &path, FileMode mode)
{
	if (mode != FileMode::ReadOnly)
		return {};

	auto p = Path::canonicalize_path(path);
	auto *blob_file = find_file(p);
	if (!blob_file)
		return {};

	return Util::make_handle<FileSlice>(file, blob_base_offset + blob_file->offset, blob_file->size);
}

FileNotifyHandle BlobFilesystem::install_notification(const std::string &, std::function<void (const FileNotifyInfo &)>)
{
	return -1;
}

void BlobFilesystem::uninstall_notification(FileNotifyHandle)
{
}

void BlobFilesystem::poll_notifications()
{
}

int BlobFilesystem::get_notification_fd() const
{
	return -1;
}

FileMapping::FileMapping(FileHandle handle_, uint64_t file_offset_,
                         void *mapped_, size_t mapped_size_,
                         size_t map_offset_, size_t accessible_size_)
	: handle(std::move(handle_))
	, file_offset(file_offset_)
	, mapped(mapped_)
	, mapped_size(mapped_size_)
	, map_offset(map_offset_)
	, accessible_size(accessible_size_)
{
}

FileMapping::~FileMapping()
{
	if (handle)
		handle->unmap(mapped, mapped_size);
}

uint64_t FileMapping::get_file_offset() const
{
	return file_offset;
}

uint64_t FileMapping::get_size() const
{
	return accessible_size;
}

Util::IntrusivePtr<FileMapping> File::map()
{
	return map_subset(0, get_size());
}

FileSlice::FileSlice(FileHandle handle_, uint64_t offset_, uint64_t range_)
	: handle(std::move(handle_)), offset(offset_), range(range_)
{
}

FileMappingHandle FileSlice::map_subset(uint64_t offset_, size_t range_)
{
	if (offset_ + range_ > range)
		return {};
	return handle->map_subset(offset + offset_, range_);
}

FileMappingHandle FileSlice::map_write(size_t)
{
	return {};
}

uint64_t FileSlice::get_size()
{
	return range;
}

void FileSlice::unmap(void *mapped, size_t mapped_size)
{
	handle->unmap(mapped, mapped_size);
}
}
