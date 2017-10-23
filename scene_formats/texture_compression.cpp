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

#include "texture_compression.hpp"
#include "texture_files.hpp"
#include "format.hpp"
#include <vector>

#ifdef HAVE_ISPC
#include <ispc_texcomp.h>
#endif

#ifdef HAVE_ASTC_ENCODER
#undef IGNORE
#include "astc_codec_internals.h"
#endif

using namespace std;

namespace Granite
{
gli::format string_to_format(const string &s)
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
	else if (s == "rgba8_unorm")
		return gli::FORMAT_RGBA8_UNORM_PACK8;
	else if (s == "rgba8_srgb")
		return gli::FORMAT_RGBA8_SRGB_PACK8;
	else if (s == "astc_4x4_srgb")
		return gli::FORMAT_RGBA_ASTC_4X4_SRGB_BLOCK16;
	else if (s == "astc_4x4_unorm")
		return gli::FORMAT_RGBA_ASTC_4X4_UNORM_BLOCK16;
	else if (s == "astc_4x4")
		return gli::FORMAT_RGBA_ASTC_4X4_UNORM_BLOCK16;
	else if (s == "astc_5x5_srgb")
		return gli::FORMAT_RGBA_ASTC_5X5_SRGB_BLOCK16;
	else if (s == "astc_5x5_unorm")
		return gli::FORMAT_RGBA_ASTC_5X5_UNORM_BLOCK16;
	else if (s == "astc_5x5")
		return gli::FORMAT_RGBA_ASTC_5X5_UNORM_BLOCK16;
	else if (s == "astc_6x6_srgb")
		return gli::FORMAT_RGBA_ASTC_6X6_SRGB_BLOCK16;
	else if (s == "astc_6x6_unorm")
		return gli::FORMAT_RGBA_ASTC_6X6_UNORM_BLOCK16;
	else if (s == "astc_6x6")
		return gli::FORMAT_RGBA_ASTC_6X6_UNORM_BLOCK16;
	else if (s == "astc_8x8_srgb")
		return gli::FORMAT_RGBA_ASTC_8X8_SRGB_BLOCK16;
	else if (s == "astc_8x8_unorm")
		return gli::FORMAT_RGBA_ASTC_8X8_UNORM_BLOCK16;
	else if (s == "astc_8x8")
		return gli::FORMAT_RGBA_ASTC_8X8_UNORM_BLOCK16;
	else
	{
		LOGE("Unknown format: %s.\n", s.c_str());
		return gli::FORMAT_UNDEFINED;
	}
}

#ifdef HAVE_ISPC
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
	case gli::FORMAT_RGBA_ASTC_4X4_SRGB_BLOCK16:
	case gli::FORMAT_RGBA_ASTC_4X4_UNORM_BLOCK16:
	case gli::FORMAT_RGBA_ASTC_5X5_SRGB_BLOCK16:
	case gli::FORMAT_RGBA_ASTC_5X5_UNORM_BLOCK16:
	case gli::FORMAT_RGBA_ASTC_6X6_SRGB_BLOCK16:
	case gli::FORMAT_RGBA_ASTC_6X6_UNORM_BLOCK16:
	case gli::FORMAT_RGBA_ASTC_8X8_SRGB_BLOCK16:
	case gli::FORMAT_RGBA_ASTC_8X8_UNORM_BLOCK16:
		return 4;

	case gli::FORMAT_RGB_BP_UFLOAT_BLOCK16:
		return 8;

	default:
		return 0;
	}
}
#endif

#ifdef HAVE_ASTC_ENCODER
struct FirstASTC
{
	FirstASTC()
	{
		prepare_angular_tables();
		build_quantization_mode_table();
	}
};

