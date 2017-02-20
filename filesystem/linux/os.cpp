#include "os.hpp"
#include "../path.hpp"
#include "util.hpp"
#include <stdexcept>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <sys/inotify.h>
#include <fcntl.h>
#include <sys/mman.h>

using namespace std;

namespace Granite
{

MMapFile::MMapFile(const std::string &path, FileMode mode)
{
	int modeflags;
	switch (mode)
	{
	case FileMode::ReadOnly:
		modeflags = O_RDONLY;
		break;

	case FileMode::WriteOnly:
		modeflags = O_RDWR | O_CREAT; // Need read access for mmap.
		break;

	case FileMode::ReadWrite:
		modeflags = O_RDWR;
		break;
	}
	fd = open(path.c_str(), modeflags, 0640);
	if (fd < 0)
	{
		LOGE("open(), error: %s\n", strerror(errno));
		throw runtime_error("MMapFile failed to open file");
	}

	if (!reopen())
	{
		close(fd);
		throw runtime_error("fstat failed");
	}
}

void *MMapFile::map_write(size_t size)
{
	if (mapped)
		return nullptr;

	if (ftruncate(fd, size) < 0)
		return nullptr;
	this->size = size;

	mapped = mmap(nullptr, size, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
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

size_t MMapFile::get_size() const
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

OSFilesystem::OSFilesystem(const std::string &base)
	: base(base)
{
	notify_fd = inotify_init1(IN_NONBLOCK);
	if (notify_fd < 0)
	{
		LOGE("Failed to init inotify.\n");
		throw runtime_error("inotify");
	}
}

OSFilesystem::~OSFilesystem()
{
	if (notify_fd > 0)
	{
		for (auto &handler : handlers)
			inotify_rm_watch(notify_fd, handler.first);
		close(notify_fd);
	}
}

unique_ptr<File> OSFilesystem::open(const std::string &path, FileMode mode)
{
	try
	{
		unique_ptr<File> file(new MMapFile(Path::join(base, path), mode));
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
	for (;;)
	{
		char buffer[sizeof(inotify_event) + NAME_MAX + 1] alignas(inotify_event);
		ssize_t ret = read(notify_fd, buffer, sizeof(buffer));

		if (ret < 0)
		{
			if (errno != EAGAIN)
				throw runtime_error("failed to read inotify fd");
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

			if (itr->second.func)
			{
				if (itr->second.directory)
				{
					auto notify_path = protocol + Path::join(itr->second.path, current->name);
					itr->second.func({ move(notify_path), type });
				}
				else
					itr->second.func({ protocol + itr->second.path, type });
			}
		}
	}
}

void OSFilesystem::uninstall_notification(FileNotifyHandle handle)
{
	auto itr = handlers.find(static_cast<int>(handle));
	if (itr == end(handlers))
		throw runtime_error("unknown inotify handler");

	auto path_itr = path_to_handler.find(itr->second.path);
	if (path_itr == end(path_to_handler))
		throw runtime_error("unknown inotify handler path");

	path_to_handler.erase(path_itr);
	handlers.erase(itr);
}

FileNotifyHandle OSFilesystem::find_notification(const std::string &path) const
{
	auto itr = path_to_handler.find(path);
	if (itr != end(path_to_handler))
		return itr->second;
	else
		return -1;
}

FileNotifyHandle OSFilesystem::install_notification(const string &path,
                                                    function<void (const FileNotifyInfo &)> func)
{
	if (find_notification(path) >= 0)
		throw runtime_error("cannot add multiple watches on same file/directory.");

	FileStat s;
	if (!stat(path, s))
		throw runtime_error("path doesn't exist");

	auto resolved_path = Path::join(base, path);
	int wd = inotify_add_watch(notify_fd, resolved_path.c_str(),
	                           IN_MOVE | IN_CLOSE_WRITE | IN_CREATE | IN_DELETE | IN_DELETE_SELF);

	if (wd < 0)
		throw runtime_error("inotify_add_watch");

	handlers[wd] = { path, move(func), s.type == PathType::Directory };
	path_to_handler[path] = wd;
	return static_cast<FileNotifyHandle>(wd);
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
	return true;
}

}
