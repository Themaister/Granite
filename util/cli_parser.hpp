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

#include <functional>
#include <string>
#include <unordered_map>
#include <utility>

namespace Util
{
class CLIParser;

struct CLICallbacks
{
	void add(const char *cli, const std::function<void(CLIParser &)> &func)
	{
		callbacks[cli] = func;
	}

	std::unordered_map<std::string, std::function<void(CLIParser &)>> callbacks;
	std::function<void()> error_handler;
	std::function<void(const char *)> default_handler;
};

class CLIParser
{
public:
	CLIParser(CLICallbacks cbs_, int argc_, char *argv_[]);

	bool parse();

	void end();

	unsigned next_uint();

	double next_double();

	const char *next_string();

	bool is_ended_state() const
	{
		return ended_state;
	}

	void ignore_unknown_arguments()
	{
		unknown_argument_is_default = true;
	}

private:
	CLICallbacks cbs;
	int argc;
	char **argv;
	bool ended_state = false;
	bool unknown_argument_is_default = false;
};
}
