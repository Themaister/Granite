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

#include <ispc_texcomp/ispc_texcomp.h>
#include "cli_parser.hpp"
#include "gli/save.hpp"
#include "gli/load.hpp"
#include "util.hpp"

using namespace std;
using namespace Granite;

static void print_help()
{
	LOGI("Usage: --quality [1-5] --format <format> --output <out.ktx> <in.ktx>\n");
}

static gli::format string_to_format(const string &s)
{
	if (s == "bc6h")
		return gli::FORMAT_RGB_BP_UFLOAT_BLOCK16;
	else if (s == "bc7_unorm")
		return gli::FORMAT_RGBA_BP_UNORM_BLOCK16;
	else if (s == "bc7_srgb")
		return gli::FORMAT_RGBA_BP_SRGB_BLOCK16;
	else if (s == "bc1_unorm")
		return gli::FORMAT_RGB_DXT1_UNORM_BLOCK8;
	else if (s == "bc1_srgb")
		return gli::FORMAT_RGB_DXT1_SRGB_BLOCK8;
	else if (s == "bc3_unorm")
		return gli::FORMAT_RGBA_DXT5_UNORM_BLOCK16;
	else if (s == "bc3_srgb")
		return gli::FORMAT_RGBA_DXT5_SRGB_BLOCK16;
	else
	{
		LOGE("Unknown format: %s.\n", s.c_str());
		return gli::FORMAT_UNDEFINED;
	}
}

static unsigned format_to_stride(gli::format format)
{
	switch (format)
	{
	case gli::FORMAT_RGBA_BP_SRGB_BLOCK16:
	case gli::FORMAT_RGBA_BP_UNORM_BLOCK16:
	case gli::FORMAT_RGB_DXT1_SRGB_BLOCK8:
	case gli::FORMAT_RGB_DXT1_UNORM_BLOCK8:
	case gli::FORMAT_RGBA_DXT5_SRGB_BLOCK16:
	case gli::FORMAT_RGBA_DXT5_UNORM_BLOCK16:
		return 4;

	case gli::FORMAT_RGB_BP_UFLOAT_BLOCK16:
		return 8;

	default:
		return 0;
	}
}

