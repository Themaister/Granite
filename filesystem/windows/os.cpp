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

#include "os.hpp"
#include "../path.hpp"
#include "util.hpp"
#include <sys/types.h>
#include <sys/stat.h>
#include <stdexcept>

using namespace std;

namespace Granite
{
static bool ensure_directory_inner(const std::string &path)
{
	if (Path::is_root_path(path))
		return false;

	struct _stat s;
	if (::_stat(path.c_str(), &s) >= 0 && (s.st_mode & _S_IFDIR) != 0)
		return true;

	auto basedir = Path::basedir(path);
	if (!ensure_directory_inner(basedir))
		return false;

	if (!CreateDirectory(path.c_str(), nullptr))
		return GetLastError() == ERROR_ALREADY_EXISTS;
	return true;
}

static bool ensure_directory(const std::string &path)
{
	auto basedir = Path::basedir(path);
	return ensure_directory_inner(basedir);
}

MappedFile::MappedFile(const string &path, FileMode mode)
{
	DWORD access = 0;
	DWORD disposition = 0;

	switch (mode)
	{
	case FileMode::ReadOnly:
		access = GENERIC_READ;
		disposition = OPEN_EXISTING;
		break;

	case FileMode::ReadWrite:
		if (!ensure_directory(path))
			throw runtime_error("MappedFile failed to create directory");
		access = GENERIC_READ | GENERIC_WRITE;
		disposition = OPEN_ALWAYS;
		break;

	case FileMode::WriteOnly:
		if (!ensure_directory(path))
			throw runtime_error("MappedFile failed to create directory");
		access = GENERIC_READ | GENERIC_WRITE;
		disposition = CREATE_ALWAYS;
		break;
	}

	file = CreateFileA(path.c_str(), access, FILE_SHARE_READ, nullptr, disposition, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, INVALID_HANDLE_VALUE);
	if (file == INVALID_HANDLE_VALUE)
	{
		LOGE("Failed to open file: %s.\n", path.c_str());
		throw runtime_error("MappedFile::MappedFile()");
	}

	if (mode != FileMode::WriteOnly)
	{
		DWORD hi;
		DWORD lo = GetFileSize(file, &hi);
		size = size_t((uint64_t(hi) << 32) | uint32_t(lo));
	}
}

size_t MappedFile::get_size()
{
	return size;
}

bool MappedFile::reopen()
{
	return true;
}

void *MappedFile::map()
{
	if (mapped)
		unmap();

	HANDLE file_view = CreateFileMappingA(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
	if (file_view == INVALID_HANDLE_VALUE)
		return nullptr;

	mapped = MapViewOfFile(file_view, FILE_MAP_READ, 0, 0, size);
	CloseHandle(file_view);
	return mapped;
}

void *MappedFile::map_write(size_t size)
{
	if (mapped)
		unmap();

	this->size = size;

#ifdef _WIN64
	DWORD hi = DWORD(size >> 32);
	DWORD lo = DWORD(size & 0xffffffffu);
#else
	DWORD hi = 0;
	DWORD lo = DWORD(size);
#endif

	HANDLE file_view = CreateFileMappingA(file, nullptr, PAGE_READWRITE, hi, lo, nullptr);
	if (file_view == INVALID_HANDLE_VALUE)
		return nullptr;

	mapped = MapViewOfFile(file_view, FILE_MAP_ALL_ACCESS, 0, 0, size);
	CloseHandle(file_view);
	return mapped;
}

void MappedFile::unmap()
{
	if (mapped)
		UnmapViewOfFile(mapped);
	mapped = nullptr;
}

MappedFile::~MappedFile()
{
	unmap();
	if (file != INVALID_HANDLE_VALUE)
		CloseHandle(file);
}

OSFilesystem::OSFilesystem(const std::string &base)
	: base(base)
{
}

OSFilesystem::~OSFilesystem()
{
}

unique_ptr<File> OSFilesystem::open(const std::string &path, FileMode mode)
{
	try
	{
		unique_ptr<File> file(new MappedFile(Path::join(base, path), mode));
		return file;
	}
	catch (const std::exception &e)
	{
		LOGE("OSFilesystem::open(): %s\n", e.what());
		return {};
	}
}

void OSFilesystem::poll_notifications()
{
}

void OSFilesystem::uninstall_notification(FileNotifyHandle)
{
}

FileNotifyHandle OSFilesystem::install_notification(const string &,
                                                    function<void (const FileNotifyInfo &)>)
{
    return -1;
}

vector<ListEntry> OSFilesystem::list(const string &path)
{
	vector<ListEntry> entries;
	WIN32_FIND_DATAA result;
	auto joined = Path::join(base, path);
	for (HANDLE handle = FindFirstFileA(joined.c_str(), &result);
		handle != INVALID_HANDLE_VALUE;
		handle = FindFirstFileA(joined.c_str(), &result))
	{
		ListEntry entry;
		if (result.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			entry.type = PathType::Directory;
		else if (result.dwFileAttributes & FILE_ATTRIBUTE_NORMAL)
			entry.type = PathType::File;
		else
			entry.type = PathType::Special;

		entry.path = Path::join(path, result.cFileName);
		entries.push_back(move(entry));
	}
	return entries;
}

bool OSFilesystem::stat(const std::string &path, FileStat &stat)
{
	auto joined = Path::join(base, path);
	struct _stat buf;
	if (_stat(path.c_str(), &buf) < 0)
		return false;

	if (buf.st_mode & _S_IFREG)
		stat.type = PathType::File;
	else if (buf.st_mode & _S_IFDIR)
		stat.type = PathType::Directory;
	else
		stat.type = PathType::Special;

	stat.size = uint64_t(buf.st_size);
	stat.last_modified = buf.st_mtime;
	return true;
}

int OSFilesystem::get_notification_fd() const
{
    return -1;
}

}
