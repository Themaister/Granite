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

#include "os_filesystem.hpp"
#include "path_utils.hpp"
#include "logging.hpp"
#include <stdexcept>
#include <sys/stat.h>
#include <sys/types.h>
#include <atomic>

namespace Granite
{
static bool ensure_directory_inner(const std::string &path)
{
	if (Path::is_root_path(path))
		return false;

	auto wpath = Path::to_utf16(path);

	struct __stat64 s;
	if (::_wstat64(wpath.c_str(), &s) >= 0 && (s.st_mode & _S_IFDIR) != 0)
		return true;

	auto basedir = Path::basedir(path);
	if (!ensure_directory_inner(basedir))
		return false;

	if (!CreateDirectoryW(wpath.c_str(), nullptr))
		return GetLastError() == ERROR_ALREADY_EXISTS;
	return true;
}

static bool ensure_directory(const std::string &path)
{
	auto basedir = Path::basedir(path);
	return ensure_directory_inner(basedir);
}

FileHandle MappedFile::open(const std::string &path, Granite::FileMode mode)
{
	auto file = Util::make_handle<MappedFile>();
	if (!file->init(path, mode))
		file.reset();
	return file;
}

static std::atomic_uint32_t global_transaction_counter;

bool MappedFile::init(const std::string &path, FileMode mode)
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
		{
			LOGE("MappedFile failed to create directory.\n");
			return false;
		}

		access = GENERIC_READ | GENERIC_WRITE;
		disposition = OPEN_ALWAYS;
		break;

	case FileMode::WriteOnly:
	case FileMode::WriteOnlyTransactional:
		if (!ensure_directory(path))
		{
			LOGE("MappedFile failed to create directory.\n");
			return false;
		}

		access = GENERIC_READ | GENERIC_WRITE;
		disposition = CREATE_ALWAYS;
		break;
	}

	if (mode == FileMode::WriteOnlyTransactional)
	{
		// Use atomic file rename to ensure that a file is written atomically.
		rename_to_on_close = path;
		rename_from_on_close =
				path + ".tmp." +
				std::to_string(GetCurrentProcessId()) + "." +
				std::to_string(global_transaction_counter.fetch_add(1, std::memory_order_relaxed));
	}

	auto wpath = Path::to_utf16(rename_from_on_close.empty() ? path : rename_from_on_close);

	file = CreateFileW(wpath.c_str(), access, FILE_SHARE_READ, nullptr, disposition,
	                   FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, INVALID_HANDLE_VALUE);
	if (file == INVALID_HANDLE_VALUE)
	{
		rename_to_on_close.clear();
		rename_from_on_close.clear();
		return false;
	}

	if (mode != FileMode::WriteOnly && mode != FileMode::WriteOnlyTransactional)
	{
		DWORD hi;
		DWORD lo = GetFileSize(file, &hi);
		size = (uint64_t(hi) << 32) | uint32_t(lo);
		file_mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
	}

	return true;
}

uint64_t MappedFile::get_size()
{
	return size;
}

struct PageSizeQuery
{
	PageSizeQuery()
	{
		SYSTEM_INFO system_info = {};
		GetSystemInfo(&system_info);
		page_size = system_info.dwPageSize;
	}
	uint32_t page_size = 0;
};
static PageSizeQuery static_page_size_query;

FileMappingHandle MappedFile::map_subset(uint64_t offset, size_t range)
{
	if (offset + range > size)
		return {};

	if (!file_mapping)
		return {};

	uint64_t begin_map = offset & ~uint64_t(static_page_size_query.page_size - 1);

	DWORD hi = DWORD(begin_map >> 32);
	DWORD lo = DWORD(begin_map & 0xffffffffu);
	uint64_t end_mapping = offset + range;
	size_t mapped_size = end_mapping - begin_map;

	void *mapped = MapViewOfFile(file_mapping, FILE_MAP_READ, hi, lo, mapped_size);
	if (!mapped)
		return {};

	return Util::make_handle<FileMapping>(
		reference_from_this(), offset,
		static_cast<uint8_t *>(mapped) + begin_map, mapped_size,
		offset - begin_map, range);
}

