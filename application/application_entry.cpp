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

#include "application.hpp"
#include "filesystem.hpp"
#include "path_utils.hpp"

//#define USE_FP_EXCEPTIONS
#ifdef USE_FP_EXCEPTIONS
#include <fenv.h>
#endif

#if defined(_WIN32) && !defined(APPLICATION_ENTRY_HEADLESS)
#define USE_WINMAIN
#endif

#ifdef USE_WINMAIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <vector>
#endif

namespace Granite
{
// Make sure this is linked in.
void application_dummy()
{
}

// Alternatively, make sure this is linked in.
// Implementation is here to trick a linker to always let main() in static library work.
void application_setup_default_filesystem(const char *default_asset_directory)
{
	auto *filesystem = GRANITE_FILESYSTEM();
	if (filesystem)
		Filesystem::setup_default_filesystem(filesystem, default_asset_directory);
}
}

#ifdef USE_WINMAIN
int CALLBACK WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
#else
int main(int argc, char *argv[])
#endif
{
#ifdef USE_FP_EXCEPTIONS
	feenableexcept(FE_DIVBYZERO | FE_INVALID | FE_OVERFLOW);
#endif

#ifdef USE_WINMAIN
	int argc;
	wchar_t **wide_argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	std::vector<char *> argv_buffer(argc + 1);
	char **argv = nullptr;
	std::vector<std::string> argv_strings(argc);

	if (wide_argv)
	{
		argv = argv_buffer.data();
		for (int i = 0; i < argc; i++)
		{
			argv_strings[i] = Granite::Path::to_utf8(wide_argv[i]);
			argv_buffer[i] = const_cast<char *>(argv_strings[i].c_str());
		}
	}
#endif

#ifdef APPLICATION_ENTRY_HEADLESS
	int ret = Granite::application_main_headless(Granite::query_application_interface,
												 Granite::application_create,
												 argc, argv);
#else
	int ret = Granite::application_main(Granite::query_application_interface,
	                                    Granite::application_create,
	                                    argc, argv);
#endif

	return ret;
}

