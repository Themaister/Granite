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

#include "filesystem.hpp"
#include "os_filesystem.hpp"
#include "fs-netfs.hpp"
#include "path.hpp"
#include "logging.hpp"
#include "string_helpers.hpp"
#include <stdlib.h>
#include <algorithm>

using namespace std;

namespace Granite
{
bool StdioFile::init(const std::string &path, FileMode mode_)
{
	mode = mode_;

	const char *filemode = nullptr;
	switch (mode)
	{
	case FileMode::ReadOnly:
		filemode = "rb";
		break;

	case FileMode::ReadWrite:
		filemode = "rb+";
		break;

	case FileMode::WriteOnly:
		filemode = "wb";
		break;
	}

	file = fopen(path.c_str(), filemode);
	if (!file)
		return false;

	if (mode != FileMode::WriteOnly)
	{
		fseek(file, 0, SEEK_END);
		size = ftell(file);
		rewind(file);
	}

	return true;
}

size_t StdioFile::get_size()
{
	return size;
}

void *StdioFile::map()
{
	rewind(file);
	buffer.resize(size);
	fread(buffer.data(), 1, size, file);
	return buffer.data();
}

void *StdioFile::map_write(size_t size_)
{
	size = size_;
	buffer.resize(size);
	return buffer.data();
}

bool StdioFile::reopen()
{
	return false;
}

void StdioFile::unmap()
{
}

StdioFile *StdioFile::open(const std::string &path, Granite::FileMode mode)
{
	auto *file = new StdioFile();
	if (!file->init(path, mode))
	{
		delete file;
		return nullptr;
	}
	else
		return file;
}

StdioFile::~StdioFile()
{
	if (file)
	{
		if (mode != FileMode::ReadOnly)
		{
			rewind(file);
			fwrite(buffer.data(), 1, size, file);
		}

		fclose(file);
	}
}

vector<ListEntry> FilesystemBackend::walk(const std::string &path)
{
	auto entries = list(path);
	vector<ListEntry> final_entries;
	for (auto &e : entries)
	{
		if (e.type == PathType::Directory)
		{
			auto subentries = walk(e.path);
			final_entries.push_back(move(e));
			for (auto &sub : subentries)
				final_entries.push_back(move(sub));
		}
		else if (e.type == PathType::File)
			final_entries.push_back(move(e));
	}
	return final_entries;
}

Filesystem::Filesystem()
{
	register_protocol("file", unique_ptr<FilesystemBackend>(new OSFilesystem(".")));
	register_protocol("memory", unique_ptr<FilesystemBackend>(new ScratchFilesystem));

#ifdef ANDROID
	register_protocol("assets", unique_ptr<FilesystemBackend>(new NetworkFilesystem));
	register_protocol("builtin", unique_ptr<FilesystemBackend>(new NetworkFilesystem));
	register_protocol("cache", unique_ptr<FilesystemBackend>(new NetworkFilesystem));
#else
	if (getenv("GRANITE_USE_NETFS"))
	{
		register_protocol("assets", unique_ptr<FilesystemBackend>(new NetworkFilesystem));
		register_protocol("builtin", unique_ptr<FilesystemBackend>(new NetworkFilesystem));
		register_protocol("cache", unique_ptr<FilesystemBackend>(new NetworkFilesystem));
	}
	else
	{
		const char *asset_dir = getenv("GRANITE_DEFAULT_ASSET_DIRECTORY");
#ifdef GRANITE_DEFAULT_ASSET_DIRECTORY
		if (!asset_dir)
			asset_dir = GRANITE_DEFAULT_ASSET_DIRECTORY;
#endif
		if (asset_dir)
			register_protocol("builtin", unique_ptr<FilesystemBackend>(new OSFilesystem(asset_dir)));

		const char *builtin_dir = getenv("GRANITE_DEFAULT_BUILTIN_DIRECTORY");
#ifdef GRANITE_DEFAULT_BUILTIN_DIRECTORY
		if (!builtin_dir)
			builtin_dir = GRANITE_DEFAULT_BUILTIN_DIRECTORY;
#endif
		if (builtin_dir)
			register_protocol("builtin", unique_ptr<FilesystemBackend>(new OSFilesystem(builtin_dir)));

		const char *cache_dir = getenv("GRANITE_DEFAULT_CACHE_DIRECTORY");
#ifdef GRANITE_DEFAULT_CACHE_DIRECTORY
		if (!cache_dir)
			cache_dir = GRANITE_DEFAULT_CACHE_DIRECTORY;
#endif
		if (cache_dir)
			register_protocol("cache", unique_ptr<FilesystemBackend>(new OSFilesystem(cache_dir)));
	}
#endif
}

void Filesystem::register_protocol(const std::string &proto, std::unique_ptr<FilesystemBackend> fs)
{
	fs->set_protocol(proto);
	auto *em = Global::event_manager();
	if (em)
		em->dispatch_inline(FilesystemProtocolEvent(proto, *fs));
	protocols[proto] = move(fs);
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

bool Filesystem::read_file_to_string(const std::string &path, std::string &str)
{
	auto file = open(path, FileMode::ReadOnly);
	if (!file)
		return false;

	auto size = file->get_size();
	auto *mapped = static_cast<const char *>(file->map());
	if (!mapped)
		return false;

	str = string(mapped, mapped + size);

	// Remove DOS EOL.
	str.erase(remove_if(begin(str), end(str), [](char c) { return c == '\r'; }), end(str));

	return true;
}

bool Filesystem::write_buffer_to_file(const std::string &path, const void *data, size_t size)
{
	auto file = open(path, FileMode::WriteOnly);
	if (!file)
		return false;

	void *mapped = file->map_write(size);
	if (!mapped)
		return false;

	memcpy(mapped, data, size);
	file->unmap();
	return true;
}

bool Filesystem::write_string_to_file(const std::string &path, const std::string &str)
{
	return write_buffer_to_file(path, str.data(), str.size());
}

std::unique_ptr<File> Filesystem::open(const std::string &path, FileMode mode)
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

struct ScratchFilesystemFile : File
{
	explicit ScratchFilesystemFile(std::vector<uint8_t> &data_)
		: data(data_)
	{
	}

	void *map() override
	{
		return data.data();
	}

	void *map_write(size_t size) override
	{
		data.resize(size);
		return data.data();
	}

	bool reopen() override
	{
		return true;
	}

	void unmap() override
	{
	}

	size_t get_size() override
	{
		return data.size();
	}

	std::vector<uint8_t> &data;
};

std::unique_ptr<File> ScratchFilesystem::open(const std::string &path, FileMode)
{
	auto itr = scratch_files.find(path);
	if (itr == end(scratch_files))
	{
		auto &file = scratch_files[path];
		file.reset(new ScratchFile);
		return std::unique_ptr<ScratchFilesystemFile>(new ScratchFilesystemFile(file->data));
	}
	else
	{
		return std::unique_ptr<ScratchFilesystemFile>(new ScratchFilesystemFile(itr->second->data));
	}
}

BlobFilesystem::BlobFilesystem(std::unique_ptr<File> file_, std::string basedir_)
	: file(std::move(file_)), base(std::move(basedir_))
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
	auto *mapped = static_cast<const uint8_t *>(file->map());
	if (!mapped)
		throw std::runtime_error("Failed to map blob archive.");

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

		if (blob_offset > std::numeric_limits<size_t>::max() ||
		    blob_size > std::numeric_limits<size_t>::max())
			throw std::range_error("Blob offset out of range.");

		add_entry(path, blob_offset, blob_size);
	}

	if (mapped_size >= 4 && memcmp(mapped, "DATA", 4) == 0)
	{
		blob_base = mapped + 4;
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
	if (const auto *zip_dir = find_directory(Path::join(base, canon_path)))
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
	auto p = Path::join(base, Path::canonicalize_path(path));

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

std::unique_ptr<File> BlobFilesystem::open(const std::string &path, FileMode mode)
{
	if (mode != FileMode::ReadOnly)
		return {};

	auto p = Path::join(base, Path::canonicalize_path(path));
	auto *blob_file = find_file(p);
	if (!blob_file)
		return {};
	return std::make_unique<ConstantMemoryFile>(blob_base + blob_file->offset, blob_file->size);
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

}