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

#include "path.hpp"
#include "util.hpp"
#include <algorithm>

using namespace std;

namespace Granite
{
namespace Path
{
string canonicalize_path(const string &path)
{
	string transformed;
	transformed.resize(path.size());
	transform(begin(path), end(path), begin(transformed), [](char c) -> char { return c == '\\' ? '/' : c; });
	auto data = Util::split_no_empty(transformed, "/");

	vector<string> result;
	for (auto &i : data)
	{
		if (i == "..")
		{
			if (!result.empty())
				result.pop_back();
		}
		else
			result.push_back(move(i));
	}

	string res;
	for (auto &i : result)
	{
		if (&i != result.data())
			res += "/";
		res += i;
	}
	return res;
}

static size_t find_last_slash(const string &str)
{
#ifdef _WIN32
	auto index = str.find_last_of("/\\");
#else
	auto index = str.find_last_of('/');
#endif
	return index;
}

bool is_abspath(const string &path)
{
	if (path.empty())
		return false;
	if (path.front() == '/')
		return true;

#ifdef _WIN32
	{
		auto index = std::min(path.find(":/"), path.find(":\\"));
		if (index != string::npos)
			return true;
	}
#endif

	return path.find("://") != string::npos;
}

bool is_root_path(const string &path)
{
	if (path.empty())
		return false;

	if (path.front() == '/' && path.size() == 1)
		return true;

#ifdef _WIN32
	{
		auto index = std::min(path.find(":/"), path.find(":\\"));
		if (index != string::npos && (index + 2) == path.size())
			return true;
	}
#endif

	auto index = path.find("://");
	return index != string::npos && (index + 3) == path.size();
}

string join(const string &base, const string &path)
{
	if (base.empty())
		return path;
	if (path.empty())
		return base;

	if (is_abspath(path))
		return path;

	auto index = find_last_slash(base);
	bool need_slash = index != base.size() - 1;
	return Util::join(base, need_slash ? "/" : "", path);
}

string basedir(const string &path)
{
	if (path.empty())
		return "";

	if (is_root_path(path))
		return path;

	auto index = find_last_slash(path);
	if (index == string::npos)
		return ".";

	// Preserve the first slash.
	if (index == 0 && is_abspath(path))
		index++;

	auto ret = path.substr(0, index + 1);
	if (!is_root_path(ret))
		ret.pop_back();
	return ret;
}

string basename(const string &path)
{
	if (path.empty())
		return "";

	auto index = find_last_slash(path);
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

	auto index = find_last_slash(path);
	if (index == string::npos)
		return make_pair(string("."), path);

	auto base = path.substr(index + 1, string::npos);
	return make_pair(path.substr(0, index), base);
}

pair<string, string> protocol_split(const string &path)
{
	if (path.empty())
		return make_pair(string(""), string(""));

	auto index = path.find("://");
	if (index == string::npos)
		return make_pair(string(""), path);

	return make_pair(path.substr(0, index), path.substr(index + 3, string::npos));
}
}
}