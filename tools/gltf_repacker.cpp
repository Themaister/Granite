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

int main(int argc, char *argv[])
{
	struct Arguments
	{
		string input;
		string output;
	} args;

	CLICallbacks cbs;
	cbs.add("--input", [&](CLIParser &parser) { args.input = parser.next_string(); });
	cbs.add("--output", [&](CLIParser &parser) { args.output = parser.next_string(); });
	CLIParser cli_parser(move(cbs), argc - 1, argv + 1);
	if (!cli_parser.parse())
		return 1;
	else if (cli_parser.is_ended_state())
		return 0;

	GLTF::Parser parser(args.input);

	SceneFormats::SceneInformation info;
	info.animations = parser.get_animations();
	info.cameras = parser.get_cameras();
	info.lights = parser.get_lights();
	info.materials = parser.get_materials();
	info.meshes = parser.get_meshes();
	info.nodes = parser.get_nodes();
	info.skins = parser.get_skins();

	if (!SceneFormats::export_scene_to_glb(info, args.output))
	{
		LOGE("Failed to export scene to GLB.\n");
		return 1;
	}

	return 0;
}