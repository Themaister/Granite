/* Copyright (c) 2017 Hans-Kristian Arntzen
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

#include "gltf.hpp"
#include "gltf_export.hpp"
#include "util.hpp"
#include "cli_parser.hpp"

using namespace Granite;
using namespace Util;
using namespace std;

static SceneFormats::TextureCompression string_to_compression(const string &fmt)
{
	if (fmt == "bc7")
		return SceneFormats::TextureCompression::BC7;
	else if (fmt == "bc3")
		return SceneFormats::TextureCompression::BC3;
	else if (fmt == "bc1")
		return SceneFormats::TextureCompression::BC1;
	else if (fmt == "bc6h")
		return SceneFormats::TextureCompression::BC6H;
	else if (fmt == "astc_4x4")
		return SceneFormats::TextureCompression::ASTC4x4;
	else if (fmt == "astc_5x5")
		return SceneFormats::TextureCompression::ASTC5x5;
	else if (fmt == "astc_6x6")
		return SceneFormats::TextureCompression::ASTC6x6;
	else if (fmt == "astc_8x8")
		return SceneFormats::TextureCompression::ASTC8x8;
	else
	{
		LOGE("Unrecognized format, using uncompressed.\n");
		return SceneFormats::TextureCompression::Uncompressed;
	}
}

static void print_help()
{
	LOGI("Usage: [--output <out.glb>] [--texcomp <type>]\n");
	LOGI("[--environment-reflection <path>] [--environment-cube <path>]\n");
	LOGI("[--environment-irradiance <path>] [--environment-texcomp <type>]\n");
	LOGI("[--environment-texcomp-quality <1 (fast) - 5 (slow)>]\n");
	LOGI("[--threads <num threads>]\n");
	LOGI("[--fog-color R G B] [--fog-falloff falloff]\n");
	LOGI("[--texcomp-quality <1 (fast) - 5 (slow)>] input.gltf\n");
}

int main(int argc, char *argv[])
{
	struct Arguments
	{
		string input;
		string output;
	} args;

	SceneFormats::ExportOptions options;

	CLICallbacks cbs;
	cbs.add("--output", [&](CLIParser &parser) { args.output = parser.next_string(); });
	cbs.add("--texcomp", [&](CLIParser &parser) { options.compression = string_to_compression(parser.next_string()); });
	cbs.add("--texcomp-quality", [&](CLIParser &parser) { options.texcomp_quality = parser.next_uint(); });
	cbs.add("--environment-cube", [&](CLIParser &parser) { options.environment.cube = parser.next_string(); });
	cbs.add("--environment-reflection", [&](CLIParser &parser) { options.environment.reflection = parser.next_string(); });
	cbs.add("--environment-irradiance", [&](CLIParser &parser) { options.environment.irradiance = parser.next_string(); });
	cbs.add("--environment-texcomp", [&](CLIParser &parser) { options.environment.compression = string_to_compression(parser.next_string()); });
	cbs.add("--environment-texcomp-quality", [&](CLIParser &parser) { options.environment.texcomp_quality = parser.next_uint(); });

	cbs.add("--fog-color", [&](CLIParser &parser) {
		for (unsigned i = 0; i < 3; i++)
			options.environment.fog_color[i] = parser.next_double();
	});

	cbs.add("--fog-falloff", [&](CLIParser &parser) {
		options.environment.fog_falloff = parser.next_double();
	});

	cbs.add("--threads", [&](CLIParser &parser) { options.threads = parser.next_uint(); });
	cbs.add("--help", [](CLIParser &parser) { print_help(); parser.end(); });
	cbs.default_handler = [&](const char *arg) { args.input = arg; };
	CLIParser cli_parser(move(cbs), argc - 1, argv + 1);
	if (!cli_parser.parse())
		return 1;
	else if (cli_parser.is_ended_state())
		return 0;

	if (args.input.empty() || args.output.empty())
	{
		print_help();
		return 1;
	}

	GLTF::Parser parser(args.input);

	SceneFormats::SceneInformation info;
	info.animations = parser.get_animations();
	info.cameras = parser.get_cameras();
	info.lights = parser.get_lights();
	info.materials = parser.get_materials();
	info.meshes = parser.get_meshes();
	info.nodes = parser.get_nodes();
	info.skins = parser.get_skins();

	if (!SceneFormats::export_scene_to_glb(info, args.output, options))
	{
		LOGE("Failed to export scene to GLB.\n");
		return 1;
	}

	return 0;
}
