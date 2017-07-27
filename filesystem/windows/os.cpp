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
#include <stdexcept>

using namespace std;

namespace Granite
{

MappedFile::MappedFile(const std::string &path, FileMode mode)
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
		access = GENERIC_READ | GENERIC_WRITE;
		disposition = OPEN_EXISTING;
		break;

	case FileMode::WriteOnly:
		access = GENERIC_READ | GENERIC_WRITE;
		disposition = CREATE_ALWAYS;
		break;
	}
	HANDLE file = CreateFileA(path.c_str(), access, 0,
	                          nullptr, disposition,
	                          FILE_ATTRIBUTE_NORMAL, nullptr);

	if (file == INVALID_HANDLE_VALUE)
	{
		LOGE("open(): %s failed.\n", path.c_str());
		throw runtime_error("Failed to open file.");
	}

	DWORD hi;
	DWORD lo = GetFileSize(file, &hi);
	size = size_t((uint64_t(hi) << 32) | lo);
}

size_t MappedFile::get_size()
{
	return size;
}

void *MappedFile::map()
{
	if (mapped)
		return mapped;

	DWORD lo = DWORD(size);
	DWORD hi_size = DWORD(uint64_t(size) >> 32);
	file_mapping = CreateFileMappingA(file, nullptr, PAGE_READONLY, hi_size, lo, nullptr);
	if (file_mapping == INVALID_HANDLE_VALUE)
		return nullptr;
	mapped = MapViewOfFile(file_mapping, FILE_MAP_READ, 0, 0, size);
	return mapped;
}

void *MappedFile::map_write(size_t size)
{
	unmap();
	this->size = size;

	DWORD lo = DWORD(size);
	DWORD hi_size = DWORD(uint64_t(size) >> 32);
	file_mapping = CreateFileMappingA(file, nullptr, PAGE_READWRITE, hi_size, lo, nullptr);
	if (file_mapping == INVALID_HANDLE_VALUE)
		return nullptr;

	mapped = MapViewOfFile(file, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, size);
	return mapped;
}

bool MappedFile::reopen()
{
	return false;
}

void MappedFile::unmap()
{
	if (mapped)
		UnmapViewOfFile(mapped);
	if (file_mapping)
		CloseHandle(file_mapping);
	mapped = nullptr;
	file_mapping = INVALID_HANDLE_VALUE;
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

vector<ListEntry> OSFilesystem::list(const string &)
{
    return {};
}

bool OSFilesystem::stat(const std::string &, FileStat &)
{
    return false;
}

int OSFilesystem::get_notification_fd() const
{
    return -1;
}

}
