#include "os.hpp"
#include "path.hpp"
#include "util.hpp"
#include <stdexcept>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>

using namespace std;

namespace Granite
{

OSFilesystem::OSFilesystem(const std::string &base)
	: base(base)
{
}

OSFilesystem::~OSFilesystem()
{
}

unique_ptr<File> OSFilesystem::open(const std::string &path)
{
	return {};
}

void OSFilesystem::uninstall_notification(Filesystem::NotifyHandle handle)
{

}

Filesystem::NotifyHandle OSFilesystem::install_notification(const string &path,
                                                            function<void (const Filesystem::NotifyInfo &)> func)
{
	return 0;
}

vector<Filesystem::Entry> OSFilesystem::list(const string &path)
{
	auto directory = Path::join(base, path);
	DIR *dir = opendir(directory.c_str());
	if (!dir)
	{
		LOG("Failed to open directory %s\n", path.c_str());
		return {};
	}

	vector<Filesystem::Entry> entries;
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
			Filesystem::Stat s;
			if (!stat(joined_path, s))
			{
				LOG("Failed to stat file: %s\n", joined_path.c_str());
				continue;
			}

			type = s.type;
		}
		entries.push_back({ move(joined_path), type });
	}
	closedir(dir);
	return entries;
}

bool OSFilesystem::stat(const std::string &path, Stat &stat)
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
