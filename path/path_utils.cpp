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

#define NOMINMAX
#include "path_utils.hpp"
#include "string_helpers.hpp"
#include <algorithm>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#ifdef __linux__
#include <linux/limits.h>
#endif
#endif

namespace Granite
{
namespace Path
{
std::string enforce_protocol(const std::string &path)
{
	if (path.empty())
		return "";

	auto index = path.find("://");
	if (index == std::string::npos)
		return std::string("file://") + path;
	else
		return path;
}

std::string canonicalize_path(const std::string &path)
{
	std::string transformed;
	transformed.resize(path.size());
	transform(begin(path), end(path), begin(transformed), [](char c) -> char { return c == '\\' ? '/' : c; });
	auto data = Util::split_no_empty(transformed, "/");

	std::vector<std::string> result;
	for (auto &i : data)
	{
		if (i == "..")
		{
			if (!result.empty())
				result.pop_back();
		}
		else if (i != ".")
			result.push_back(std::move(i));
	}

	std::string res;
	for (auto &i : result)
	{
		if (&i != result.data())
			res += "/";
		res += i;
	}
	return res;
}

static size_t find_last_slash(const std::string &str)
{
#ifdef _WIN32
	auto index = str.find_last_of("/\\");
#else
	auto index = str.find_last_of('/');
#endif
	return index;
}

bool is_abspath(const std::string &path)
{
	if (path.empty())
		return false;
	if (path.front() == '/')
		return true;

#ifdef _WIN32
	{
		auto index = std::min(path.find(":/"), path.find(":\\"));
		if (index != std::string::npos)
			return true;
	}
#endif

	return path.find("://") != std::string::npos;
}

bool is_root_path(const std::string &path)
{
	if (path.empty())
		return false;

	if (path.front() == '/' && path.size() == 1)
		return true;

#ifdef _WIN32
	{
		auto index = std::min(path.find(":/"), path.find(":\\"));
		if (index != std::string::npos && (index + 2) == path.size())
			return true;
	}
#endif

	auto index = path.find("://");
	return index != std::string::npos && (index + 3) == path.size();
}

std::string join(const std::string &base, const std::string &path)
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

std::string basedir(const std::string &path)
{
	if (path.empty())
		return "";

	if (is_root_path(path))
		return path;

	auto index = find_last_slash(path);
	if (index == std::string::npos)
		return ".";

	// Preserve the first slash.
	if (index == 0 && is_abspath(path))
		index++;

	auto ret = path.substr(0, index + 1);
	if (!is_root_path(ret))
		ret.pop_back();
	return ret;
}

std::string basename(const std::string &path)
{
	if (path.empty())
		return "";

	auto index = find_last_slash(path);
	if (index == std::string::npos)
		return path;

	auto base = path.substr(index + 1, std::string::npos);
	return base;
}

std::string relpath(const std::string &base, const std::string &path)
{
	return Path::join(basedir(base), path);
}

std::string ext(const std::string &path)
{
	auto index = path.find_last_of('.');
	if (index == std::string::npos)
		return "";
	else
		return path.substr(index + 1, std::string::npos);
}

std::pair<std::string, std::string> split(const std::string &path)
{
	if (path.empty())
		return make_pair(std::string("."), std::string("."));

	auto index = find_last_slash(path);
	if (index == std::string::npos)
		return make_pair(std::string("."), path);

	auto base = path.substr(index + 1, std::string::npos);
	return make_pair(path.substr(0, index), base);
}

std::pair<std::string, std::string> protocol_split(const std::string &path)
{
	if (path.empty())
		return make_pair(std::string(""), std::string(""));

	auto index = path.find("://");
	if (index == std::string::npos)
		return make_pair(std::string(""), path);

	return make_pair(path.substr(0, index), path.substr(index + 3, std::string::npos));
}

std::string get_executable_path()
{
#ifdef _WIN32
	wchar_t target[4096];
	DWORD ret = GetModuleFileNameW(GetModuleHandle(nullptr), target, sizeof(target) / sizeof(wchar_t));
	return canonicalize_path(Path::to_utf8(target, ret));
#else
	pid_t pid = getpid();
	static const char *exts[] = { "exe", "file", "a.out" };
	char link_path[PATH_MAX];
	char target[PATH_MAX];

	for (auto *ext : exts)
	{
		snprintf(link_path, sizeof(link_path), "/proc/%u/%s",
		         unsigned(pid), ext);
		ssize_t ret = readlink(link_path, target, sizeof(target) - 1);
		if (ret >= 0)
		{
			target[ret] = '\0';
			return std::string(target);
		}
	}

	return "";
#endif
}

#ifdef _WIN32
std::string to_utf8(const wchar_t *wstr, size_t len)
{
	std::vector<char> char_buffer;
	auto ret = WideCharToMultiByte(CP_UTF8, 0, wstr, len, nullptr, 0, nullptr, nullptr);
	if (ret < 0)
		return "";
	char_buffer.resize(ret);
	WideCharToMultiByte(CP_UTF8, 0, wstr, len, char_buffer.data(), char_buffer.size(), nullptr, nullptr);
	return std::string(char_buffer.data(), char_buffer.size());
}

std::wstring to_utf16(const char *str, size_t len)
{
	std::vector<wchar_t> wchar_buffer;
	auto ret = MultiByteToWideChar(CP_UTF8, 0, str, len, nullptr, 0);
	if (ret < 0)
		return L"";
	wchar_buffer.resize(ret);
	MultiByteToWideChar(CP_UTF8, 0, str, len, wchar_buffer.data(), wchar_buffer.size());
	return std::wstring(wchar_buffer.data(), wchar_buffer.size());
}

std::string to_utf8(const std::wstring &wstr)
{
	return to_utf8(wstr.data(), wstr.size());
}

std::wstring to_utf16(const std::string &str)
{
	return to_utf16(str.data(), str.size());
}
#endif
}
}
