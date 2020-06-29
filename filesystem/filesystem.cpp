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

ZIPFilesystem::ZIPFilesystem(std::unique_ptr<File> file_, std::string basedir_)
	: file(std::move(file_)), base(std::move(basedir_))
{
}

const ZIPFilesystem::Directory *ZIPFilesystem::find_directory(const std::string &path) const
{
	auto split = Util::split_no_empty(path, "/");
	const auto *dir = root.get();

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

const ZIPFilesystem::ZipFile *ZIPFilesystem::find_file(const std::string &path) const
{
	auto paths = Path::split(path);
	auto *dir = find_directory(paths.first);
	if (!dir)
		return nullptr;

	auto file_itr = std::find_if(dir->files.begin(), dir->files.end(), [&](const ZipFile &zip_file) {
		return paths.second == zip_file.path;
	});

	if (file_itr != dir->files.end())
		return &*file_itr;
	else
		return nullptr;
}

std::vector<ListEntry> ZIPFilesystem::list(const std::string &path)
{
	std::vector<ListEntry> entries;
	if (const auto *zip_dir = find_directory(Path::join(base, path)))
	{
		entries.reserve(zip_dir->dirs.size() + zip_dir->files.size());
		for (auto &dir : zip_dir->dirs)
			entries.push_back({ dir->path, PathType::Directory });
		for (auto &f : zip_dir->files)
			entries.push_back({ f.path, PathType::File });
	}
	return entries;
}

bool ZIPFilesystem::stat(const std::string &path, FileStat &stat)
{
	auto p = Path::join(base, path);

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

std::unique_ptr<File> ZIPFilesystem::open(const std::string &path, FileMode mode)
{
	if (mode != FileMode::ReadOnly)
		return {};

	auto p = Path::join(base, path);
	auto *zip_file = find_file(p);
	if (!zip_file)
		return {};
	return std::make_unique<ConstantMemoryFile>(zip_file->mapped, zip_file->size);
}

FileNotifyHandle ZIPFilesystem::install_notification(const std::string &, std::function<void (const FileNotifyInfo &)>)
{
	return -1;
}

void ZIPFilesystem::uninstall_notification(FileNotifyHandle)
{
}

void ZIPFilesystem::poll_notifications()
{
}

int ZIPFilesystem::get_notification_fd() const
{
	return -1;
}

}