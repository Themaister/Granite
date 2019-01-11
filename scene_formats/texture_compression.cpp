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

#include "rgtc_compressor.hpp"
#define RGTC_DEBUG

using namespace std;

namespace Granite
{
using namespace SceneFormats;

VkFormat string_to_format(const string &s)
{
	if (s == "bc6h")
		return VK_FORMAT_BC6H_UFLOAT_BLOCK;
	else if (s == "bc7_unorm")
		return VK_FORMAT_BC7_UNORM_BLOCK;
	else if (s == "bc7_srgb")
		return VK_FORMAT_BC7_SRGB_BLOCK;
	else if (s == "bc1_unorm")
		return VK_FORMAT_BC1_RGB_UNORM_BLOCK;
	else if (s == "bc1_srgb")
		return VK_FORMAT_BC1_RGB_SRGB_BLOCK;
	else if (s == "bc3_unorm")
		return VK_FORMAT_BC3_UNORM_BLOCK;
	else if (s == "bc3_srgb")
		return VK_FORMAT_BC3_SRGB_BLOCK;
	else if (s == "rgba8_unorm")
		return VK_FORMAT_R8G8B8A8_UNORM;
	else if (s == "rgba8_srgb")
		return VK_FORMAT_R8G8B8A8_SRGB;
	else if (s == "astc_4x4_srgb")
		return VK_FORMAT_ASTC_4x4_SRGB_BLOCK;
	else if (s == "astc_4x4_unorm")
		return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
	else if (s == "astc_4x4")
		return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
	else if (s == "astc_5x5_srgb")
		return VK_FORMAT_ASTC_5x5_SRGB_BLOCK;
	else if (s == "astc_5x5_unorm")
		return VK_FORMAT_ASTC_5x5_UNORM_BLOCK;
	else if (s == "astc_5x5")
		return VK_FORMAT_ASTC_5x5_UNORM_BLOCK;
	else if (s == "astc_6x6_srgb")
		return VK_FORMAT_ASTC_6x6_SRGB_BLOCK;
	else if (s == "astc_6x6_unorm")
		return VK_FORMAT_ASTC_6x6_UNORM_BLOCK;
	else if (s == "astc_6x6")
		return VK_FORMAT_ASTC_6x6_UNORM_BLOCK;
	else if (s == "astc_8x8_srgb")
		return VK_FORMAT_ASTC_8x8_SRGB_BLOCK;
	else if (s == "astc_8x8_unorm")
		return VK_FORMAT_ASTC_8x8_UNORM_BLOCK;
	else if (s == "astc_8x8")
		return VK_FORMAT_ASTC_8x8_UNORM_BLOCK;
	else
	{
		LOGE("Unknown format: %s.\n", s.c_str());
		return VK_FORMAT_UNDEFINED;
	}
}

#ifdef HAVE_ISPC
static unsigned format_to_stride(VkFormat format)
{
	switch (format)
	{
	case VK_FORMAT_BC7_UNORM_BLOCK:
	case VK_FORMAT_BC7_SRGB_BLOCK:
	case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
	case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
	case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
	case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
	case VK_FORMAT_BC3_UNORM_BLOCK:
	case VK_FORMAT_BC3_SRGB_BLOCK:
	case VK_FORMAT_ASTC_4x4_UNORM_BLOCK:
	case VK_FORMAT_ASTC_4x4_SRGB_BLOCK:
	case VK_FORMAT_ASTC_5x5_UNORM_BLOCK:
	case VK_FORMAT_ASTC_5x5_SRGB_BLOCK:
	case VK_FORMAT_ASTC_6x6_UNORM_BLOCK:
	case VK_FORMAT_ASTC_6x6_SRGB_BLOCK:
	case VK_FORMAT_ASTC_8x8_UNORM_BLOCK:
	case VK_FORMAT_ASTC_8x8_SRGB_BLOCK:
		return 4;

	case VK_FORMAT_BC6H_UFLOAT_BLOCK:
	case VK_FORMAT_BC6H_SFLOAT_BLOCK:
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
	shared_ptr<MemoryMappedTexture> input;
	shared_ptr<MemoryMappedTexture> output;

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
	void enqueue_compression_block_ispc(TaskGroup &group, const CompressorArguments &args, unsigned layer, unsigned level);
	void enqueue_compression_block_astc(TaskGroup &group, const CompressorArguments &args, unsigned layer, unsigned level, TextureMode mode);
	void enqueue_compression_block_rgtc(TaskGroup &group, const CompressorArguments &args, unsigned layer, unsigned level);

	double total_error[4] = {};
	mutex lock;
	TaskSignal *signal = nullptr;
};

void CompressorState::setup(const CompressorArguments &args)
{
#ifdef HAVE_ISPC
	bool alpha = args.mode == TextureMode::sRGBA || args.mode == TextureMode::RGBA;
#endif
	auto &layout = input->get_layout();

	const auto is_8bit = [&]() -> bool {
		return layout.get_format() == VK_FORMAT_R8G8B8A8_SRGB ||
		       layout.get_format() == VK_FORMAT_R8G8B8A8_UNORM;
	};

	const auto is_unorm = [&]() -> bool {
		return layout.get_format() == VK_FORMAT_R8G8B8A8_UNORM;
	};

	const auto is_16bit = [&]() -> bool {
		return layout.get_format() == VK_FORMAT_R16G16B16A16_SFLOAT;
	};

	const auto handle_astc_ldr_format = [&](unsigned x, unsigned y) -> bool {
		block_size_x = x;
		block_size_y = y;
		if (!is_8bit())
		{
			LOGE("Input format to ASTC LDR must be RGBA8.\n");
			return false;
		}

#ifdef HAVE_ISPC
		if (alpha)
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
		if (layout.get_format() != VK_FORMAT_R16G16B16A16_SFLOAT)
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
	case VK_FORMAT_BC4_UNORM_BLOCK:
	case VK_FORMAT_BC5_UNORM_BLOCK:
		block_size_x = 4;
		block_size_y = 4;
		if (!is_unorm())
		{
			LOGE("Input format to bc4 must be RGBA8.\n");
			return;
		}
		break;

#ifdef HAVE_ISPC
	case VK_FORMAT_BC6H_UFLOAT_BLOCK:
		block_size_x = 4;
		block_size_y = 4;
		if (!is_16bit())
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

	case VK_FORMAT_BC7_SRGB_BLOCK:
	case VK_FORMAT_BC7_UNORM_BLOCK:
		block_size_x = 4;
		block_size_y = 4;
		if (!is_8bit())
		{
			LOGE("Input format to bc7 must be RGBA8.\n");
			return;
		}

		switch (args.quality)
		{
		case 1:
			if (alpha)
				GetProfile_alpha_ultrafast(&bc7);
			else
				GetProfile_ultrafast(&bc7);
			break;

		case 2:
			if (alpha)
				GetProfile_alpha_veryfast(&bc7);
			else
				GetProfile_veryfast(&bc7);
			break;

		case 3:
			if (alpha)
				GetProfile_alpha_fast(&bc7);
			else
				GetProfile_fast(&bc7);
			break;

		case 4:
			if (alpha)
				GetProfile_alpha_basic(&bc7);
			else
				GetProfile_basic(&bc7);
			break;

		case 5:
			if (alpha)
				GetProfile_alpha_slow(&bc7);
			else
				GetProfile_slow(&bc7);
			break;

		default:
			LOGE("Unknown quality.\n");
			return;
		}
		break;

	case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
	case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
	case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
	case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
	case VK_FORMAT_BC3_SRGB_BLOCK:
	case VK_FORMAT_BC3_UNORM_BLOCK:
		block_size_x = 4;
		block_size_y = 4;
		if (!is_8bit())
		{
			LOGE("Input format to bc1 or bc3 must be RGBA8.\n");
			return;
		}
		break;
#endif

	case VK_FORMAT_ASTC_4x4_UNORM_BLOCK:
	case VK_FORMAT_ASTC_4x4_SRGB_BLOCK:
		if (is_16bit())
		{
			if (!handle_astc_hdr_format(4, 4))
				return;
		}
		else if (!handle_astc_ldr_format(4, 4))
			return;
		break;

	case VK_FORMAT_ASTC_5x5_UNORM_BLOCK:
	case VK_FORMAT_ASTC_5x5_SRGB_BLOCK:
		if (is_16bit())
		{
			if (!handle_astc_hdr_format(5, 5))
				return;
		}
		else if (!handle_astc_ldr_format(5, 5))
			return;
		break;

	case VK_FORMAT_ASTC_6x6_UNORM_BLOCK:
	case VK_FORMAT_ASTC_6x6_SRGB_BLOCK:
		if (is_16bit())
		{
			if (!handle_astc_hdr_format(6, 6))
				return;
		}
		else if (!handle_astc_ldr_format(6, 6))
			return;
		break;

	case VK_FORMAT_ASTC_8x8_UNORM_BLOCK:
	case VK_FORMAT_ASTC_8x8_SRGB_BLOCK:
		if (is_16bit())
		{
			if (!handle_astc_hdr_format(8, 8))
				return;
		}
		else if (!handle_astc_ldr_format(8, 8))
			return;
		break;

	case VK_FORMAT_R8G8B8A8_UNORM:
	case VK_FORMAT_R8G8B8A8_SRGB:
		break;

	default:
		LOGE("Unknown format.\n");
		return;
	}
}

void CompressorState::enqueue_compression_block_rgtc(TaskGroup &group, const CompressorArguments &args, unsigned layer, unsigned level)
{
	auto &layout = input->get_layout();
	int width = layout.get_width(level);
	int height = layout.get_height(level);
	int blocks_x = (width + block_size_x - 1) / block_size_x;

	for (int y = 0; y < height; y += block_size_y)
	{
		for (int x = 0; x < width; x += block_size_x)
		{
			group->enqueue_task([=, format = args.format]() {
				uint8_t padded_red[4 * 4];
				uint8_t padded_green[4 * 4];
				auto *src = static_cast<const uint8_t *>(layout.data(layer, level));

				const auto get_block_data = [&](int block_size) -> uint8_t * {
					auto *dst = static_cast<uint8_t *>(output->get_layout().data(layer, level));
					dst += (x / block_size_x) * block_size;
					dst += (y / block_size_y) * blocks_x * block_size;
					return dst;
				};

				const auto get_encode_data = [&](int block_size) -> uint8_t * {
					return get_block_data(block_size);
				};

				const auto get_component = [&](int sx, int sy, int c) -> uint8_t {
					sx = std::min(sx, width - 1);
					sy = std::min(sy, height - 1);
					return src[4 * (sy * width + sx) + c];
				};

				for (int sy = 0; sy < 4; sy++)
				{
					for (int sx = 0; sx < 4; sx++)
					{
						padded_red[sy * 4 + sx] = get_component(x + sx, y + sy, 0);
						padded_green[sy * 4 + sx] = get_component(x + sx, y + sy, 1);
					}
				}

				switch (format)
				{
				case VK_FORMAT_BC4_UNORM_BLOCK:
				{
					compress_rgtc_red_block(get_encode_data(8), padded_red);

#ifdef RGTC_DEBUG
					if (level == 0 && layer == 0)
					{
						uint8_t decoded_red[16];
						decompress_rgtc_red_block(decoded_red, get_encode_data(8));
						double error = 0.0;
						for (int i = 0; i < 16; i++)
							error += double((decoded_red[i] - padded_red[i]) * (decoded_red[i] - padded_red[i])) / (width * height);

						lock_guard<mutex> l{lock};
						total_error[0] += error;
					}
#endif
					break;
				}

				case VK_FORMAT_BC5_UNORM_BLOCK:
				{
					compress_rgtc_red_green_block(get_encode_data(16), padded_red, padded_green);

#ifdef RGTC_DEBUG
					if (level == 0 && layer == 0)
					{
						uint8_t decoded_red[16];
						uint8_t decoded_green[16];
						decompress_rgtc_red_block(decoded_red, get_encode_data(16));
						decompress_rgtc_red_block(decoded_green, get_encode_data(16) + 8);

						double error_red = 0.0;
						double error_green = 0.0;
						for (int i = 0; i < 16; i++)
							error_red += double((decoded_red[i] - padded_red[i]) * (decoded_red[i] - padded_red[i])) / (width * height);
						for (int i = 0; i < 16; i++)
							error_green += double((decoded_green[i] - padded_green[i]) * (decoded_green[i] - padded_green[i])) / (width * height);

						lock_guard<mutex> l{lock};
						total_error[0] += error_red;
						total_error[1] += error_green;
					}
#endif
					break;
				}

				default:
					break;
				}
			});
		}
	}
}

#ifdef HAVE_ISPC
void CompressorState::enqueue_compression_block_ispc(TaskGroup &group, const CompressorArguments &args,
                                                     unsigned layer, unsigned level)
{
	auto &layout = input->get_layout();
	int width = layout.get_width(level);
	int height = layout.get_height(level);
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
				surface.ptr = const_cast<uint8_t *>(static_cast<const uint8_t *>(layout.data(layer, level)));
				surface.width = std::min(width - x, grid_stride_x);
				surface.height = std::min(height - y, grid_stride_y);
				surface.stride = width * format_to_stride(format);
				surface.ptr += y * surface.stride + x * format_to_stride(format);

				rgba_surface padded_surface = {};

				int num_blocks_x = (surface.width + block_size_x - 1) / block_size_x;
				int num_blocks_y = (surface.height + block_size_y - 1) / block_size_y;
				int blocks_x = (width + block_size_x - 1) / block_size_x;

				const auto get_block_data = [&](int bx, int by, int block_size) -> uint8_t * {
					auto *dst = static_cast<uint8_t *>(output->get_layout().data(layer, level));
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
					padded_surface.width = num_blocks_x * block_size_x;
					padded_surface.height = num_blocks_y * block_size_y;
					padded_surface.stride = padded_surface.width * format_to_stride(format);
					padded_surface.ptr = padded_buffer;
					ReplicateBorders(&padded_surface, &surface, 0, 0, format_to_stride(format) * 8);
				}
				else
					padded_surface = surface;

				switch (format)
				{
				case VK_FORMAT_BC6H_UFLOAT_BLOCK:
				{
					CompressBlocksBC6H(&padded_surface, encode_buffer, &bc6);
					write_encode_data(16);
					break;
				}

				case VK_FORMAT_BC7_SRGB_BLOCK:
				case VK_FORMAT_BC7_UNORM_BLOCK:
				{
					CompressBlocksBC7(&padded_surface, encode_buffer, &bc7);
					write_encode_data(16);
					break;
				}

				case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
				case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
				case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
				case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
				{
					CompressBlocksBC1(&padded_surface, encode_buffer);
					write_encode_data(8);
					break;
				}

				case VK_FORMAT_BC3_SRGB_BLOCK:
				case VK_FORMAT_BC3_UNORM_BLOCK:
				{
					CompressBlocksBC3(&padded_surface, encode_buffer);
					write_encode_data(16);
					break;
				}

				case VK_FORMAT_ASTC_4x4_SRGB_BLOCK:
				case VK_FORMAT_ASTC_4x4_UNORM_BLOCK:
				case VK_FORMAT_ASTC_5x5_SRGB_BLOCK:
				case VK_FORMAT_ASTC_5x5_UNORM_BLOCK:
				case VK_FORMAT_ASTC_6x6_SRGB_BLOCK:
				case VK_FORMAT_ASTC_6x6_UNORM_BLOCK:
				case VK_FORMAT_ASTC_8x8_SRGB_BLOCK:
				case VK_FORMAT_ASTC_8x8_UNORM_BLOCK:
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
                                                     unsigned layer, unsigned level, TextureMode mode)
{
	static FirstASTC first_astc;

	struct CodecState
	{
		error_weighting_params ewp = {};
		vector<uint8_t *> rows;
		vector<uint8_t> buffer;
		astc_codec_image astc_image = {};

		uint32_t layer, level;
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

	if (mode == TextureMode::sRGBA || mode == TextureMode::RGBA)
		state->ewp.rgba_weights[3] = 1.0f;
	else
		state->ewp.rgba_weights[3] = 0.0f;

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

	if (mode == TextureMode::HDR)
	{
		state->ewp.rgb_power = 0.75f;
		state->ewp.alpha_power = 0.75f;
#if 0
		state->ewp.mean_stdev_radius = 0;
		state->ewp.rgb_base_weight = 0.0f;
		state->ewp.rgb_mean_weight = 1.0f;
		state->ewp.alpha_base_weight = 0.0f;
		state->ewp.alpha_mean_weight = 1.0f;
#endif
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

	int width = input->get_layout().get_width(level);
	int height = input->get_layout().get_height(level);

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

			auto *src = static_cast<const uint8_t *>(input->get_layout().data(layer, level));
			memcpy(&state->buffer[dst_offset * estride], &src[src_offset * estride], estride);
		}
	}

	state->ptr = reinterpret_cast<void **>(state->rows.data());

	// Triple-pointer wat.
	if (use_hdr)
		state->astc_image.imagedata16 = reinterpret_cast<uint16_t ***>(&state->ptr);
	else
		state->astc_image.imagedata8 = reinterpret_cast<uint8_t ***>(&state->ptr);

#if 0
	if (state->astc_image.padding > 0 || state->ewp.rgb_mean_weight != 0.0f || state->ewp.rgb_stdev_weight != 0.0f ||
	    state->ewp.alpha_mean_weight != 0.0f || state->ewp.alpha_stdev_weight != 0.0f)
	{
		const swizzlepattern swizzle = { 0, 1, 2, 3 };

		// For some reason, this is not thread-safe :(
		static mutex global_lock;
		lock_guard<mutex> holder{global_lock};

		compute_averages_and_variances(&state->astc_image, state->ewp.rgb_power, state->ewp.alpha_power,
		                               state->ewp.mean_stdev_radius, state->ewp.alpha_radius, swizzle);
	}
#endif

	state->blocks_x = (width + block_size_x - 1) / block_size_x;
	state->blocks_y = (height + block_size_y - 1) / block_size_y;
	state->layer = layer;
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

				auto *dst = static_cast<uint8_t *>(output->get_layout().data(state->layer, state->level));
				memcpy(dst + 16 * (y * state->blocks_x + x), &pcb, sizeof(pcb));
			});
		}
	}
}
#endif