static void compress_image_astc(uint8_t *output, const uint8_t *input, unsigned width, unsigned height,
                                unsigned block_size_x, unsigned block_size_y, unsigned quality, bool hdr)
{
	static FirstASTC first_astc;

	error_weighting_params ewp = {};
	ewp.rgb_power = 1.0f;
	ewp.alpha_power = 1.0f;
	ewp.rgb_base_weight = 1.0f;
	ewp.alpha_base_weight = 1.0f;

	ewp.rgba_weights[0] = 1.0f;
	ewp.rgba_weights[1] = 1.0f;
	ewp.rgba_weights[2] = 1.0f;
	ewp.rgba_weights[3] = 1.0f;

	float log10_texels = log10(float(block_size_x * block_size_y));
	float dblimit = std::max(95.0f - 35.0f * log10_texels, 70.0f - 19.0f * log10_texels);
	swizzlepattern swizzle = { 0, 1, 2, 3 };

	if (quality >= 3)
	{
		ewp.max_refinement_iters = 2;
		ewp.block_mode_cutoff = 75.0f / 100.0f;
		ewp.partition_1_to_2_limit = 1.2f;
		ewp.lowest_correlation_cutoff = 0.75f;
		ewp.partition_search_limit = 25;
		dblimit = std::max(95.0f - 35.0f * log10_texels, 70.0f - 19.0f * log10_texels);
	}
	else if (quality == 2)
	{
		ewp.max_refinement_iters = 1;
		ewp.block_mode_cutoff = 50.0f / 100.0f;
		ewp.partition_1_to_2_limit = 1.0f;
		ewp.lowest_correlation_cutoff = 0.5f;
		ewp.partition_search_limit = 4;
		dblimit = std::max(85.0f - 35.0f * log10_texels, 63.0f - 19.0f * log10_texels);
	}
	else if (quality <= 1)
	{
		ewp.max_refinement_iters = 1;
		ewp.block_mode_cutoff = 25.0f / 100.0f;
		ewp.partition_1_to_2_limit = 1.0f;
		ewp.lowest_correlation_cutoff = 0.5f;
		ewp.partition_search_limit = 2;
		dblimit = std::max(70.0f - 35.0f * log10_texels, 53.0f - 19.0f * log10_texels);
	}

	if (hdr)
	{
		ewp.mean_stdev_radius = 0.0f;
		ewp.rgb_power = 0.75f;
		ewp.rgb_base_weight = 0.0f;
		ewp.rgb_mean_weight = 1.0f;
		ewp.alpha_power = 0.75f;
		ewp.alpha_base_weight = 0.0f;
		ewp.alpha_mean_weight = 1.0f;
		ewp.partition_search_limit = PARTITION_COUNT;
		ewp.texel_avg_error_limit = 0.0f;
		rgb_force_use_of_hdr = 1;
		alpha_force_use_of_hdr = 1;
	}
	else
	{
		rgb_force_use_of_hdr = 0;
		alpha_force_use_of_hdr = 0;
		ewp.texel_avg_error_limit = pow(0.1f, dblimit * 0.1f) * 65535.0f * 65535.0f;
	}

	float max_color_component_weight = std::max(std::max(ewp.rgba_weights[0], ewp.rgba_weights[1]),
	                                            std::max(ewp.rgba_weights[2], ewp.rgba_weights[3]));
	for (unsigned i = 0; i < 4; i++)
		ewp.rgba_weights[i] = std::max(ewp.rgba_weights[i], max_color_component_weight / 1000.0f);
	expand_block_artifact_suppression(block_size_x, block_size_y, 1, &ewp);

	imageblock pb = {};
	astc_codec_image astc_image = {};
	astc_image.xsize = width;
	astc_image.ysize = height;
	astc_image.zsize = 1;
	astc_image.padding = std::max(ewp.mean_stdev_radius, ewp.alpha_radius);

	int exsize = astc_image.xsize + astc_image.padding * 2;
	int eysize = astc_image.ysize + astc_image.padding * 2;
	int estride = hdr ? 8 : 4;
	vector<uint8_t *> rows(eysize);

	vector<uint8_t> buffer(exsize * eysize * estride);
	rows[0] = buffer.data();
	for (int y = 1; y < eysize; y++)
		rows[y] = rows[0] + y * exsize * estride;

	for (int y = 0; y < eysize; y++)
	{
		for (int x = 0; x < exsize; x++)
		{
			int dst_offset = x + y * exsize;
			int src_offset = clamp(x - astc_image.padding, 0, astc_image.xsize - 1) +
			                 clamp(y - astc_image.padding, 0, astc_image.ysize - 1) * astc_image.xsize;
			memcpy(&buffer[dst_offset * estride], &input[src_offset * estride], estride);
		}
	}

	auto *ptr16 = reinterpret_cast<uint16_t **>(rows.data());
	auto *ptr8 = rows.data();
	// Triple-pointer wat.
	if (hdr)
		astc_image.imagedata16 = &ptr16;
	else
		astc_image.imagedata8 = &ptr8;

	if (astc_image.padding > 0 || ewp.rgb_mean_weight != 0.0f || ewp.rgb_stdev_weight != 0.0f ||
	    ewp.alpha_mean_weight != 0.0f || ewp.alpha_stdev_weight != 0.0f)
	{
		compute_averages_and_variances(&astc_image, ewp.rgb_power, ewp.alpha_power,
		                               ewp.mean_stdev_radius, ewp.alpha_radius, swizzle);
	}

	int blocks_x = (width + block_size_x - 1) / block_size_x;
	int blocks_y = (height + block_size_y - 1) / block_size_y;

	symbolic_compressed_block scb;
	physical_compressed_block pcb;

	for (int y = 0; y < blocks_y; y++)
	{
		for (int x = 0; x < blocks_x; x++)
		{
			fetch_imageblock(&astc_image, &pb, block_size_x, block_size_y, 1, x * block_size_x, y * block_size_y, 0, swizzle);
			compress_symbolic_block(&astc_image, hdr ? DECODE_HDR : DECODE_LDR,
			                        block_size_x, block_size_y, 1, &ewp, &pb, &scb);
			pcb = symbolic_to_physical(block_size_x, block_size_y, 1, &scb);
			memcpy(output + 16 * (y * blocks_x + x), &pcb, sizeof(pcb));
		}
	}
}
#endif

