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
#endif

struct CompressorState : enable_shared_from_this<CompressorState>
{
	shared_ptr<gli::texture> input;
	shared_ptr<gli::texture> output;

#ifdef HAVE_ISPC
	bc6h_enc_settings bc6 = {};
	bc7_enc_settings bc7 = {};
	astc_enc_settings astc = {};
#endif

	bool use_astc_encoder = false;
	bool use_hdr = false;

	unsigned block_size_x = 1;
	unsigned block_size_y = 1;

	void setup(const CompressorArguments &args);
	void enqueue_compression(ThreadGroup &group, const CompressorArguments &args);
	void enqueue_compression_block_ispc(TaskGroup &group, const CompressorArguments &args, unsigned layer, unsigned face, unsigned level);
	void enqueue_compression_block_astc(TaskGroup &group, const CompressorArguments &args, unsigned layer, unsigned face, unsigned level, bool use_hdr);
};

void CompressorState::setup(const CompressorArguments &args)
{
	const auto handle_astc_ldr_format = [&](unsigned x, unsigned y) -> bool {
		block_size_x = x;
		block_size_y = y;
		if (input->format() != gli::FORMAT_RGBA8_SRGB_PACK8 && input->format() != gli::FORMAT_RGBA8_UNORM_PACK8)
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
		if (input->format() != gli::FORMAT_RGBA16_SFLOAT_PACK16)
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
		if (input->format() != gli::FORMAT_RGBA16_SFLOAT_PACK16)
		{
			LOGE("Input format to bc6h must be RGBA16_SFLOAT.\n");
			return;
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
			return;
		}
		break;

	case gli::FORMAT_RGBA_BP_UNORM_BLOCK16:
	case gli::FORMAT_RGBA_BP_SRGB_BLOCK16:
		block_size_x = 4;
		block_size_y = 4;
		if (input->format() != gli::FORMAT_RGBA8_SRGB_PACK8 && input->format() != gli::FORMAT_RGBA8_UNORM_PACK8)
		{
			LOGE("Input format to bc7 must be RGBA8.\n");
			return;
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
			return;
		}
		break;

	case gli::FORMAT_RGB_DXT1_SRGB_BLOCK8:
	case gli::FORMAT_RGB_DXT1_UNORM_BLOCK8:
	case gli::FORMAT_RGBA_DXT5_SRGB_BLOCK16:
	case gli::FORMAT_RGBA_DXT5_UNORM_BLOCK16:
		block_size_x = 4;
		block_size_y = 4;
		if (input->format() != gli::FORMAT_RGBA8_SRGB_PACK8 && input->format() != gli::FORMAT_RGBA8_UNORM_PACK8)
		{
			LOGE("Input format to bc1 or bc3 must be RGBA8.\n");
			return;
		}
		break;
#endif

	case gli::FORMAT_RGBA_ASTC_4X4_SRGB_BLOCK16:
	case gli::FORMAT_RGBA_ASTC_4X4_UNORM_BLOCK16:
		if (input->format() == gli::FORMAT_RGBA16_SFLOAT_PACK16)
		{
			if (!handle_astc_hdr_format(4, 4))
				return;
		}
		else if (!handle_astc_ldr_format(4, 4))
			return;
		break;

	case gli::FORMAT_RGBA_ASTC_5X5_SRGB_BLOCK16:
	case gli::FORMAT_RGBA_ASTC_5X5_UNORM_BLOCK16:
		if (input->format() == gli::FORMAT_RGBA16_SFLOAT_PACK16)
		{
			if (!handle_astc_hdr_format(5, 5))
				return;
		}
		else if (!handle_astc_ldr_format(5, 5))
			return;
		break;

	case gli::FORMAT_RGBA_ASTC_6X6_SRGB_BLOCK16:
	case gli::FORMAT_RGBA_ASTC_6X6_UNORM_BLOCK16:
		if (input->format() == gli::FORMAT_RGBA16_SFLOAT_PACK16)
		{
			if (!handle_astc_hdr_format(6, 6))
				return;
		}
		else if (!handle_astc_ldr_format(6, 6))
			return;
		break;

	case gli::FORMAT_RGBA_ASTC_8X8_SRGB_BLOCK16:
	case gli::FORMAT_RGBA_ASTC_8X8_UNORM_BLOCK16:
		if (input->format() == gli::FORMAT_RGBA16_SFLOAT_PACK16)
		{
			if (!handle_astc_hdr_format(8, 8))
				return;
		}
		else if (!handle_astc_ldr_format(8, 8))
			return;
		break;

	case gli::FORMAT_RGBA8_UNORM_PACK8:
	case gli::FORMAT_RGBA8_SRGB_PACK8:
		break;

	default:
		LOGE("Unknown format.\n");
		return;
	}
}

