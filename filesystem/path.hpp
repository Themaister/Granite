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

#pragma once
#include <string>
#include <utility>

namespace Granite
{
namespace Path
{
std::string join(const std::string &base, const std::string &path);
std::string basedir(const std::string &path);
std::string basename(const std::string &path);
std::pair<std::string, std::string> split(const std::string &path);
std::string relpath(const std::string &base, const std::string &path);
std::string ext(const std::string &path);
std::pair<std::string, std::string> protocol_split(const std::string &path);
bool is_abspath(const std::string &path);
bool is_root_path(const std::string &path);
std::string canonicalize_path(const std::string &path);
std::string enforce_protocol(const std::string &path);
std::string get_executable_path();

#ifdef _WIN32
std::string to_utf8(const wchar_t *wstr, size_t len);
std::wstring to_utf16(const char *str, size_t len);
std::string to_utf8(const std::wstring &wstr);
std::wstring to_utf16(const std::string &str);
#endif
}
}