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

	application_dummy();

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
	cbs.default_handler = [&](const char *arg) { path = arg; };

	auto self_dir = Path::basedir(Path::get_executable_path());
	auto assets_dir = Path::join(self_dir, "assets");
	auto builtin_dir = Path::join(self_dir, "builtin/assets");

#ifdef ASSET_DIRECTORY
	const char *asset_dir = getenv("ASSET_DIRECTORY");
	if (!asset_dir)
		asset_dir = ASSET_DIRECTORY;

	Global::filesystem()->register_protocol("assets", std::unique_ptr<FilesystemBackend>(new OSFilesystem(asset_dir)));
#endif

	FileStat s;
	if (Global::filesystem()->stat(assets_dir, s) && s.type == PathType::Directory)
	{
		Global::filesystem()->register_protocol("assets", std::make_unique<OSFilesystem>(assets_dir));
		LOGI("Redirecting filesystem \"assets\" to %s.\n", assets_dir.c_str());

		auto cache_dir = Path::join(self_dir, "cache");
		Global::filesystem()->register_protocol("cache", std::make_unique<OSFilesystem>(cache_dir));
		LOGI("Redirecting filesystem \"cache\" to %s.\n", cache_dir.c_str());
	}

	if (Global::filesystem()->stat(builtin_dir, s) && s.type == PathType::Directory)
	{
		Global::filesystem()->register_protocol("builtin", std::make_unique<OSFilesystem>(builtin_dir));
		LOGI("Redirecting filesystem \"builtin\" to %s.\n", builtin_dir.c_str());
	}

	CLIParser parser(std::move(cbs), argc - 1, argv + 1);
	if (!parser.parse())
		return nullptr;

	if (path.empty())
	{
		LOGE("Need path to scene file.\n");
		return nullptr;
	}

	try
	{
		auto *app = new SceneViewerApplication(path, config, quirks);
		//app->rescale_scene(5.0f);
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
