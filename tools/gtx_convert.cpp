/* Copyright (c) 2017-2019 Hans-Kristian Arntzen
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
#include "util.hpp"
#include "texture_compression.hpp"
#include "memory_mapped_texture.hpp"
#include "texture_utils.hpp"

using namespace std;
using namespace Granite;
using namespace Granite::SceneFormats;
using namespace Util;

static void print_help()
{
	LOGI("Usage: \n"
	     "\t[--mipgen]\n"
	     "\t[--fixup-alpha]\n"
	     "\t[--deferred-mipgen]\n"
	     "\t[--quality [1-5]]\n"
	     "\t[--format <format>]\n"
	     "\t--output <out.gtx>\n"
	     "\t<in.gtx>\n");
}

int main(int argc, char *argv[])
{
	Global::init();

	string input_path;
	bool generate_mipmap = false;
	bool deferred_generate_mipmap = false;
	bool fixup_alpha = false;
	CompressorArguments args;

	args.mode = TextureMode::RGB;

	CLICallbacks cbs;
	cbs.add("--help", [&](CLIParser &parser) { print_help(); parser.end(); });
	cbs.add("--quality", [&](CLIParser &parser) { args.quality = parser.next_uint(); });
	cbs.add("--format", [&](CLIParser &parser) { args.format = string_to_format(parser.next_string()); });
	cbs.add("--output", [&](CLIParser &parser) { args.output = parser.next_string(); });
	cbs.add("--alpha", [&](CLIParser &) { args.mode = TextureMode::RGBA; });
	cbs.add("--fixup-alpha", [&](CLIParser &) { fixup_alpha = true; });
	cbs.add("--mipgen", [&](CLIParser &) { generate_mipmap = true; });
	cbs.add("--deferred-mipgen", [&](CLIParser &) { deferred_generate_mipmap = true; });
	cbs.default_handler = [&](const char *arg) { input_path = arg; };
	cbs.error_handler = []() { print_help(); };
	CLIParser parser(move(cbs), argc - 1, argv + 1);

	if (!parser.parse())
		return 1;
	else if (parser.is_ended_state())
		return 0;

	if (args.format == VK_FORMAT_UNDEFINED)
	{
		LOGE("Must provide a format.\n");
		return 1;
	}

	if (args.output.empty() || input_path.empty())
	{
		LOGE("Must provide input and output paths.\n");
		return 1;
	}

	Granite::ColorSpace color;
	switch (args.format)
	{
	case VK_FORMAT_R8G8B8A8_UNORM:
	case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
	case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
	case VK_FORMAT_BC3_UNORM_BLOCK:
	case VK_FORMAT_BC4_UNORM_BLOCK:
	case VK_FORMAT_BC5_UNORM_BLOCK:
	case VK_FORMAT_BC7_UNORM_BLOCK:
	case VK_FORMAT_ASTC_4x4_UNORM_BLOCK:
	case VK_FORMAT_ASTC_5x5_UNORM_BLOCK:
	case VK_FORMAT_ASTC_6x6_UNORM_BLOCK:
	case VK_FORMAT_ASTC_8x8_UNORM_BLOCK:
		color = Granite::ColorSpace::Linear;
		break;

	default:
		color = Granite::ColorSpace::sRGB;
		break;
	}

	auto input = make_shared<MemoryMappedTexture>(Granite::load_texture_from_file(input_path, color));

	if (input->get_layout().get_required_size() == 0)
	{
		LOGE("Failed to load texture %s.\n", input_path.c_str());
		return 1;
	}

	args.deferred_mipgen = deferred_generate_mipmap;

	if (generate_mipmap)
	{
		*input = generate_mipmaps(input->get_layout(), input->get_flags());
		if (input->get_layout().get_required_size() == 0)
		{
			LOGE("Failed to save texture: %s\n", args.output.c_str());
			return 1;
		}
	}

	if (fixup_alpha)
	{
		*input = fixup_alpha_edges(input->get_layout(), input->get_flags());
		if (input->get_layout().get_required_size() == 0)
		{
			LOGE("Failed to save texture: %s\n", args.output.c_str());
			return 1;
		}
	}

	if (input->get_layout().get_format() == VK_FORMAT_R16G16B16A16_SFLOAT)
		args.mode = TextureMode::HDR;

	ThreadGroup group;
	group.start(std::thread::hardware_concurrency());

	auto dummy = group.create_task();
	compress_texture(group, args, input, dummy, nullptr);
	dummy->flush();
	group.wait_idle();
}