FileMappingHandle MappedFile::map_write(size_t map_size)
{
	size = map_size;

#ifdef _WIN64
	DWORD hi = DWORD(size >> 32);
	DWORD lo = DWORD(size & 0xffffffffu);
#else
	DWORD hi = 0;
	DWORD lo = DWORD(size);
#endif

	HANDLE file_view = CreateFileMappingW(file, nullptr, PAGE_READWRITE, hi, lo, nullptr);
	if (!file_view)
		return {};

	void *mapped = MapViewOfFile(file_view, FILE_MAP_ALL_ACCESS, 0, 0, size);
	CloseHandle(file_view);

	if (!mapped)
		return {};

	return Util::make_handle<FileMapping>(reference_from_this(), 0,
		static_cast<uint8_t *>(mapped), size,
		0, size);
}

void MappedFile::unmap(void *mapped, size_t)
{
	if (mapped)
		UnmapViewOfFile(mapped);
}

MappedFile::~MappedFile()
{
	if (file_mapping)
		CloseHandle(file_mapping);
	if (file != INVALID_HANDLE_VALUE)
		CloseHandle(file);

	if (!rename_from_on_close.empty() && !rename_to_on_close.empty())
	{
		auto to_w16 = Path::to_utf16(rename_to_on_close);
		auto from_w16 = Path::to_utf16(rename_from_on_close);
		DWORD code = S_OK;

		if (!MoveFileW(from_w16.c_str(), to_w16.c_str()))
		{
			code = GetLastError();
			if (code == ERROR_ALREADY_EXISTS && !ReplaceFileW(to_w16.c_str(), from_w16.c_str(), nullptr, 0, nullptr, nullptr))
				code = GetLastError();
		}

		if (FAILED(code))
			LOGE("Failed to rename file %s -> %s (0x%lx).\n", rename_from_on_close.c_str(), rename_to_on_close.c_str(), code);
	}
}

OSFilesystem::OSFilesystem(const std::string &base_)
    : base(base_)
{
}

OSFilesystem::~OSFilesystem()
{
	for (auto &handler : handlers)
	{
		CancelIo(handler.second.handle);
		CloseHandle(handler.second.handle);
		CloseHandle(handler.second.event);
	}
}

std::string OSFilesystem::get_filesystem_path(const std::string &path)
{
	return Path::join(base, path);
}

FileHandle OSFilesystem::open(const std::string &path, FileMode mode)
{
	return MappedFile::open(Path::join(base, path), mode);
}

void OSFilesystem::poll_notifications()
{
	for (auto &handler : handlers)
	{
		if (WaitForSingleObject(handler.second.event, 0) != WAIT_OBJECT_0)
			continue;

		DWORD bytes_returned;
		if (!GetOverlappedResult(handler.second.handle, &handler.second.overlapped, &bytes_returned, TRUE))
			continue;

		size_t offset = 0;
		const FILE_NOTIFY_INFORMATION *info = nullptr;
		do
		{
			info = reinterpret_cast<const FILE_NOTIFY_INFORMATION *>(
			    reinterpret_cast<const uint8_t *>(handler.second.async_buffer) + offset);

			FileNotifyInfo notify;
			notify.handle = handler.first;
			notify.path = Path::join(handler.second.path,
			                         Path::to_utf8(info->FileName,
			                                       info->FileNameLength / sizeof(wchar_t)));

			switch (info->Action)
			{
			case FILE_ACTION_ADDED:
			case FILE_ACTION_RENAMED_NEW_NAME:
				notify.type = FileNotifyType::FileCreated;
				if (handler.second.func)
					handler.second.func(notify);
				break;

			case FILE_ACTION_REMOVED:
			case FILE_ACTION_RENAMED_OLD_NAME:
				notify.type = FileNotifyType::FileDeleted;
				if (handler.second.func)
					handler.second.func(notify);
				break;

			case FILE_ACTION_MODIFIED:
				notify.type = FileNotifyType::FileChanged;
				if (handler.second.func)
					handler.second.func(notify);
				break;

			default:
				LOGE("Invalid notify type.\n");
				break;
			}

			offset += info->NextEntryOffset;
		} while (info->NextEntryOffset != 0);

		kick_async(handler.second);
	}
}