int main(int argc, char *argv[])
{
	struct Arguments
	{
		string output;
		string input;
		gli::format format = gli::FORMAT_UNDEFINED;
		unsigned quality = 3;
		bool alpha = false;
	} args;
	CLICallbacks cbs;
	cbs.add("--help", [&](CLIParser &parser) { print_help(); parser.end(); });
	cbs.add("--quality", [&](CLIParser &parser) { args.quality = parser.next_uint(); });
	cbs.add("--format", [&](CLIParser &parser) { args.format = string_to_format(parser.next_string()); });
	cbs.add("--output", [&](CLIParser &parser) { args.output = parser.next_string(); });
	cbs.add("--alpha", [&](CLIParser &) { args.alpha = true; });
	cbs.default_handler = [&](const char *arg) { args.input = arg; };
	CLIParser parser(move(cbs), argc - 1, argv + 1);

	if (!parser.parse())
		return 1;
	else if (parser.is_ended_state())
		return 0;

	if (args.format == gli::FORMAT_UNDEFINED)
		return 1;
	if (args.output.empty() || args.input.empty())
		return 1;

	auto input = gli::load(args.input);
	if (input.empty())
	{
		LOGE("Failed to load texture %s.\n", args.input.c_str());
		return 1;
	}

	gli::texture output(input.target(), args.format, input.extent(), input.layers(), input.faces(), input.levels());
	bc6h_enc_settings bc6;
	bc7_enc_settings bc7;

	switch (args.format)
	{
	case gli::FORMAT_RGB_BP_UFLOAT_BLOCK16:
		if (input.format() != gli::FORMAT_RGBA16_SFLOAT_PACK16)
		{
			LOGE("Input format to bc6h must be RGBA16_SFLOAT.\n");
			return 1;
		}

		switch (args.quality)
		{
		case 1:
			GetProfile_bc6h_veryfast(&bc6);
			break;

		case 2:
			GetProfile_bc6h_fast(&bc6);
			break;

		case 3:
			GetProfile_bc6h_basic(&bc6);
			break;

		case 4:
			GetProfile_bc6h_slow(&bc6);
			break;

		case 5:
			GetProfile_bc6h_veryslow(&bc6);
			break;

		default:
			LOGE("Unknown quality.\n");
			return 1;
		}
		break;

	case gli::FORMAT_RGBA_BP_UNORM_BLOCK16:
	case gli::FORMAT_RGBA_BP_SRGB_BLOCK16:
		if (input.format() != gli::FORMAT_RGBA8_SRGB_PACK8 && input.format() != gli::FORMAT_RGBA8_UNORM_PACK8)
		{
			LOGE("Input format to bc7 must be RGBA8.\n");
			return 1;
		}

		switch (args.quality)
		{
		case 1:
			if (args.alpha)
				GetProfile_alpha_ultrafast(&bc7);
			else
				GetProfile_ultrafast(&bc7);
			break;

		case 2:
			if (args.alpha)
				GetProfile_alpha_veryfast(&bc7);
			else
				GetProfile_veryfast(&bc7);
			break;

		case 3:
			if (args.alpha)
				GetProfile_alpha_fast(&bc7);
			else
				GetProfile_fast(&bc7);
			break;

		case 4:
			if (args.alpha)
				GetProfile_alpha_basic(&bc7);
			else
				GetProfile_basic(&bc7);
			break;

		case 5:
			if (args.alpha)
				GetProfile_alpha_slow(&bc7);
			else
				GetProfile_slow(&bc7);
			break;

		default:
			LOGE("Unknown quality.\n");
			return 1;
		}
		break;

	case gli::FORMAT_RGB_DXT1_SRGB_BLOCK8:
	case gli::FORMAT_RGB_DXT1_UNORM_BLOCK8:
	case gli::FORMAT_RGBA_DXT5_SRGB_BLOCK16:
	case gli::FORMAT_RGBA_DXT5_UNORM_BLOCK16:
		if (input.format() != gli::FORMAT_RGBA8_SRGB_PACK8 && input.format() != gli::FORMAT_RGBA8_UNORM_PACK8)
		{
			LOGE("Input format to bc1 or bc3 must be RGBA8.\n");
			return 1;
		}
		break;

	default:
		LOGE("Unknown format.\n");
		return 1;
	}

	vector<uint8_t> padded_buffer;

	for (unsigned layer = 0; layer < input.layers(); layer++)
	{
		for (unsigned face = 0; face < input.faces(); face++)
		{
			for (unsigned level = 0; level < input.levels(); level++)
			{
				rgba_surface surface = {};
				surface.ptr = static_cast<uint8_t *>(input.data(layer, face, level));
				surface.width = input.extent(level).x;
				surface.height = input.extent(level).y;
				surface.stride = surface.width * format_to_stride(args.format);

				rgba_surface padded_surface = {};

				if ((surface.width & 3) || (surface.height & 3))
				{
					padded_surface.width = (surface.width + 3) & ~3;
					padded_surface.height = (surface.height + 3) & ~3;
					padded_surface.stride = padded_surface.width * format_to_stride(args.format);
					padded_buffer.resize(padded_surface.stride * padded_surface.height);
					padded_surface.ptr = padded_buffer.data();
					ReplicateBorders(&padded_surface, &surface, 0, 0, format_to_stride(args.format) * 8);
				}
				else
				{
					padded_surface = surface;
					padded_surface.stride = padded_surface.width * format_to_stride(args.format);
				}

				switch (args.format)
				{
				case gli::FORMAT_RGB_BP_UFLOAT_BLOCK16:
					CompressBlocksBC6H(&padded_surface, static_cast<uint8_t *>(output.data(layer, face, level)), &bc6);
					break;

				case gli::FORMAT_RGBA_BP_SRGB_BLOCK16:
				case gli::FORMAT_RGBA_BP_UNORM_BLOCK16:
					CompressBlocksBC7(&padded_surface, static_cast<uint8_t *>(output.data(layer, face, level)), &bc7);
					break;

				case gli::FORMAT_RGB_DXT1_SRGB_BLOCK8:
				case gli::FORMAT_RGB_DXT1_UNORM_BLOCK8:
					CompressBlocksBC1(&padded_surface, static_cast<uint8_t *>(output.data(layer, face, level)));
					break;

				case gli::FORMAT_RGBA_DXT5_SRGB_BLOCK16:
				case gli::FORMAT_RGBA_DXT5_UNORM_BLOCK16:
					CompressBlocksBC3(&padded_surface, static_cast<uint8_t *>(output.data(layer, face, level)));
					break;

				default:
					break;
				}
			}
		}
	}

	if (!gli::save(output, args.output))
	{
		LOGE("Failed to save texture: %s\n", args.output.c_str());
		return 1;
	}

	return 0;
}