bool compress_texture(const CompressorArguments &args, const gli::texture &input)
{
	gli::texture output(input.target(), args.format, input.extent(), input.layers(), input.faces(), input.levels());

#ifdef HAVE_ISPC
	bc6h_enc_settings bc6;
	bc7_enc_settings bc7;
	astc_enc_settings astc;
#endif

	bool use_astc_encoder = false;
	bool use_hdr = false;

	unsigned block_size_x = 1;
	unsigned block_size_y = 1;

	const auto handle_astc_ldr_format = [&](unsigned x, unsigned y) -> bool {
		block_size_x = x;
		block_size_y = y;
		if (input.format() != gli::FORMAT_RGBA8_SRGB_PACK8 && input.format() != gli::FORMAT_RGBA8_UNORM_PACK8)
		{
			LOGE("Input format to ASTC LDR must be RGBA8.\n");
			return false;
		}

#ifdef HAVE_ISPC
		if (args.alpha)
		{
			if (args.quality <= 3)
				GetProfile_astc_alpha_fast(&astc, x, y);
			else
				GetProfile_astc_alpha_slow(&astc, x, y);
		}
		else
			GetProfile_astc_fast(&astc, x, y);
#elif defined(HAVE_ASTC_ENCODER)
		use_astc_encoder = true;
#endif

		return true;
	};

	const auto handle_astc_hdr_format = [&](unsigned x, unsigned y) -> bool {
#ifdef HAVE_ASTC_ENCODER
		if (input.format() != gli::FORMAT_RGBA16_SFLOAT_PACK16)
		{
			LOGE("Input format to ASTC HDR must be RGBA16F.\n");
			return false;
		}
		use_astc_encoder = true;
		use_hdr = true;
		block_size_x = x;
		block_size_y = y;
		return true;
#else
		(void)x;
		(void)y;
		return false;
#endif
	};

	switch (args.format)
	{
#ifdef HAVE_ISPC
	case gli::FORMAT_RGB_BP_UFLOAT_BLOCK16:
		block_size_x = 4;
		block_size_y = 4;
		if (input.format() != gli::FORMAT_RGBA16_SFLOAT_PACK16)
		{
			LOGE("Input format to bc6h must be RGBA16_SFLOAT.\n");
			return false;
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
			return false;
		}
		break;

	case gli::FORMAT_RGBA_BP_UNORM_BLOCK16:
	case gli::FORMAT_RGBA_BP_SRGB_BLOCK16:
		block_size_x = 4;
		block_size_y = 4;
		if (input.format() != gli::FORMAT_RGBA8_SRGB_PACK8 && input.format() != gli::FORMAT_RGBA8_UNORM_PACK8)
		{
			LOGE("Input format to bc7 must be RGBA8.\n");
			return false;
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
			return false;
		}
		break;

	case gli::FORMAT_RGB_DXT1_SRGB_BLOCK8:
	case gli::FORMAT_RGB_DXT1_UNORM_BLOCK8:
	case gli::FORMAT_RGBA_DXT5_SRGB_BLOCK16:
	case gli::FORMAT_RGBA_DXT5_UNORM_BLOCK16:
		block_size_x = 4;
		block_size_y = 4;
		if (input.format() != gli::FORMAT_RGBA8_SRGB_PACK8 && input.format() != gli::FORMAT_RGBA8_UNORM_PACK8)
		{
			LOGE("Input format to bc1 or bc3 must be RGBA8.\n");
			return false;
		}
		break;
#endif

	case gli::FORMAT_RGBA_ASTC_4X4_SRGB_BLOCK16:
	case gli::FORMAT_RGBA_ASTC_4X4_UNORM_BLOCK16:
		if (input.format() == gli::FORMAT_RGBA16_SFLOAT_PACK16)
		{
			if (!handle_astc_hdr_format(4, 4))
				return false;
		}
		else if (!handle_astc_ldr_format(4, 4))
			return false;
		break;

	case gli::FORMAT_RGBA_ASTC_5X5_SRGB_BLOCK16:
	case gli::FORMAT_RGBA_ASTC_5X5_UNORM_BLOCK16:
		if (input.format() == gli::FORMAT_RGBA16_SFLOAT_PACK16)
		{
			if (!handle_astc_hdr_format(5, 5))
				return false;
		}
		else if (!handle_astc_ldr_format(5, 5))
			return false;
		break;

	case gli::FORMAT_RGBA_ASTC_6X6_SRGB_BLOCK16:
	case gli::FORMAT_RGBA_ASTC_6X6_UNORM_BLOCK16:
		if (input.format() == gli::FORMAT_RGBA16_SFLOAT_PACK16)
		{
			if (!handle_astc_hdr_format(6, 6))
				return false;
		}
		else if (!handle_astc_ldr_format(6, 6))
			return false;
		break;

	case gli::FORMAT_RGBA_ASTC_8X8_SRGB_BLOCK16:
	case gli::FORMAT_RGBA_ASTC_8X8_UNORM_BLOCK16:
		if (input.format() == gli::FORMAT_RGBA16_SFLOAT_PACK16)
		{
			if (!handle_astc_hdr_format(8, 8))
				return false;
		}
		else if (!handle_astc_ldr_format(8, 8))
			return false;
		break;

	case gli::FORMAT_RGBA8_UNORM_PACK8:
	case gli::FORMAT_RGBA8_SRGB_PACK8:
		break;

	default:
		LOGE("Unknown format.\n");
		return false;
	}

	vector<uint8_t> padded_buffer;
	for (unsigned layer = 0; layer < input.layers(); layer++)
	{
		for (unsigned face = 0; face < input.faces(); face++)
		{
			for (unsigned level = 0; level < input.levels(); level++)
			{
#ifdef HAVE_ISPC
				rgba_surface surface = {};
				surface.ptr = const_cast<uint8_t *>(static_cast<const uint8_t *>(input.data(layer, face, level)));
				surface.width = input.extent(level).x;
				surface.height = input.extent(level).y;
				surface.stride = surface.width * format_to_stride(args.format);

				rgba_surface padded_surface = {};

				if ((surface.width % block_size_x) || (surface.height % block_size_y))
				{
					padded_surface.width = ((surface.width + block_size_x - 1) / block_size_x) * block_size_x;
					padded_surface.height = ((surface.height + block_size_y - 1) / block_size_y) * block_size_y;
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
#endif

				switch (args.format)
				{
#ifdef HAVE_ISPC
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
#endif

				case gli::FORMAT_RGBA_ASTC_4X4_SRGB_BLOCK16:
				case gli::FORMAT_RGBA_ASTC_4X4_UNORM_BLOCK16:
				case gli::FORMAT_RGBA_ASTC_5X5_SRGB_BLOCK16:
				case gli::FORMAT_RGBA_ASTC_5X5_UNORM_BLOCK16:
				case gli::FORMAT_RGBA_ASTC_6X6_SRGB_BLOCK16:
				case gli::FORMAT_RGBA_ASTC_6X6_UNORM_BLOCK16:
				case gli::FORMAT_RGBA_ASTC_8X8_SRGB_BLOCK16:
				case gli::FORMAT_RGBA_ASTC_8X8_UNORM_BLOCK16:
#ifdef HAVE_ISPC
					if (!use_astc_encoder)
						CompressBlocksASTC(&padded_surface, static_cast<uint8_t *>(output.data(layer, face, level)), &astc);
					else
#endif
					{
#ifdef HAVE_ASTC_ENCODER
						compress_image_astc(static_cast<uint8_t *>(output.data(layer, face, level)),
						                    static_cast<const uint8_t *>(input.data(layer, face, level)),
						                    input.extent(level).x,
						                    input.extent(level).y,
						                    block_size_x, block_size_y, args.quality, use_hdr);
#else
						return false;
#endif
					}
					break;

				default:
					break;
				}
			}
		}
	}

	if (!save_texture_to_file(args.output, output))
	{
		LOGE("Failed to save texture: %s\n", args.output.c_str());
		return false;
	}

	return true;
}
}