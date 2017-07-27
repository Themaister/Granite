/* Copyright (c) 2017 Hans-Kristian Arntzen
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
#include "os.hpp"
#include "fs-netfs.hpp"
#include "path.hpp"
#include "util.hpp"
#include <stdlib.h>

using namespace std;

namespace Granite
{
StdioFile::StdioFile(const std::string &path, FileMode mode)
	: mode(mode)
{
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
	{
		LOGE("Failed to open file: %s\n", path.c_str());
		throw runtime_error("fopen() failed.");
	}

	if (mode != FileMode::WriteOnly)
	{
		fseek(file, 0, SEEK_END);
		size = ftell(file);
		rewind(file);
	}
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

void *StdioFile::map_write(size_t size)
{
	buffer.resize(size);
	this->size = size;
	return buffer.data();
}

bool StdioFile::reopen()
{
	return false;
}

void StdioFile::unmap()
{
}

StdioFile::~StdioFile()
{
	if (mode != FileMode::ReadOnly)
	{
		rewind(file);
		fwrite(buffer.data(), 1, size, file);
	}
	fclose(file);
}

Filesystem &Filesystem::get()
{
	static Filesystem fs;
	return fs;
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
	protocols[""] = unique_ptr<FilesystemBackend>(new OSFilesystem("/"));

#ifdef ANDROID
	protocols["assets"] = unique_ptr<FilesystemBackend>(new NetworkFilesystem);
	protocols["assets"]->set_protocol("assets");
	protocols["cache"] = unique_ptr<FilesystemBackend>(new NetworkFilesystem);
	protocols["cache"]->set_protocol("cache");
#else
	if (getenv("GRANITE_USE_NETFS"))
	{
		protocols["assets"] = unique_ptr<FilesystemBackend>(new NetworkFilesystem);
		protocols["assets"]->set_protocol("assets");
		protocols["cache"] = unique_ptr<FilesystemBackend>(new NetworkFilesystem);
		protocols["cache"]->set_protocol("cache");
	}
	else
	{
#ifdef GRANITE_DEFAULT_ASSET_DIRECTORY
		const char *asset_dir = getenv("GRANITE_DEFAULT_ASSET_DIRECTORY");
		if (!asset_dir)
			asset_dir = GRANITE_DEFAULT_ASSET_DIRECTORY;
		protocols["assets"] = unique_ptr<FilesystemBackend>(new OSFilesystem(asset_dir));
		protocols["assets"]->set_protocol("assets");
#endif

#ifdef GRANITE_DEFAULT_CACHE_DIRECTORY
		const char *cache_dir = getenv("GRANITE_DEFAULT_CACHE_DIRECTORY");
		if (!cache_dir)
			cache_dir = GRANITE_DEFAULT_CACHE_DIRECTORY;
		protocols["cache"] = unique_ptr<FilesystemBackend>(new OSFilesystem(cache_dir));
		protocols["cache"]->set_protocol("cache");
#endif
	}
#endif
}

void Filesystem::register_protocol(const std::string &proto, std::unique_ptr<FilesystemBackend> fs)
{
	EventManager::get_global().dispatch_inline(FilesystemProtocolEvent(proto, *fs));
	protocols[proto] = move(fs);
}

FilesystemBackend *Filesystem::get_backend(const std::string &proto)
{
	auto itr = protocols.find(proto);
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
	const char *mapped = static_cast<const char *>(file->map());
	if (!mapped)
		return false;

	str = string(mapped, mapped + size);
	return true;
}

std::unique_ptr<File> Filesystem::open(const std::string &path, FileMode mode)
{
	auto paths = Path::protocol_split(path);
	auto *backend = get_backend(paths.first);
	if (!backend)
		return {};

	return backend->open(paths.second, mode);
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
}