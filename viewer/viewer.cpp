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

#include "scene_viewer_application.hpp"
#include "os_filesystem.hpp"
#include "cli_parser.hpp"

using namespace Util;

namespace Granite
{
Application *application_create(int argc, char **argv)
{
	if (argc < 1)
		return nullptr;

	GRANITE_APPLICATION_SETUP_FILESYSTEM();

	SceneViewerApplication::CLIConfig cli_config = {};
	std::string config;
	std::string quirks;
	std::string path;

#ifdef ANDROID
	config = "assets://config.json";
	quirks = "assets://quirks.json";
#endif
	path = "assets://scene.glb";

	CLICallbacks cbs;
	cbs.add("--config", [&](CLIParser &parser) { config = parser.next_string(); });
	cbs.add("--quirks", [&](CLIParser &parser) { quirks = parser.next_string(); });
	cbs.add("--timestamp", [&](CLIParser &) { cli_config.timestamp = true; });
	cbs.add("--camera-index", [&](CLIParser &parser) { cli_config.camera_index = int(parser.next_uint()); });
	cbs.add("--ocean", [&](CLIParser &) { cli_config.ocean = true; });
	cbs.default_handler = [&](const char *arg) { path = arg; };

	CLIParser parser(std::move(cbs), argc - 1, argv + 1);
	if (!parser.parse())
		return nullptr;

	if (path.empty())
	{
		LOGE("Need path to scene file.\n");
		return nullptr;
	}

	Granite::FileStat s = {};
	if (config.empty() && GRANITE_FILESYSTEM()->stat("assets://config.json", s) && s.type == Granite::PathType::File)
	{
		LOGI("Using default config from assets.\n");
		config = "assets://config.json";
	}

	if (quirks.empty() && GRANITE_FILESYSTEM()->stat("assets://quirks.json", s) && s.type == Granite::PathType::File)
	{
		LOGI("Using default quirks from assets.\n");
		quirks = "assets://quirks.json";
	}

	try
	{
		auto *app = new SceneViewerApplication(path, config, quirks, cli_config);
		app->loop_animations();
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
}
