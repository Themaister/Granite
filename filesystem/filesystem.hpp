#pragma once
#include <vector>
#include <string>
#include <memory>

namespace Granite
{
class File
{
public:
	virtual ~File() = default;

	virtual void *map() = 0;
	virtual void unmap() = 0;
	virtual size_t get_size() const = 0;
};

class Filesystem
{
public:
	Filesystem();
	virtual ~Filesystem() = default;

	enum class PathType
	{
		File,
		Directory,
		Special
	};

	struct DirEntry
	{
		std::string path;
		PathType type;
	};

	struct Stat
	{
		uint64_t size;
		PathType type;
	};

	virtual std::vector<DirEntry> list(const std::string &path) = 0;
	virtual std::unique_ptr<File> open(const std::string &path) = 0;
	virtual bool stat(const std::string &path, Stat &stat) = 0;
};
}