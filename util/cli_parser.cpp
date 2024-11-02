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
#include "cli_parser.hpp"
#include "logging.hpp"
#include <limits>
#include <stdexcept>
#include <vector>

namespace Util
{
CLIParser::CLIParser(CLICallbacks cbs_, int argc_, char *argv_[])
	: cbs(std::move(cbs_)), argc(argc_), argv(argv_)
{
}

bool CLIParser::parse()
{
#ifdef __EXCEPTIONS
	try
#endif
	{
		while (argc && !ended_state)
		{
			const char *next = *argv++;
			argc--;

			if (*next != '-' && cbs.default_handler)
			{
				cbs.default_handler(next);
			}
			else
			{
				auto itr = cbs.callbacks.find(next);
				if (itr == std::end(cbs.callbacks))
				{
					if (unknown_argument_is_default)
						cbs.default_handler(next);
#ifdef __EXCEPTIONS
					else
						throw std::invalid_argument("Invalid argument");
#else
					return false;
#endif
				}
				else
					itr->second(*this);
			}
		}

		return true;
	}
#ifdef __EXCEPTIONS
	catch (const std::exception &e)
	{
		LOGE("Failed to parse arguments: %s\n", e.what());
		if (cbs.error_handler)
		{
			cbs.error_handler();
		}
		return false;
	}
#endif
}

void CLIParser::end()
{
	ended_state = true;
}

unsigned CLIParser::next_uint()
{
	if (!argc)
	{
#ifdef __EXCEPTIONS
		throw std::invalid_argument("Tried to parse uint, but nothing left in arguments");
#else
		return 0;
#endif
	}

	auto val = std::stoul(*argv);
	if (val > std::numeric_limits<unsigned>::max())
	{
#ifdef __EXCEPTIONS
		throw std::invalid_argument("next_uint() out of range");
#else
		return 0;
#endif
	}

	argc--;
	argv++;

	return unsigned(val);
}

double CLIParser::next_double()
{
	if (!argc)
	{
#ifdef __EXCEPTIONS
		throw std::invalid_argument("Tried to parse double, but nothing left in arguments");
#else
		return 0;
#endif
	}

	double val = std::stod(*argv);

	argc--;
	argv++;

	return val;
}

const char *CLIParser::next_string()
{
	if (!argc)
	{
#ifdef __EXCEPTIONS
		throw std::invalid_argument("Tried to parse string, but nothing left in arguments");
#else
		return nullptr;
#endif
	}

	const char *ret = *argv;
	argc--;
	argv++;
	return ret;
}

bool parse_cli_filtered(CLICallbacks cbs, int &argc, char *argv[], int &exit_code)
{
	if (argc == 0)
	{
		exit_code = 1;
		return false;
	}

	exit_code = 0;
	std::vector<char *> filtered;
	filtered.reserve(argc + 1);
	filtered.push_back(argv[0]);

	cbs.default_handler = [&](const char *arg) { filtered.push_back(const_cast<char *>(arg)); };

	CLIParser parser(std::move(cbs), argc - 1, argv + 1);
	parser.ignore_unknown_arguments();

	if (!parser.parse())
	{
		exit_code = 1;
		return false;
	}
	else if (parser.is_ended_state())
	{
		exit_code = 0;
		return false;
	}

	argc = int(filtered.size());
	std::copy(filtered.begin(), filtered.end(), argv);
	argv[argc] = nullptr;
	return true;
}
}