#ifdef HAVE_ISPC
void CompressorState::enqueue_compression_block_ispc(TaskGroup &group, const CompressorArguments &args,
                                                     unsigned layer, unsigned face, unsigned level)
{
	int width = input->extent(level).x;
	int height = input->extent(level).y;
	int grid_stride_x = (32 / block_size_x) * block_size_x;
	int grid_stride_y = (32 / block_size_y) * block_size_y;

	for (int y = 0; y < height; y += grid_stride_y)
	{
		for (int x = 0; x < width; x += grid_stride_x)
		{
			group->enqueue_task([=, format = args.format]() {
				uint8_t padded_buffer[32 * 32 * 8];
				uint8_t encode_buffer[16 * 8 * 8];
				rgba_surface surface = {};
				surface.ptr = const_cast<uint8_t *>(static_cast<const uint8_t *>(input->data(layer, face, level)));
				surface.width = std::min(width - x, grid_stride_x);
				surface.height = std::min(height - y, grid_stride_y);
				surface.stride = width * format_to_stride(format);
				surface.ptr += y * surface.stride + x * format_to_stride(format);

				rgba_surface padded_surface = {};

				int num_blocks_x = surface.width / block_size_x;
				int num_blocks_y = surface.height / block_size_y;
				int blocks_x = (width + block_size_x - 1) / block_size_x;

				const auto get_block_data = [&](int bx, int by, int block_size) -> uint8_t * {
					auto *dst = static_cast<uint8_t *>(output->data(layer, face, level));
					dst += ((x / block_size_x) + bx) * block_size;
					dst += ((y / block_size_y) + by) * blocks_x * block_size;
					return dst;
				};

				const auto write_encode_data = [&](int block_size) {
					for (int by = 0; by < num_blocks_y; by++)
					{
						for (int bx = 0; bx < num_blocks_x; bx++)
						{
							auto *dst = get_block_data(bx, by, block_size);
							memcpy(dst, &encode_buffer[(by * num_blocks_x + bx) * block_size], block_size);
						}
					}
				};

				if ((surface.width % block_size_x) || (surface.height % block_size_y))
				{
					padded_surface.width = ((surface.width + block_size_x - 1) / block_size_x) * block_size_x;
					padded_surface.height = ((surface.height + block_size_y - 1) / block_size_y) * block_size_y;
					padded_surface.stride = padded_surface.width * format_to_stride(format);
					padded_surface.ptr = padded_buffer;
					ReplicateBorders(&padded_surface, &surface, 0, 0, format_to_stride(format) * 8);
				}
				else
					padded_surface = surface;

				switch (format)
				{
				case gli::FORMAT_RGB_BP_UFLOAT_BLOCK16:
				{
					CompressBlocksBC6H(&padded_surface, encode_buffer, &bc6);
					write_encode_data(16);
					break;
				}

				case gli::FORMAT_RGBA_BP_SRGB_BLOCK16:
				case gli::FORMAT_RGBA_BP_UNORM_BLOCK16:
				{
					CompressBlocksBC7(&padded_surface, encode_buffer, &bc7);
					write_encode_data(16);
					break;
				}

				case gli::FORMAT_RGB_DXT1_SRGB_BLOCK8:
				case gli::FORMAT_RGB_DXT1_UNORM_BLOCK8:
				{
					CompressBlocksBC1(&padded_surface, encode_buffer);
					write_encode_data(8);
					break;
				}

				case gli::FORMAT_RGBA_DXT5_SRGB_BLOCK16:
				case gli::FORMAT_RGBA_DXT5_UNORM_BLOCK16:
				{
					CompressBlocksBC3(&padded_surface, encode_buffer);
					write_encode_data(16);
					break;
				}

				case gli::FORMAT_RGBA_ASTC_4X4_SRGB_BLOCK16:
				case gli::FORMAT_RGBA_ASTC_4X4_UNORM_BLOCK16:
				case gli::FORMAT_RGBA_ASTC_5X5_SRGB_BLOCK16:
				case gli::FORMAT_RGBA_ASTC_5X5_UNORM_BLOCK16:
				case gli::FORMAT_RGBA_ASTC_6X6_SRGB_BLOCK16:
				case gli::FORMAT_RGBA_ASTC_6X6_UNORM_BLOCK16:
				case gli::FORMAT_RGBA_ASTC_8X8_SRGB_BLOCK16:
				case gli::FORMAT_RGBA_ASTC_8X8_UNORM_BLOCK16:
				{
					CompressBlocksASTC(&padded_surface, encode_buffer, &astc);
					write_encode_data(16);
					break;
				}

				default:
					break;
				}
			});
		}
	}
}
#endif

