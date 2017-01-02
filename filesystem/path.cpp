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
}
}