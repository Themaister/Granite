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

#include "os_filesystem.hpp"
#include "path.hpp"
#include "logging.hpp"
#include <stdexcept>
#include <algorithm>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#ifdef __linux__
#include <sys/inotify.h>
#endif

using namespace std;

namespace Granite
{

static bool ensure_directory_inner(const std::string &path)
{
	if (Path::is_root_path(path))
		return false;

	struct stat s;
	if (::stat(path.c_str(), &s) >= 0 && S_ISDIR(s.st_mode))
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

MMapFile *MMapFile::open(const std::string &path, FileMode mode)
{
	auto *file = new MMapFile();
	if (!file->init(path, mode))
	{
		delete file;
		return nullptr;
	}
	else
		return file;
}

bool MMapFile::init(const std::string &path, FileMode mode)
{
	int modeflags = 0;
	switch (mode)
	{
	case FileMode::ReadOnly:
		modeflags = O_RDONLY;
		break;

	case FileMode::WriteOnly:
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

	fd = ::open(path.c_str(), modeflags, 0640);
	if (fd < 0)
		return false;

	if (!reopen())
	{
		close(fd);
		return false;
	}

	return true;
}

void *MMapFile::map_write(size_t map_size)
{
	if (mapped)
		return nullptr;

	if (ftruncate(fd, map_size) < 0)
		return nullptr;
	this->size = map_size;

	mapped = mmap(nullptr, map_size, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
	if (mapped == MAP_FAILED)
	{
		LOGE("Failed to mmap: %s\n", strerror(errno));
		return nullptr;
	}
	return mapped;
}

void *MMapFile::map()
{
	if (mapped)
		return mapped;

	mapped = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (mapped == MAP_FAILED)
		return nullptr;
	return mapped;
}

size_t MMapFile::get_size()
{
	return size;
}

bool MMapFile::reopen()
{
	unmap();
	struct stat s;
	if (fstat(fd, &s) < 0)
		return false;

	if (uint64_t(s.st_size) > SIZE_MAX)
		return false;
	size = static_cast<size_t>(s.st_size);
	return true;
}

void MMapFile::unmap()
{
	if (mapped)
	{
		munmap(mapped, size);
		mapped = nullptr;
	}
}

MMapFile::~MMapFile()
{
	unmap();
	if (fd >= 0)
		close(fd);
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

unique_ptr<File> OSFilesystem::open(const std::string &path, FileMode mode)
{
	return unique_ptr<MMapFile>(MMapFile::open(Path::join(base, path), mode));
}

string OSFilesystem::get_filesystem_path(const string &path)
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
						func.func({ move(notify_path), type, func.virtual_handle });
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

FileNotifyHandle OSFilesystem::install_notification(const string &path,
                                                    function<void (const FileNotifyInfo &)> func)
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
		handlers[wd] = { {{ move(path), move(func), ++virtual_handle }}, s.type == PathType::Directory };
	else
		itr->second.funcs.push_back({ move(path), move(func), ++virtual_handle });

	//LOGI("  Got handle: %d\n", virtual_handle);

	virtual_to_real[virtual_handle] = wd;
	return static_cast<FileNotifyHandle>(virtual_handle);
#else
	(void)path;
	(void)func;
	return -1;
#endif
}

vector<ListEntry> OSFilesystem::list(const string &path)
{
	auto directory = Path::join(base, path);
	DIR *dir = opendir(directory.c_str());
	if (!dir)
	{
		LOGE("Failed to open directory %s\n", path.c_str());
		return {};
	}

	vector<ListEntry> entries;
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
		entries.push_back({ move(joined_path), type });
	}
	closedir(dir);
	return entries;
}

bool OSFilesystem::stat(const std::string &path, FileStat &stat)
{
	auto resolved_path = Path::join(base, path);
	struct stat buf;
	if (::stat(resolved_path.c_str(), &buf) < 0)
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