void CompressorState::enqueue_compression(ThreadGroup &group, const CompressorArguments &args)
{
	auto compression_task = group.create_task();

	for (unsigned layer = 0; layer < input->get_layout().get_layers(); layer++)
	{
		for (unsigned level = 0; level < input->get_layout().get_levels(); level++)
		{
			switch (args.format)
			{
			case VK_FORMAT_BC4_UNORM_BLOCK:
			case VK_FORMAT_BC5_UNORM_BLOCK:
				enqueue_compression_block_rgtc(compression_task, args, layer, level);
				break;

#ifdef HAVE_ISPC
			case VK_FORMAT_BC6H_UFLOAT_BLOCK:
			case VK_FORMAT_BC7_SRGB_BLOCK:
			case VK_FORMAT_BC7_UNORM_BLOCK:
			case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
			case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
			case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
			case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
			case VK_FORMAT_BC3_SRGB_BLOCK:
			case VK_FORMAT_BC3_UNORM_BLOCK:
				enqueue_compression_block_ispc(compression_task, args, layer, level);
				break;
#endif

			case VK_FORMAT_ASTC_4x4_SRGB_BLOCK:
			case VK_FORMAT_ASTC_4x4_UNORM_BLOCK:
			case VK_FORMAT_ASTC_5x5_SRGB_BLOCK:
			case VK_FORMAT_ASTC_5x5_UNORM_BLOCK:
			case VK_FORMAT_ASTC_6x6_SRGB_BLOCK:
			case VK_FORMAT_ASTC_6x6_UNORM_BLOCK:
			case VK_FORMAT_ASTC_8x8_SRGB_BLOCK:
			case VK_FORMAT_ASTC_8x8_UNORM_BLOCK:
#ifdef HAVE_ISPC
				if (!use_astc_encoder)
					enqueue_compression_block_ispc(compression_task, args, layer, level);
				else
#endif
				{
#ifdef HAVE_ASTC_ENCODER
					enqueue_compression_block_astc(compression_task, args, layer, level, args.mode);
#endif
				}
				break;

			default:
				return;
			}
		}
	}

	// Pass down ownership to final task.
	auto write_task = group.create_task([args, state = shared_from_this()]() {
		if (state->total_error[0] != 0.0)
			LOGI("Red PSNR: %.f dB\n", 10.0 * log10(255.0 * 255.0 / state->total_error[0]));
		if (state->total_error[1] != 0.0)
			LOGI("Green PSNR: %.f dB\n", 10.0 * log10(255.0 * 255.0 / state->total_error[1]));

		LOGI("Unmapping %u bytes for texture writing.\n", unsigned(state->output->get_required_size()));
		LOGI("Unmapping %u bytes for texture reading.\n", unsigned(state->input->get_required_size()));

		state->output.reset();
		state->input.reset();
	});
	group.add_dependency(write_task, compression_task);
	write_task->set_fence_counter_signal(signal);
}