void OSFilesystem::uninstall_notification(FileNotifyHandle id)
{
	auto itr = handlers.find(id);
	if (itr != end(handlers))
	{
		CancelIo(itr->second.handle);
		CloseHandle(itr->second.handle);
		CloseHandle(itr->second.event);
		handlers.erase(itr);
	}
}

void OSFilesystem::kick_async(Handler &handler)
{
	handler.overlapped = {};
	handler.overlapped.hEvent = handler.event;

	auto ret = ReadDirectoryChangesW(handler.handle, handler.async_buffer, sizeof(handler.async_buffer), FALSE,
	                                 FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION | FILE_NOTIFY_CHANGE_FILE_NAME,
	                                 nullptr, &handler.overlapped, nullptr);

	if (!ret && GetLastError() != ERROR_IO_PENDING)
	{
		LOGE("Failed to read directory changes async.\n");
	}
}

FileNotifyHandle OSFilesystem::install_notification(const std::string &path, std::function<void(const FileNotifyInfo &)> func)
{
	FileStat s = {};
	if (!stat(path, s))
	{
		LOGE("Window inotify: path doesn't exist.\n");
		return -1;
	}

	if (s.type != PathType::Directory)
	{
		LOGE("Windows inotify: Implementation only supports directories.\n");
		return -1;
	}

	auto resolved_path = Path::to_utf16(Path::join(base, path));
	HANDLE handle =
	    CreateFileW(resolved_path.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_WRITE | FILE_SHARE_READ | FILE_SHARE_DELETE,
	                nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
	if (handle == INVALID_HANDLE_VALUE)
	{
		LOGE("Failed to open directory for watching.\n");
		return -1;
	}

	HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (event == nullptr)
	{
		CloseHandle(handle);
		return -1;
	}

	handle_id++;
	Handler handler;
	handler.path = protocol + "://" + path;
	handler.func = std::move(func);
	handler.handle = handle;
	handler.event = event;
	auto &h = handlers[handle_id];
	h = std::move(handler);
	kick_async(h);

	return handle_id;
}

bool OSFilesystem::remove(const std::string &path)
{
	auto joined = Path::to_utf16(Path::join(base, path));
	return bool(DeleteFileW(joined.c_str()));
}

bool OSFilesystem::move_yield(const std::string &dst, const std::string &src)
{
	auto joined_dst = Path::to_utf16(Path::join(base, dst));
	auto joined_src = Path::to_utf16(Path::join(base, src));
	return bool(MoveFileW(joined_src.c_str(), joined_dst.c_str()));
}

bool OSFilesystem::move_replace(const std::string &dst, const std::string &src)
{
	auto joined_dst = Path::to_utf16(Path::join(base, dst));
	auto joined_src = Path::to_utf16(Path::join(base, src));
	if (MoveFileW(joined_src.c_str(), joined_dst.c_str()))
		return true;
	if (GetLastError() != ERROR_ALREADY_EXISTS)
		return false;
	return bool(ReplaceFileW(joined_dst.c_str(), joined_src.c_str(), nullptr, 0, nullptr, nullptr));
}

std::vector<ListEntry> OSFilesystem::list(const std::string &path)
{
	std::vector<ListEntry> entries;
	WIN32_FIND_DATAW result;
	auto joined = Path::to_utf16(Path::join(base, path));
	joined += L"/*";

	HANDLE handle = FindFirstFileW(joined.c_str(), &result);
	if (handle == INVALID_HANDLE_VALUE)
		return entries;

	do
	{
		ListEntry entry;
		if (result.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			entry.type = PathType::Directory;
		else
			entry.type = PathType::File;

		auto utf8_path = Path::to_utf8(result.cFileName);
		if (utf8_path == "." || utf8_path == "..")
			continue;

		entry.path = Path::join(path, utf8_path);
		entries.push_back(std::move(entry));
	} while (FindNextFileW(handle, &result));

	FindClose(handle);
	return entries;
}

bool OSFilesystem::stat(const std::string &path, FileStat &stat)
{
	auto joined = Path::join(base, path);
	struct __stat64 buf;
	if (_wstat64(Path::to_utf16(joined).c_str(), &buf) < 0)
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

} // namespace Granite
