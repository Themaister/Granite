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

#include <texture_files.hpp>
#include "cli_parser.hpp"
#include "logging.hpp"
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
	     "\t[--alpha]\n"
	     "\t[--deferred-mipgen]\n"
	     "\t[--quality [1-5]]\n"
	     "\t[--format <format>]\n"
	     "\t[--swizzle <rgba01>x4]\n"
	     "\t[--normal-la]\n"
	     "\t[--mask-la]\n"
	     "\t--output <out.gtx>\n"
	     "\t<in.gtx>\n");
}

static VkComponentSwizzle parse_swizzle(const char c)
{
	switch (c)
	{
	case 'r':
	case 'R':
		return VK_COMPONENT_SWIZZLE_R;

	case 'g':
	case 'G':
		return VK_COMPONENT_SWIZZLE_G;

	case 'b':
	case 'B':
		return VK_COMPONENT_SWIZZLE_B;

	case 'a':
	case 'A':
		return VK_COMPONENT_SWIZZLE_A;

	case '1':
		return VK_COMPONENT_SWIZZLE_ONE;
	case '0':
		return VK_COMPONENT_SWIZZLE_ZERO;

	default:
		break;
	}

	LOGE("Invalid swizzle %c.\n", c);
	exit(EXIT_FAILURE);
}

static VkComponentMapping parse_swizzle(const char *str)
{
	if (strlen(str) != 4)
	{
		LOGE("Swizzle string must be 4 characters.\n");
		exit(EXIT_FAILURE);
	}

	return {
		parse_swizzle(str[0]),
		parse_swizzle(str[1]),
		parse_swizzle(str[2]),
		parse_swizzle(str[3])
	};
}

int main(int argc, char *argv[])
{
	Global::init(Global::MANAGER_FEATURE_THREAD_GROUP_BIT |
	             Global::MANAGER_FEATURE_FILESYSTEM_BIT |
	             Global::MANAGER_FEATURE_EVENT_BIT);

	string input_path;
	bool generate_mipmap = false;
	bool deferred_generate_mipmap = false;
	bool fixup_alpha = false;
	CompressorArguments args;

	VkComponentMapping swizzle = {
		VK_COMPONENT_SWIZZLE_R,
		VK_COMPONENT_SWIZZLE_G,
		VK_COMPONENT_SWIZZLE_B,
		VK_COMPONENT_SWIZZLE_A,
	};

	args.mode = TextureMode::RGB;

	CLICallbacks cbs;
	cbs.add("--help", [&](CLIParser &parser) { print_help(); parser.end(); });
	cbs.add("--quality", [&](CLIParser &parser) { args.quality = parser.next_uint(); });
	cbs.add("--format", [&](CLIParser &parser) { args.format = string_to_format(parser.next_string()); });
	cbs.add("--output", [&](CLIParser &parser) { args.output = parser.next_string(); });
	cbs.add("--alpha", [&](CLIParser &) { args.mode = TextureMode::RGBA; });
	cbs.add("--normal-la", [&](CLIParser &) { args.mode = TextureMode::NormalLA; });
	cbs.add("--mask-la", [&](CLIParser &) { args.mode = TextureMode::MaskLA; });
	cbs.add("--fixup-alpha", [&](CLIParser &) { fixup_alpha = true; });
	cbs.add("--mipgen", [&](CLIParser &) { generate_mipmap = true; });
	cbs.add("--deferred-mipgen", [&](CLIParser &) { deferred_generate_mipmap = true; });
	cbs.add("--swizzle", [&](CLIParser &parser) { swizzle = parse_swizzle(parser.next_string()); });
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

	Granite::ColorSpace color = Vulkan::format_is_srgb(args.format) ?
	                            Granite::ColorSpace::sRGB : Granite::ColorSpace::Linear;

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

	if (!swizzle_image(*input, swizzle))
	{
		LOGE("Failed to swizzle image.\n");
		return 1;
	}

	ThreadGroup &group = *Global::thread_group();

	auto dummy = group.create_task();
	compress_texture(group, args, input, dummy, nullptr);
	dummy->flush();
	group.wait_idle();
}