void compress_texture(ThreadGroup &group, const CompressorArguments &args, const shared_ptr<MemoryMappedTexture> &input,
                      TaskGroup &dep, TaskSignal *signal)
{
	auto output = make_shared<CompressorState>();
	output->input = input;
	output->signal = signal;

	auto setup_task = group.create_task([&group, output, args]() {
		output->output = make_shared<MemoryMappedTexture>();
		auto &layout = output->input->get_layout();

		output->setup(args);

		switch (layout.get_image_type())
		{
		case VK_IMAGE_TYPE_1D:
			output->output->set_1d(args.format, layout.get_width(), layout.get_layers(), layout.get_levels());
			break;
		case VK_IMAGE_TYPE_2D:
			if (output->input->get_flags() & MEMORY_MAPPED_TEXTURE_CUBE_MAP_COMPATIBLE_BIT)
				output->output->set_cube(args.format, layout.get_width(), layout.get_layers() / 6, layout.get_levels());
			else
				output->output->set_2d(args.format, layout.get_width(), layout.get_height(), layout.get_layers(), layout.get_levels());
			break;
		case VK_IMAGE_TYPE_3D:
			output->output->set_3d(args.format, layout.get_width(), layout.get_depth(), layout.get_levels());
			break;
		default:
			LOGE("Unsupported image type.\n");
			return;
		}

		if (!output->output->map_write(args.output))
		{
			LOGE("Failed to map output texture for writing.\n");
			return;
		}

		LOGI("Mapping %u bytes for texture writeout.\n",
		     unsigned(output->output->get_required_size()));

		output->enqueue_compression(group, args);
	});
	group.add_dependency(setup_task, dep);
}
}
