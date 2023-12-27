/* Copyright (c) 2017-2023 Hans-Kristian Arntzen
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

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "environment.hpp"
#include <string>
#include <stdlib.h>

namespace Util
{
bool get_environment(const char *env, std::string &str)
{
#ifdef _WIN32
	char buf[4096];
	DWORD count = GetEnvironmentVariableA(env, buf, sizeof(buf));
	if (count)
	{
		str = { buf, buf + count };
		return true;
	}
	else
		return false;
#else
	if (const char *v = getenv(env))
	{
		str = v;
		return true;
	}
	else
		return false;
#endif
}

void set_environment(const char *env, const char *value)
{
#ifdef _WIN32
	SetEnvironmentVariableA(env, value);
#else
	setenv(env, value, 1);
#endif
}

std::string get_environment_string(const char *env, const char *default_value)
{
	std::string v;
	if (!get_environment(env, v))
		v = default_value;
	return v;
}

unsigned get_environment_uint(const char *env, unsigned default_value)
{
	unsigned value = default_value;
	std::string v;
	if (get_environment(env, v))
		value = unsigned(std::stoul(v));
	return value;
}

int get_environment_int(const char *env, int default_value)
{
	int value = default_value;
	std::string v;
	if (get_environment(env, v))
		value = int(std::stol(v));
	return value;
}

bool get_environment_bool(const char *env, bool default_value)
{
	return get_environment_int(env, int(default_value)) != 0;
}
}
