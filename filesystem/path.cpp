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

using namespace std;

namespace Granite
{
namespace Path
{
bool is_abspath(const string &path)
{
	if (path.empty())
		return false;
	if (path.front() == '/')
		return true;

	return path.find("://") != string::npos;
}

bool is_root_path(const string &path)
{
	if (path.empty())
		return false;

	if (path.front() == '/' && path.size() == 1)
		return true;

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

	auto index = base.find_last_of('/');
	bool need_slash = index != base.size() - 1;
	return Util::join(base, need_slash ? "/" : "", path);
}

string basedir(const string &path)
{
	if (path.empty())
		return "";

	if (is_root_path(path))
		return path;

	auto index = path.find_last_of('/');
	if (index == string::npos)
		return ".";

	// Preserve the first slash.
	if (index == 0 && is_abspath(path))
		index++;

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