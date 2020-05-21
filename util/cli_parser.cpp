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

#include "cli_parser.hpp"
#include "logging.hpp"
#include <limits>
#include <stdexcept>

using namespace std;

namespace Util
{
CLIParser::CLIParser(CLICallbacks cbs_, int argc_, char *argv_[])
	: cbs(move(cbs_)), argc(argc_), argv(argv_)
{
}

bool CLIParser::parse()
{
	try
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
				if (itr == ::end(cbs.callbacks))
				{
					if (unknown_argument_is_default)
						cbs.default_handler(next);
					else
						throw std::invalid_argument("Invalid argument");
				}
				else
					itr->second(*this);
			}
		}

		return true;
	}
	catch (const std::exception &e)
	{
		LOGE("Failed to parse arguments: %s\n", e.what());
		if (cbs.error_handler)
		{
			cbs.error_handler();
		}
		return false;
	}
}

void CLIParser::end()
{
	ended_state = true;
}

unsigned CLIParser::next_uint()
{
	if (!argc)
	{
		throw invalid_argument("Tried to parse uint, but nothing left in arguments");
	}

	auto val = stoul(*argv);
	if (val > numeric_limits<unsigned>::max())
	{
		throw invalid_argument("next_uint() out of range");
	}

	argc--;
	argv++;

	return unsigned(val);
}

double CLIParser::next_double()
{
	if (!argc)
	{
		throw invalid_argument("Tried to parse double, but nothing left in arguments");
	}

	double val = stod(*argv);

	argc--;
	argv++;

	return val;
}

const char *CLIParser::next_string()
{
	if (!argc)
	{
		throw invalid_argument("Tried to parse string, but nothing left in arguments");
	}

	const char *ret = *argv;
	argc--;
	argv++;
	return ret;
}
}
