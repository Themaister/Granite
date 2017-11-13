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

#include <texture_files.hpp>
#include "cli_parser.hpp"
#include "gli/save.hpp"
#include "gli/load.hpp"
#include "util.hpp"
#include "texture_compression.hpp"

using namespace std;
using namespace Granite;
using namespace Util;

static void print_help()
{
	LOGI("Usage: [--mipgen] [--quality [1-5]] [--format <format>] --output <out.ktx> <in.ktx>\n");
}

int main(int argc, char *argv[])
{
	string input_path;
	bool generate_mipmap = false;
	CompressorArguments args;

	args.mode = TextureMode::RGB;

	CLICallbacks cbs;
	cbs.add("--help", [&](CLIParser &parser) { print_help(); parser.end(); });
	cbs.add("--quality", [&](CLIParser &parser) { args.quality = parser.next_uint(); });
	cbs.add("--format", [&](CLIParser &parser) { args.format = string_to_format(parser.next_string()); });
	cbs.add("--output", [&](CLIParser &parser) { args.output = parser.next_string(); });
	cbs.add("--alpha", [&](CLIParser &) { args.mode = TextureMode::RGBA; });
	cbs.add("--mipgen", [&](CLIParser &) { generate_mipmap = true; });
	cbs.default_handler = [&](const char *arg) { input_path = arg; };
	cbs.error_handler = []() { print_help(); };
	CLIParser parser(move(cbs), argc - 1, argv + 1);

	if (!parser.parse())
		return 1;
	else if (parser.is_ended_state())
		return 0;

	if (args.format == gli::FORMAT_UNDEFINED)
		return 1;
	if (args.output.empty() || input_path.empty())
		return 1;

	Granite::ColorSpace color;
	switch (args.format)
	{
	case gli::FORMAT_RGBA8_UNORM_PACK8:
	case gli::FORMAT_RGBA_BP_UNORM_BLOCK16:
	case gli::FORMAT_RGB_DXT1_UNORM_BLOCK8:
	case gli::FORMAT_RGBA_DXT5_UNORM_BLOCK16:
	case gli::FORMAT_R_ATI1N_UNORM_BLOCK8:
	case gli::FORMAT_RG_ATI2N_UNORM_BLOCK16:
	case gli::FORMAT_RGB_BP_UFLOAT_BLOCK16:
	case gli::FORMAT_RGBA_ASTC_4X4_UNORM_BLOCK16:
	case gli::FORMAT_RGBA_ASTC_5X5_UNORM_BLOCK16:
	case gli::FORMAT_RGBA_ASTC_6X6_UNORM_BLOCK16:
	case gli::FORMAT_RGBA_ASTC_8X8_UNORM_BLOCK16:
		color = Granite::ColorSpace::Linear;
		break;

	default:
		color = Granite::ColorSpace::sRGB;
		break;
	}

	auto input = make_shared<gli::texture>(Granite::load_texture_from_file(input_path, color));

	if (input->empty())
	{
		LOGE("Failed to load texture %s.\n", input_path.c_str());
		return 1;
	}

	if (generate_mipmap)
		*input = Granite::generate_offline_mipmaps(*input);

	if (input->format() == gli::FORMAT_RGBA16_SFLOAT_PACK16)
		args.mode = TextureMode::HDR;

	if (args.format == gli::FORMAT_RGBA8_UNORM_PACK8 || args.format == gli::FORMAT_RGBA8_SRGB_PACK8)
	{
		if (!save_texture_to_file(args.output, *input))
		{
			LOGE("Failed to save texture: %s\n", args.output.c_str());
			return 1;
		}
		return 0;
	}

	ThreadGroup group;
	group.start(std::thread::hardware_concurrency());

	auto dummy = group.create_task();
	compress_texture(group, args, input, dummy);
	dummy->flush();
	group.wait_idle();
}