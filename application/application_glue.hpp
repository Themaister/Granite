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

#pragma once
#include <stddef.h>
#include <stdint.h>

namespace Granite
{
enum class ApplicationQuery
{
	DefaultManagerFlags
};

class Application;

int application_main(
		bool (*query_application_interface)(ApplicationQuery, void *data, size_t size),
		Application *(*create_application)(int, char **),
		int argc, char **argv);

int application_main_headless(
		bool (*query_application_interface)(ApplicationQuery, void *data, size_t size),
		Application *(*create_application)(int, char **),
		int argc, char **argv);

extern Application *application_create(int argc, char *argv[]);

struct ApplicationQueryDefaultManagerFlags
{
	uint32_t manager_feature_flags;
};

extern bool query_application_interface(ApplicationQuery query, void *data, size_t size);

// Call this or setup_default_filesystem to ensure application-main is linked in correctly without having to mess around
// with -Wl,--whole-archive.
void application_dummy();

void application_setup_default_filesystem(const char *default_asset_directory);
}

#ifdef ASSET_DIRECTORY
#define GRANITE_APPLICATION_SETUP_FILESYSTEM() ::Granite::application_setup_default_filesystem(ASSET_DIRECTORY)
#else
#define GRANITE_APPLICATION_SETUP_FILESYSTEM() ::Granite::application_setup_default_filesystem(nullptr)
#endif

#define GRANITE_APPLICATION_DECL_DEFAULT_QUERY() namespace Granite { bool query_application_interface(Granite::ApplicationQuery, void *, size_t) { return false; } }
