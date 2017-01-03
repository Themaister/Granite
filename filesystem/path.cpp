#include "path.hpp"
#include "util.hpp"

using namespace std;

namespace Granite
{
namespace Path
{
string join(const string &base, const string &path)
{
	if (base.empty())
		return path;
	if (path.empty())
		return base;

	if (path.front() == '/')
		return path;

	auto index = base.find_last_of('/');
	bool need_slash = index != base.size() - 1;
	return Util::join(base, need_slash ? "/" : "", path);
}

string basedir(const string &path)
{
	if (path.empty())
		return "";
	auto index = path.find_last_of('/');
	if (index == string::npos)
		return ".";

	return path.substr(0, index);
}

string basename(const string &path)
{
	if (path.empty())
		return "";
	auto index = path.find_last_of('/');
	if (index == string::npos)
		return path;

	auto base = path.substr(index + 1, string::npos);
	return base;
}

string relpath(const string &base, const string &path)
{
	return Path::join(basedir(base), path);
}

string ext(const string &path)
{
	auto index = path.find_last_of('.');
	if (index == string::npos)
		return "";
	else
		return path.substr(index + 1, string::npos);
}

pair<string, string> split(const string &path)
{
	if (path.empty())
		return make_pair(string("."), string("."));
	auto index = path.find_last_of('/');
	if (index == string::npos)
		return make_pair(string("."), path);

	auto base = path.substr(index + 1, string::npos);
	return make_pair(path.substr(0, index), base);
}
}
}