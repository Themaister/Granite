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
#include <algorithm>
#include <stdexcept>

#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <atomic>
#ifdef __linux__
#include <sys/inotify.h>
#endif

#if defined(__FreeBSD__) || defined(__APPLE__)
#define FSTAT64 fstat
#define FTRUNCATE64 ftruncate
#define MMAP64 mmap
#define STAT64 stat
#define off64_t off_t
#else
#define FSTAT64 fstat64
#define FTRUNCATE64 ftruncate64
#define MMAP64 mmap64
#define STAT64 stat64
#endif

namespace Granite
{
static bool ensure_directory_inner(const std::string &path)
{
	if (Path::is_root_path(path))
		return false;

	struct STAT64 s = {};
	if (::STAT64(path.c_str(), &s) >= 0 && S_ISDIR(s.st_mode))
		return true;

	auto basedir = Path::basedir(path);
	if (!ensure_directory_inner(basedir))
		return false;

	return (mkdir(path.c_str(), 0750) >= 0) || (errno == EEXIST);
}

static bool ensure_directory(const std::string &path)
{
	auto basedir = Path::basedir(path);
	return ensure_directory_inner(basedir);
}

FileHandle MMapFile::open(const std::string &path, FileMode mode)
{
	auto file = Util::make_handle<MMapFile>();
	if (!file->init(path, mode))
		file.reset();
	return file;
}

static std::atomic_uint32_t global_transaction_counter;

bool MMapFile::init(const std::string &path, FileMode mode)
{
	int modeflags = 0;
	switch (mode)
	{
	case FileMode::ReadOnly:
		modeflags = O_RDONLY;
		break;

	case FileMode::WriteOnly:
	case FileMode::WriteOnlyTransactional:
		if (!ensure_directory(path))
		{
			LOGE("MMapFile failed to create directory.\n");
			return false;
		}
		modeflags = O_RDWR | O_CREAT | O_TRUNC; // Need read access for mmap.
		break;

	case FileMode::ReadWrite:
		if (!ensure_directory(path))
		{
			LOGE("MMapFile failed to create directory.\n");
			return false;
		}
		modeflags = O_RDWR | O_CREAT;
		break;
	}

	const char *open_path = path.c_str();

	if (mode == FileMode::WriteOnlyTransactional)
	{
		// Use atomic file rename to ensure that a file is written atomically.
		rename_to_on_close = path;
		rename_from_on_close =
				path + ".tmp." +
				std::to_string(getpid()) + "." +
				std::to_string(global_transaction_counter.fetch_add(1, std::memory_order_relaxed));
		open_path = rename_from_on_close.c_str();
	}

	fd = ::open(open_path, modeflags, 0640);
	if (fd < 0)
	{
		rename_to_on_close.clear();
		rename_from_on_close.clear();
		return false;
	}

	if (!query_stat())
	{
		close(fd);
		rename_to_on_close.clear();
		rename_from_on_close.clear();
		return false;
	}

	return true;
}

FileMappingHandle MMapFile::map_write(size_t map_size)
{
	if (has_write_map)
		return {};

	if (FTRUNCATE64(fd, off64_t(map_size)) < 0)
	{
		LOGE("Failed to truncate.\n");
		report_error();
		return {};
	}

	size = map_size;

	void *mapped = MMAP64(nullptr, map_size, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
	if (mapped == MAP_FAILED)
	{
		report_error();
		return {};
	}

	has_write_map = true;

	return Util::make_handle<FileMapping>(
		reference_from_this(),
		0,
		mapped, map_size,
		0, map_size);
}

void MMapFile::report_error()
{
#ifdef __linux__
	int err = errno;
	char fdpath[PATH_MAX];
	char path[PATH_MAX];
	snprintf(fdpath, sizeof(fdpath), "/proc/%u/fd/%d", getpid(), fd);
	int ret = readlink(fdpath, path, sizeof(path) - 1);
	if (ret > 0)
	{
		path[ret] = '\0';
		LOGE("mmap failed for \"%s\" (%s).\n", path, strerror(err));
	}
#endif
}

FileMappingHandle MMapFile::map_subset(uint64_t offset, size_t range)
{
	uint64_t page_size = sysconf(_SC_PAGESIZE);
	uint64_t begin_map = offset & ~(page_size - 1);
	uint64_t end_map = offset + range;
	size_t mapped_size = end_map - begin_map;

	// length need not be aligned.

	void *mapped = MMAP64(nullptr, mapped_size, PROT_READ, MAP_PRIVATE, fd, off64_t(begin_map));
	if (mapped == MAP_FAILED)
	{
		report_error();
		return {};
	}

	return Util::make_handle<FileMapping>(
		reference_from_this(),
		offset,
		mapped, mapped_size,
		offset - begin_map, range);
}

uint64_t MMapFile::get_size()
{
	return size;
}

bool MMapFile::query_stat()
{
	struct STAT64 s = {};
	if (FSTAT64(fd, &s) < 0)
		return false;

	if (uint64_t(s.st_size) > SIZE_MAX)
		return false;
	size = static_cast<size_t>(s.st_size);
	return true;
}

void MMapFile::unmap(void *mapped, size_t mapped_size)
{
	munmap(mapped, mapped_size);
}

MMapFile::~MMapFile()
{
	if (fd >= 0)
		close(fd);

	if (!rename_from_on_close.empty() && !rename_to_on_close.empty())
	{
		int ret = rename(rename_from_on_close.c_str(), rename_to_on_close.c_str());
		if (ret != 0)
			LOGE("Failed to rename file %s -> %s.\n", rename_from_on_close.c_str(), rename_to_on_close.c_str());
	}
}

OSFilesystem::OSFilesystem(const std::string &base_)
	: base(base_)
{
#ifdef __linux__
	notify_fd = inotify_init1(IN_NONBLOCK);
	if (notify_fd < 0)
		LOGE("Failed to init inotify.\n");
#else
	notify_fd = -1;
#endif
}

OSFilesystem::~OSFilesystem()
{
#ifdef __linux__
	if (notify_fd > 0)
	{
		for (auto &handler : handlers)
			inotify_rm_watch(notify_fd, handler.first);
		close(notify_fd);
	}
#endif
}

FileHandle OSFilesystem::open(const std::string &path, FileMode mode)
{
	return MMapFile::open(Path::join(base, path), mode);
}

std::string OSFilesystem::get_filesystem_path(const std::string &path)
{
	return Path::join(base, path);
}

int OSFilesystem::get_notification_fd() const
{
	return notify_fd;
}

void OSFilesystem::poll_notifications()
{
#ifdef __linux__
	if (notify_fd < 0)
		return;

	for (;;)
	{
		alignas(inotify_event) char buffer[sizeof(inotify_event) + NAME_MAX + 1];
		ssize_t ret = read(notify_fd, buffer, sizeof(buffer));

		if (ret < 0)
		{
			if (errno != EAGAIN)
				LOGE("failed to read inotify fd.\n");
			break;
		}

		struct inotify_event *current = nullptr;
		for (ssize_t i = 0; i < ret; i += current->len + sizeof(struct inotify_event))
		{
			current = reinterpret_cast<inotify_event *>(buffer + i);
			auto mask = current->mask;

			int wd = current->wd;
			auto itr = handlers.find(wd);
			if (itr == end(handlers))
				continue;

			FileNotifyType type;
			if (mask & IN_CLOSE_WRITE)
				type = FileNotifyType::FileChanged;
			else if (mask & (IN_CREATE | IN_MOVED_TO))
				type = FileNotifyType::FileCreated;
			else if (mask & (IN_DELETE | IN_DELETE_SELF | IN_MOVED_FROM))
				type = FileNotifyType::FileDeleted;
			else
				continue;

			for (auto &func : itr->second.funcs)
			{
				if (func.func)
				{
					if (itr->second.directory)
					{
						auto notify_path = protocol + "://" + Path::join(func.path, current->name);
						func.func({ std::move(notify_path), type, func.virtual_handle });
					}
					else
						func.func({ protocol + "://" + func.path, type, func.virtual_handle });
				}
			}
		}
	}
#endif
}

void OSFilesystem::uninstall_notification(FileNotifyHandle handle)
{
#ifdef __linux__
	if (handle < 0)
		return;

	if (notify_fd < 0)
		return;

	//LOGI("Uninstalling notification: %d\n", handle);

	auto real = virtual_to_real.find(handle);
	if (real == end(virtual_to_real))
	{
		LOGE("unknown virtual inotify handler.\n");
		return;
	}

	auto itr = handlers.find(static_cast<int>(real->second));
	if (itr == end(handlers))
	{
		LOGE("unknown inotify handler.\n");
		return;
	}

	auto handler_instance = find_if(begin(itr->second.funcs), end(itr->second.funcs), [=](const VirtualHandler &v) {
		return v.virtual_handle == handle;
	});

	if (handler_instance == end(itr->second.funcs))
	{
		LOGE("unknown inotify handler path.\n");
		return;
	}

	itr->second.funcs.erase(handler_instance);

	if (itr->second.funcs.empty())
	{
		inotify_rm_watch(notify_fd, real->second);
		handlers.erase(itr);
	}

	virtual_to_real.erase(real);
#else
	(void)handle;
#endif
}

FileNotifyHandle OSFilesystem::install_notification(const std::string &path,
                                                    std::function<void (const FileNotifyInfo &)> func)
{
#ifdef __linux__
	//LOGI("Installing notification for: %s\n", path.c_str());
	if (notify_fd < 0)
		return -1;

	FileStat s = {};
	if (!stat(path, s))
	{
		LOGE("inotify: path doesn't exist.\n");
		return -1;
	}

	auto resolved_path = Path::join(base, path);
	int wd = inotify_add_watch(notify_fd, resolved_path.c_str(),
	                           IN_MOVE | IN_CLOSE_WRITE | IN_CREATE | IN_DELETE | IN_DELETE_SELF);

	if (wd < 0)
	{
		LOGE("Failed to create watch handle.\n");
		return -1;
	}

	// We could have different paths which look different but resolve to the same wd, so handle that.
	auto itr = handlers.find(wd);
	if (itr == end(handlers))
		handlers[wd] = { {{ path, std::move(func), ++virtual_handle }}, s.type == PathType::Directory };
	else
		itr->second.funcs.push_back({ path, std::move(func), ++virtual_handle });

	//LOGI("  Got handle: %d\n", virtual_handle);

	virtual_to_real[virtual_handle] = wd;
	return static_cast<FileNotifyHandle>(virtual_handle);
#else
	(void)path;
	(void)func;
	return -1;
#endif
}

bool OSFilesystem::remove(const std::string &path)
{
	auto resolved_path = Path::join(base, path);
	return unlink(resolved_path.c_str()) == 0;
}

bool OSFilesystem::move_yield(const std::string &dst, const std::string &src)
{
	auto resolved_dst = Path::join(base, dst);
	auto resolved_src = Path::join(base, src);
#if !defined(__linux__) || (defined(ANDROID) && (__ANDROID_API__ < __ANDROID_API_R__))
	// Workaround since Android does not have renameat2 until API level 30.
	// If we can exclusive create a new file, we can rename with replace somewhat safely.
	int fd = ::open(resolved_dst.c_str(), O_EXCL | O_RDWR | O_CREAT, 0600);
	if (fd >= 0)
	{
		::close(fd);
		return rename(resolved_src.c_str(), resolved_dst.c_str()) == 0;
	}
	else
		return false;
#else
	return renameat2(AT_FDCWD, resolved_src.c_str(), AT_FDCWD, resolved_dst.c_str(), RENAME_NOREPLACE) == 0;
#endif
}

bool OSFilesystem::move_replace(const std::string &dst, const std::string &src)
{
	auto resolved_dst = Path::join(base, dst);
	auto resolved_src = Path::join(base, src);
	return rename(resolved_src.c_str(), resolved_dst.c_str()) == 0;
}

std::vector<ListEntry> OSFilesystem::list(const std::string &path)
{
	auto directory = Path::join(base, path);
	DIR *dir = opendir(directory.c_str());
	if (!dir)
	{
		LOGE("Failed to open directory %s\n", path.c_str());
		return {};
	}

	std::vector<ListEntry> entries;
	struct dirent *entry;
	while ((entry = readdir(dir)))
	{
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			continue;

		auto joined_path = Path::join(path, entry->d_name);

		PathType type;
		if (entry->d_type == DT_DIR)
			type = PathType::Directory;
		else if (entry->d_type == DT_REG)
			type = PathType::File;
		else if (entry->d_type != DT_UNKNOWN && entry->d_type != DT_LNK)
			type = PathType::Special;
		else
		{
			FileStat s;
			if (!stat(joined_path, s))
			{
				LOGE("Failed to stat file: %s\n", joined_path.c_str());
				continue;
			}

			type = s.type;
		}
		entries.push_back({ std::move(joined_path), type });
	}
	closedir(dir);
	return entries;
}

bool OSFilesystem::stat(const std::string &path, FileStat &stat)
{
	auto resolved_path = Path::join(base, path);
	struct STAT64 buf = {};
	if (::STAT64(resolved_path.c_str(), &buf) < 0)
		return false;

	if (S_ISREG(buf.st_mode))
		stat.type = PathType::File;
	else if (S_ISDIR(buf.st_mode))
		stat.type = PathType::Directory;
	else
		stat.type = PathType::Special;

	stat.size = uint64_t(buf.st_size);
#ifdef __linux__
	stat.last_modified = buf.st_mtim.tv_sec * 1000000000ull + buf.st_mtim.tv_nsec;
#else
	stat.last_modified = buf.st_mtimespec.tv_sec * 1000000000ull + buf.st_mtimespec.tv_nsec;
#endif
	return true;
}

}
