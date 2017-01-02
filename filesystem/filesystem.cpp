#include "filesystem.hpp"
#include "fs/os.hpp"

using namespace std;

namespace Granite
{
Filesystem &Filesystem::get()
{
	static OSFilesystem absolute_filesystem("/");
	return absolute_filesystem;
}

vector<Filesystem::Entry> Filesystem::walk(const std::string &path)
{
	auto entries = list(path);
	vector<Filesystem::Entry> final_entries;
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
}