#ifdef HAVE_ASTC_ENCODER
void CompressorState::enqueue_compression_block_astc(TaskGroup &compression_task, const CompressorArguments &args,
                                                     unsigned layer, unsigned face, unsigned level, bool use_hdr)
{
	static FirstASTC first_astc;

	struct CodecState
	{
		error_weighting_params ewp = {};
		vector<uint8_t *> rows;
		vector<uint8_t> buffer;
		astc_codec_image astc_image = {};

		int layer, face, level;
		int blocks_x, blocks_y;
		void **ptr;
	};

	auto state = make_shared<CodecState>();

	state->ewp.rgb_power = 1.0f;
	state->ewp.alpha_power = 1.0f;
	state->ewp.rgb_base_weight = 1.0f;
	state->ewp.alpha_base_weight = 1.0f;

	state->ewp.rgba_weights[0] = 1.0f;
	state->ewp.rgba_weights[1] = 1.0f;
	state->ewp.rgba_weights[2] = 1.0f;
	state->ewp.rgba_weights[3] = 1.0f;

	float log10_texels = log10(float(block_size_x * block_size_y));
	float dblimit = std::max(95.0f - 35.0f * log10_texels, 70.0f - 19.0f * log10_texels);

	if (args.quality >= 3)
	{
		state->ewp.max_refinement_iters = 2;
		state->ewp.block_mode_cutoff = 75.0f / 100.0f;
		state->ewp.partition_1_to_2_limit = 1.2f;
		state->ewp.lowest_correlation_cutoff = 0.75f;
		state->ewp.partition_search_limit = 25;
		dblimit = std::max(95.0f - 35.0f * log10_texels, 70.0f - 19.0f * log10_texels);
	}
	else if (args.quality == 2)
	{
		state->ewp.max_refinement_iters = 1;
		state->ewp.block_mode_cutoff = 50.0f / 100.0f;
		state->ewp.partition_1_to_2_limit = 1.0f;
		state->ewp.lowest_correlation_cutoff = 0.5f;
		state->ewp.partition_search_limit = 4;
		dblimit = std::max(85.0f - 35.0f * log10_texels, 63.0f - 19.0f * log10_texels);
	}
	else if (args.quality <= 1)
	{
		state->ewp.max_refinement_iters = 1;
		state->ewp.block_mode_cutoff = 25.0f / 100.0f;
		state->ewp.partition_1_to_2_limit = 1.0f;
		state->ewp.lowest_correlation_cutoff = 0.5f;
		state->ewp.partition_search_limit = 2;
		dblimit = std::max(70.0f - 35.0f * log10_texels, 53.0f - 19.0f * log10_texels);
	}

	if (use_hdr)
	{
		state->ewp.mean_stdev_radius = 0;
		state->ewp.rgb_power = 0.75f;
		state->ewp.rgb_base_weight = 0.0f;
		state->ewp.rgb_mean_weight = 1.0f;
		state->ewp.alpha_power = 0.75f;
		state->ewp.alpha_base_weight = 0.0f;
		state->ewp.alpha_mean_weight = 1.0f;
		state->ewp.partition_search_limit = PARTITION_COUNT;
		state->ewp.texel_avg_error_limit = 0.0f;
		rgb_force_use_of_hdr = 1;
		alpha_force_use_of_hdr = 1;
	}
	else
	{
		rgb_force_use_of_hdr = 0;
		alpha_force_use_of_hdr = 0;
		state->ewp.texel_avg_error_limit = pow(0.1f, dblimit * 0.1f) * 65535.0f * 65535.0f;
	}

	float max_color_component_weight = std::max(std::max(state->ewp.rgba_weights[0], state->ewp.rgba_weights[1]),
	                                            std::max(state->ewp.rgba_weights[2], state->ewp.rgba_weights[3]));
	for (auto &w : state->ewp.rgba_weights)
		w = std::max(w, max_color_component_weight / 1000.0f);

	expand_block_artifact_suppression(block_size_x, block_size_y, 1, &state->ewp);

	int width = input->extent(level).x;
	int height = input->extent(level).y;

	state->astc_image.xsize = width;
	state->astc_image.ysize = height;
	state->astc_image.zsize = 1;
	state->astc_image.padding = std::max(state->ewp.mean_stdev_radius, state->ewp.alpha_radius);

	int exsize = state->astc_image.xsize + state->astc_image.padding * 2;
	int eysize = state->astc_image.ysize + state->astc_image.padding * 2;
	int estride = use_hdr ? 8 : 4;
	state->rows.resize(eysize);

	state->buffer.resize(exsize * eysize * estride);
	state->rows[0] = state->buffer.data();
	for (int y = 1; y < eysize; y++)
		state->rows[y] = state->rows[0] + y * exsize * estride;

	for (int y = 0; y < eysize; y++)
	{
		for (int x = 0; x < exsize; x++)
		{
			int dst_offset = x + y * exsize;
			int src_offset = clamp(x - state->astc_image.padding, 0, state->astc_image.xsize - 1) +
			                 clamp(y - state->astc_image.padding, 0, state->astc_image.ysize - 1) * state->astc_image.xsize;

			auto *src = static_cast<const uint8_t *>(input->data(layer, face, level));
			memcpy(&state->buffer[dst_offset * estride], &src[src_offset * estride], estride);
		}
	}

	state->ptr = reinterpret_cast<void **>(state->rows.data());

	// Triple-pointer wat.
	if (use_hdr)
		state->astc_image.imagedata16 = reinterpret_cast<uint16_t ***>(&state->ptr);
	else
		state->astc_image.imagedata8 = reinterpret_cast<uint8_t ***>(&state->ptr);

	if (state->astc_image.padding > 0 || state->ewp.rgb_mean_weight != 0.0f || state->ewp.rgb_stdev_weight != 0.0f ||
	    state->ewp.alpha_mean_weight != 0.0f || state->ewp.alpha_stdev_weight != 0.0f)
	{
		const swizzlepattern swizzle = { 0, 1, 2, 3 };
		compute_averages_and_variances(&state->astc_image, state->ewp.rgb_power, state->ewp.alpha_power,
		                               state->ewp.mean_stdev_radius, state->ewp.alpha_radius, swizzle);
	}

	state->blocks_x = (width + block_size_x - 1) / block_size_x;
	state->blocks_y = (height + block_size_y - 1) / block_size_y;
	state->layer = layer;
	state->face = face;
	state->level = level;

	for (int y = 0; y < state->blocks_y; y++)
	{
		for (int x = 0; x < state->blocks_x; x++)
		{
			compression_task->enqueue_task([=]() {
				symbolic_compressed_block scb;
				physical_compressed_block pcb;
				imageblock pb = {};
				const swizzlepattern swizzle = { 0, 1, 2, 3 };

				fetch_imageblock(&state->astc_image, &pb, block_size_x, block_size_y, 1, x * block_size_x,
				                 y * block_size_y, 0, swizzle);
				compress_symbolic_block(&state->astc_image, use_hdr ? DECODE_HDR : DECODE_LDR,
				                        block_size_x, block_size_y, 1, &state->ewp, &pb, &scb);
				pcb = symbolic_to_physical(block_size_x, block_size_y, 1, &scb);

				auto *dst = static_cast<uint8_t *>(output->data(state->layer, state->face, state->level));
				memcpy(dst + 16 * (y * state->blocks_x + x), &pcb, sizeof(pcb));
			});
		}
	}
}
#endif

void CompressorState::enqueue_compression(ThreadGroup &group, const CompressorArguments &args)
{
	auto compression_task = group.create_task();

	for (unsigned layer = 0; layer < input->layers(); layer++)
	{
		for (unsigned face = 0; face < input->faces(); face++)
		{
			for (unsigned level = 0; level < input->levels(); level++)
			{
				switch (args.format)
				{
#ifdef HAVE_ISPC
				case gli::FORMAT_RGB_BP_UFLOAT_BLOCK16:
				case gli::FORMAT_RGBA_BP_SRGB_BLOCK16:
				case gli::FORMAT_RGBA_BP_UNORM_BLOCK16:
				case gli::FORMAT_RGB_DXT1_SRGB_BLOCK8:
				case gli::FORMAT_RGB_DXT1_UNORM_BLOCK8:
				case gli::FORMAT_RGBA_DXT5_SRGB_BLOCK16:
				case gli::FORMAT_RGBA_DXT5_UNORM_BLOCK16:
					enqueue_compression_block_ispc(compression_task, args, layer, face, level);
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
						enqueue_compression_block_ispc(compression_task, args, layer, face, level);
					else
#endif
					{
#ifdef HAVE_ASTC_ENCODER
						enqueue_compression_block_astc(compression_task, args, layer, face, level, use_hdr);
#endif
					}
					break;

				default:
					return;
				}
			}
		}
	}

	// Pass down ownership to final task.
	auto write_task = group.create_task([args, state = shared_from_this()]() {
		if (!save_texture_to_file(args.output, *state->output))
			LOGE("Failed to save texture to file.\n");
	});
	group.add_dependency(write_task, compression_task);
}

void compress_texture(ThreadGroup &group, const CompressorArguments &args, const shared_ptr<gli::texture> &input, TaskGroup &dep)
{
	auto output = make_shared<CompressorState>();
	output->input = input;

	auto setup_task = group.create_task([&group, output, args]() {
		output->output = make_shared<gli::texture>(output->input->target(), args.format,
		                                           output->input->extent(),
		                                           output->input->layers(), output->input->faces(), output->input->levels());
		output->setup(args);
		output->enqueue_compression(group, args);
	});
	group.add_dependency(setup_task, dep);
}
}