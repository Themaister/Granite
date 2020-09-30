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

#include "texture_compression.hpp"
#include "texture_files.hpp"
#include "format.hpp"
#include <vector>

#ifdef HAVE_ISPC
#include <ispc_texcomp.h>
#endif

#ifdef HAVE_ASTC_ENCODER
#include "astcenc.h"
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
	else if (s == "bc4_unorm")
		return VK_FORMAT_BC4_UNORM_BLOCK;
	else if (s == "bc5_unorm")
		return VK_FORMAT_BC5_UNORM_BLOCK;
	else if (s == "rgba8_unorm")
		return VK_FORMAT_R8G8B8A8_UNORM;
	else if (s == "rg8_unorm")
		return VK_FORMAT_R8G8_UNORM;
	else if (s == "r8_unorm")
		return VK_FORMAT_R8_UNORM;
	else if (s == "rgba16_float")
		return VK_FORMAT_R16G16B16A16_SFLOAT;
	else if (s == "rg16_float")
		return VK_FORMAT_R16G16_SFLOAT;
	else if (s == "r16_float")
		return VK_FORMAT_R16_SFLOAT;
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
static unsigned output_format_to_input_stride(VkFormat format)
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
	void enqueue_compression_block_ispc(TaskGroupHandle &group, const CompressorArguments &args, unsigned layer, unsigned level);
	void enqueue_compression_block_astc(TaskGroupHandle &group, const CompressorArguments &args, unsigned layer, unsigned level, TextureMode mode);
	void enqueue_compression_block_rgtc(TaskGroupHandle &group, const CompressorArguments &args, unsigned layer, unsigned level);
	void enqueue_compression_copy_8bit(TaskGroupHandle &group, const CompressorArguments &, unsigned layer, unsigned level);
	void enqueue_compression_copy_16bit(TaskGroupHandle &group, const CompressorArguments &, unsigned layer, unsigned level);

	double total_error[4] = {};
	mutex lock;
	TaskSignal *signal = nullptr;
};

void CompressorState::setup(const CompressorArguments &args)
{
	output->set_swizzle(args.output_mapping);
	output->set_generate_mipmaps_on_load(args.deferred_mipgen);
#ifdef HAVE_ISPC
	bool alpha = args.mode == TextureMode::sRGBA || args.mode == TextureMode::RGBA;
#endif
	auto &layout = input->get_layout();

	const auto is_8bit = [&]() -> bool {
		return layout.get_format() == VK_FORMAT_R8G8B8A8_SRGB ||
		       layout.get_format() == VK_FORMAT_R8G8B8A8_UNORM;
	};

#ifdef HAVE_ISPC
	const auto is_8bit_rgba = [&]() -> bool {
		return layout.get_format() == VK_FORMAT_R8G8B8A8_SRGB ||
		       layout.get_format() == VK_FORMAT_R8G8B8A8_UNORM;
	};
#endif

	const auto is_unorm = [&]() -> bool {
		return layout.get_format() == VK_FORMAT_R8G8B8A8_UNORM;
	};

	const auto is_16bit_float = [&]() -> bool {
		return layout.get_format() == VK_FORMAT_R16G16B16A16_SFLOAT;
	};

	const auto handle_astc_ldr_format = [&](unsigned x, unsigned y) -> bool {
		block_size_x = x;
		block_size_y = y;
		if (!is_8bit())
		{
			LOGE("Input format to ASTC LDR must be 8-bit.\n");
			return false;
		}

#if defined(HAVE_ASTC_ENCODER)
		use_astc_encoder = true;
#elif defined(HAVE_ISPC)
		bool astc_w_swizzle = args.mode == TextureMode::NormalLA || args.mode == TextureMode::MaskLA;
		if (alpha || astc_w_swizzle)
		{
			if (args.quality <= 3)
				GetProfile_astc_alpha_fast(&astc, x, y);
			else
				GetProfile_astc_alpha_slow(&astc, x, y);
		}
		else
			GetProfile_astc_fast(&astc, x, y);
#endif

		return true;
	};

	const auto handle_astc_hdr_format = [&](unsigned x, unsigned y) -> bool {
#ifdef HAVE_ASTC_ENCODER
		if (!is_16bit_float())
		{
			LOGE("Input format to ASTC HDR must be 16-bit float.\n");
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
		block_size_x = 4;
		block_size_y = 4;
		if (!is_unorm())
		{
			LOGE("Input format to bc4 must be UNORM.\n");
			return;
		}
		break;

	case VK_FORMAT_BC5_UNORM_BLOCK:
		block_size_x = 4;
		block_size_y = 4;
		if (!is_unorm())
		{
			LOGE("Input format to bc5 must be UNORM.\n");
			return;
		}
		break;

#ifdef HAVE_ISPC
	case VK_FORMAT_BC6H_UFLOAT_BLOCK:
		block_size_x = 4;
		block_size_y = 4;
		if (!is_16bit_float())
		{
			LOGE("Input format to bc6h must be float.\n");
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
			LOGE("Input format to bc7 must be 8-bit.\n");
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
		if (!is_8bit_rgba())
		{
			LOGE("Input format to bc1 or bc3 must be RGBA8.\n");
			return;
		}
		break;
#endif

	case VK_FORMAT_ASTC_4x4_UNORM_BLOCK:
	case VK_FORMAT_ASTC_4x4_SRGB_BLOCK:
		if (is_16bit_float())
		{
			if (!handle_astc_hdr_format(4, 4))
				return;
		}
		else if (!handle_astc_ldr_format(4, 4))
			return;
		break;

	case VK_FORMAT_ASTC_5x5_UNORM_BLOCK:
	case VK_FORMAT_ASTC_5x5_SRGB_BLOCK:
		if (is_16bit_float())
		{
			if (!handle_astc_hdr_format(5, 5))
				return;
		}
		else if (!handle_astc_ldr_format(5, 5))
			return;
		break;

	case VK_FORMAT_ASTC_6x6_UNORM_BLOCK:
	case VK_FORMAT_ASTC_6x6_SRGB_BLOCK:
		if (is_16bit_float())
		{
			if (!handle_astc_hdr_format(6, 6))
				return;
		}
		else if (!handle_astc_ldr_format(6, 6))
			return;
		break;

	case VK_FORMAT_ASTC_8x8_UNORM_BLOCK:
	case VK_FORMAT_ASTC_8x8_SRGB_BLOCK:
		if (is_16bit_float())
		{
			if (!handle_astc_hdr_format(8, 8))
				return;
		}
		else if (!handle_astc_ldr_format(8, 8))
			return;
		break;

	case VK_FORMAT_R8G8B8A8_UNORM:
	case VK_FORMAT_R8G8B8A8_SRGB:
	case VK_FORMAT_R8G8_UNORM:
	case VK_FORMAT_R8_UNORM:
		break;

	case VK_FORMAT_R16G16B16A16_SFLOAT:
	case VK_FORMAT_R16G16_SFLOAT:
	case VK_FORMAT_R16_SFLOAT:
		break;

	default:
		LOGE("Unknown format.\n");
		return;
	}
}

void CompressorState::enqueue_compression_copy_16bit(TaskGroupHandle &group, const CompressorArguments &,
                                                     unsigned layer, unsigned level)
{
	group->enqueue_task([=]() {
		auto &input_layout = input->get_layout();
		auto &output_layout = output->get_layout();
		auto input_stride = input_layout.get_block_stride();
		auto output_stride = output_layout.get_block_stride();

		u16vec4 tmp(0, 0, 0, floatToHalf(1.0f));
		if (input_stride <= sizeof(u16vec4) && output_stride <= sizeof(u16vec4))
		{
			unsigned width = input_layout.get_width(level);
			unsigned height = input_layout.get_height(level);

			for (unsigned y = 0; y < height; y++)
			{
				for (unsigned x = 0; x < width; x++)
				{
					void *output_opaque = output_layout.data_opaque(x, y, layer, level);
					void *input_opaque = input_layout.data_opaque(x, y, layer, level);
					memcpy(tmp.data, input_opaque, input_stride);
					memcpy(output_opaque, tmp.data, output_stride);
				}
			}
		}
		else
			LOGE("Format is not as expected.\n");
	});
}

void CompressorState::enqueue_compression_copy_8bit(TaskGroupHandle &group, const CompressorArguments &,
                                                    unsigned layer, unsigned level)
{
	group->enqueue_task([=]() {
		auto &input_layout = input->get_layout();
		auto &output_layout = output->get_layout();
		auto input_stride = input_layout.get_block_stride();
		auto output_stride = output_layout.get_block_stride();

		if (input_stride <= sizeof(u8vec4) && output_stride <= sizeof(u8vec4))
		{
			unsigned width = input_layout.get_width(level);
			unsigned height = input_layout.get_height(level);
			u8vec4 tmp(0, 0, 0, 255);

			for (unsigned y = 0; y < height; y++)
			{
				for (unsigned x = 0; x < width; x++)
				{
					void *output_opaque = output_layout.data_opaque(x, y, layer, level);
					void *input_opaque = input_layout.data_opaque(x, y, layer, level);
					memcpy(tmp.data, input_opaque, input_stride);
					memcpy(output_opaque, tmp.data, output_stride);
				}
			}
		}
		else
			LOGE("Format is not as expected.\n");
	});
}

void CompressorState::enqueue_compression_block_rgtc(TaskGroupHandle &group, const CompressorArguments &args, unsigned layer, unsigned level)
{
	int width = input->get_layout().get_width(level);
	int height = input->get_layout().get_height(level);
	int blocks_x = (width + block_size_x - 1) / block_size_x;

	for (int y = 0; y < height; y += block_size_y)
	{
		for (int x = 0; x < width; x += block_size_x)
		{
			group->enqueue_task([=, format = args.format]() {
				auto &layout = input->get_layout();
				uint8_t padded_red[4 * 4];
				uint8_t padded_green[4 * 4];
				auto *src = static_cast<const uint8_t *>(layout.data(layer, level));
				unsigned pixel_stride = layout.get_block_stride();

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
					return src[pixel_stride * (sy * width + sx) + c];
				};

				for (int sy = 0; sy < 4; sy++)
				{
					for (int sx = 0; sx < 4; sx++)
					{
						padded_red[sy * 4 + sx] = get_component(x + sx, y + sy, 0);
						if (pixel_stride > 1)
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
void CompressorState::enqueue_compression_block_ispc(TaskGroupHandle &group, const CompressorArguments &args,
                                                     unsigned layer, unsigned level)
{
	int width = input->get_layout().get_width(level);
	int height = input->get_layout().get_height(level);
	int grid_stride_x = (32 / block_size_x) * block_size_x;
	int grid_stride_y = (32 / block_size_y) * block_size_y;

	for (int y = 0; y < height; y += grid_stride_y)
	{
		for (int x = 0; x < width; x += grid_stride_x)
		{
			group->enqueue_task([=, format = args.format]() {
				auto &layout = input->get_layout();
				uint8_t padded_buffer[32 * 32 * 8];

				union
				{
					u8vec4 splat_buffer8[32 * 32];
					u16vec4 splat_buffer16[32 * 32];
				};

				uint8_t encode_buffer[16 * 8 * 8];
				rgba_surface surface = {};

				assert(layout.get_block_stride() == output_format_to_input_stride(format));
				surface.ptr = const_cast<uint8_t *>(static_cast<const uint8_t *>(layout.data(layer, level)));
				surface.width = std::min(width - x, grid_stride_x);
				surface.height = std::min(height - y, grid_stride_y);
				surface.stride = width * output_format_to_input_stride(format);
				surface.ptr += y * surface.stride + x * output_format_to_input_stride(format);

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
					padded_surface.stride = padded_surface.width * output_format_to_input_stride(format);
					padded_surface.ptr = padded_buffer;
					ReplicateBorders(&padded_surface, &surface, 0, 0, output_format_to_input_stride(format) * 8);
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
void CompressorState::enqueue_compression_block_astc(TaskGroupHandle &compression_task, const CompressorArguments &args,
                                                     unsigned layer, unsigned level, TextureMode mode)
{
	struct ContextDeleter
	{
		void operator()(astcenc_context *context)
		{
			astcenc_context_free(context);
		}
	};

	struct CodecState
	{
		astcenc_config config;
		std::unique_ptr<astcenc_context, ContextDeleter> context;
		uint32_t layer, level;
		int blocks_x, blocks_y;

		std::vector<uint8_t *> rows_8;
		std::vector<uint16_t *> rows_16;
		std::vector<u8vec4> data_padded_8;
		std::vector<u16vec4> data_padded_16;
		uint8_t **slice_8 = nullptr;
		uint16_t **slice_16 = nullptr;
		astcenc_image image;
	};

	auto state = make_shared<CodecState>();
	unsigned flags = 0;

	astcenc_profile profile;
	switch (mode)
	{
	case TextureMode::HDR:
		profile = ASTCENC_PRF_HDR;
		break;

	case TextureMode::sRGBA:
	case TextureMode::sRGB:
		profile = ASTCENC_PRF_LDR_SRGB;
		break;

	case TextureMode::RGB:
	case TextureMode::RGBA:
	case TextureMode::Normal:
	case TextureMode::Mask:
	case TextureMode::NormalLA:
	case TextureMode::MaskLA:
	case TextureMode::Luminance:
		profile = ASTCENC_PRF_LDR;
		break;

	default:
		LOGE("Unknown TextureMode.\n");
		return;
	}

	bool use_alpha_weight = mode == TextureMode::sRGBA || mode == TextureMode::RGBA;
	bool use_alpha_channel = use_alpha_weight || mode == TextureMode::NormalLA || mode == TextureMode::MaskLA;
	if (mode == TextureMode::NormalLA)
		flags |= ASTCENC_FLG_MAP_NORMAL;
	else if (mode == TextureMode::MaskLA)
		flags |= ASTCENC_FLG_MAP_MASK;
	else if (use_alpha_weight)
		flags |= ASTCENC_FLG_USE_ALPHA_WEIGHT;

	astcenc_preset preset;
	if (args.quality >= 5)
		preset = ASTCENC_PRE_EXHAUSTIVE;
	else if (args.quality >= 4)
		preset = ASTCENC_PRE_THOROUGH;
	else if (args.quality >= 3)
		preset = ASTCENC_PRE_MEDIUM;
	else
		preset = ASTCENC_PRE_FAST;

	if (astcenc_init_config(profile, block_size_x, block_size_y, 1,
	                        preset, flags, state->config) != ASTCENC_SUCCESS)
	{
		LOGE("Failed to initialize ASTC encoder config.\n");
		return;
	}

	int width = input->get_layout().get_width(level);
	int height = input->get_layout().get_height(level);

	// Seems to be a bug in astcenc itself.

#if 0
	auto *group = compression_task->get_thread_group();
	int num_blocks_x = (width + block_size_x - 1) / block_size_x;
	int num_blocks_y = (height + block_size_y - 1) / block_size_y;

	// There are some weird bugs happening when using multi-threading.
	int num_blocks = num_blocks_x * num_blocks_y;
	int num_threads = (num_blocks + 127) / 128;
#else
	constexpr int num_threads = 1;
#endif

	astcenc_context *context = nullptr;
	if (astcenc_context_alloc(state->config, num_threads, &context) != ASTCENC_SUCCESS)
	{
		LOGE("Failed to allocate ASTC encoding context.\n");
		return;
	}

	state->context.reset(context);

	constexpr int padding_pixels = 8;
	int padded_width = width + padding_pixels * 2;
	int padded_height = height + padding_pixels * 2;

	VkFormat input_format = input->get_layout().get_format();
	if (input_format == VK_FORMAT_R16G16B16A16_SFLOAT)
	{
		state->data_padded_16.resize(padded_width * height);
		state->rows_16.reserve(padded_height);
		for (unsigned y = 0; y < padding_pixels; y++)
			state->rows_16.push_back(&state->data_padded_16[0].x);
		for (int y = 0; y < height; y++)
			state->rows_16.push_back(&state->data_padded_16[y * padded_width].x);
		for (unsigned y = 0; y < padding_pixels; y++)
			state->rows_16.push_back(&state->data_padded_16[(height - 1) * padded_width].x);
		state->slice_16 = state->rows_16.data();
	}
	else
	{
		state->data_padded_8.resize(padded_width * height);
		state->rows_8.reserve(padded_height);
		for (unsigned y = 0; y < padding_pixels; y++)
			state->rows_8.push_back(&state->data_padded_8[0].x);
		for (int y = 0; y < height; y++)
			state->rows_8.push_back(&state->data_padded_8[y * padded_width].x);
		for (unsigned y = 0; y < padding_pixels; y++)
			state->rows_8.push_back(&state->data_padded_8[(height - 1) * padded_width].x);
		state->slice_8 = state->rows_8.data();
	}

	int pixel_size = Vulkan::TextureFormatLayout::format_block_size(input_format, VK_IMAGE_ASPECT_COLOR_BIT);

	for (int y = 0; y < height; y++)
	{
		for (int x = 0; x < padded_width; x++)
		{
			int cx = clamp(x - padding_pixels, 0, width - 1);
			if (input_format == VK_FORMAT_R16G16B16A16_SFLOAT)
			{
				auto *src = input->get_layout().data_opaque(cx, y, layer, level);
				memcpy(&state->data_padded_16[y * padded_width + x], src, pixel_size);
			}
			else
			{
				auto *src = input->get_layout().data_opaque(cx, y, layer, level);
				memcpy(&state->data_padded_8[y * padded_width + x], src, pixel_size);
			}
		}
	}

	state->layer = layer;
	state->level = level;

	state->image.dim_x = width;
	state->image.dim_y = height;
	state->image.dim_z = 1;
	state->image.dim_pad = padding_pixels;
	state->image.data16 = &state->slice_16;
	state->image.data8 = &state->slice_8;

	const astcenc_swizzle swiz = {
		ASTCENC_SWZ_R,
		ASTCENC_SWZ_G,
		ASTCENC_SWZ_B,
		use_alpha_channel ? ASTCENC_SWZ_A : ASTCENC_SWZ_1
	};

#if 0
	if (astcenc_compress_image_multistage(state->context.get(), state->image, swiz,
			ASTCENC_COMPRESS_STAGE_INIT,
			static_cast<uint8_t *>(output->get_layout().data(state->layer, state->level)),
			output->get_layout().get_layer_size(state->level), 0) != ASTCENC_SUCCESS)
	{
		LOGE("Failed to initialize ASTC encoder.\n");
		return;
	}

	auto compute_task = group->create_task();
	for (int i = 0; i < num_threads; i++)
	{
		compute_task->enqueue_task([this, state, i, swiz]() {
			if (astcenc_compress_image_multistage(state->context.get(), state->image, swiz,
					ASTCENC_COMPRESS_STAGE_COMPUTE_AVERAGES_AND_VARIANCE,
					static_cast<uint8_t *>(output->get_layout().data(state->layer, state->level)),
					output->get_layout().get_layer_size(state->level), i) != ASTCENC_SUCCESS)
			{
				LOGE("Failed to run compute averages and variance stage.\n");
			}
		});
	}
#endif

	for (int i = 0; i < num_threads; i++)
	{
		compression_task->enqueue_task([this, state, i, swiz]() {
#if 0
			if (astcenc_compress_image_multistage(state->context.get(), state->image, swiz,
					ASTCENC_COMPRESS_STAGE_EXECUTE,
					static_cast<uint8_t *>(output->get_layout().data(state->layer, state->level)),
					output->get_layout().get_layer_size(state->level), i) != ASTCENC_SUCCESS)
#else
			if (astcenc_compress_image(state->context.get(), state->image, swiz,
					static_cast<uint8_t *>(output->get_layout().data(state->layer, state->level)),
					output->get_layout().get_layer_size(state->level), i) != ASTCENC_SUCCESS)
#endif
			{
				LOGE("Failed to compress ASTC blocks.\n");
			}
		});
	}

#if 0
	group->add_dependency(compression_task, compute_task);

	auto cleanup_task = group->create_task([this, state, swiz]() {
		if (astcenc_compress_image_multistage(state->context.get(), state->image, swiz,
				ASTCENC_COMPRESS_STAGE_CLEANUP,
				static_cast<uint8_t *>(output->get_layout().data(state->layer, state->level)),
				output->get_layout().get_layer_size(state->level), 0) != ASTCENC_SUCCESS)
		{
			LOGE("Failed to cleanup ASTC encoder.\n");
		}
	});
	group->add_dependency(cleanup_task, compression_task);
#endif
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

			case VK_FORMAT_BC6H_UFLOAT_BLOCK:
			case VK_FORMAT_BC7_SRGB_BLOCK:
			case VK_FORMAT_BC7_UNORM_BLOCK:
			case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
			case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
			case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
			case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
			case VK_FORMAT_BC3_SRGB_BLOCK:
			case VK_FORMAT_BC3_UNORM_BLOCK:
#ifdef HAVE_ISPC
				enqueue_compression_block_ispc(compression_task, args, layer, level);
#endif
				break;

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

			case VK_FORMAT_R8G8B8A8_SRGB:
			case VK_FORMAT_R8G8B8A8_UNORM:
			case VK_FORMAT_R8G8_UNORM:
			case VK_FORMAT_R8_UNORM:
				enqueue_compression_copy_8bit(compression_task, args, layer, level);
				break;

			case VK_FORMAT_R16G16B16A16_SFLOAT:
			case VK_FORMAT_R16G16_SFLOAT:
			case VK_FORMAT_R16_SFLOAT:
				enqueue_compression_copy_16bit(compression_task, args, layer, level);
				break;

			default:
				break;
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
	group.add_dependency(*write_task, *compression_task);
	write_task->set_fence_counter_signal(signal);
}

bool compress_texture(ThreadGroup &group, const CompressorArguments &args, const shared_ptr<MemoryMappedTexture> &input,
                      TaskGroupHandle &dep, TaskSignal *signal)
{
	auto output = make_shared<CompressorState>();
	output->input = input;
	output->signal = signal;

	switch (input->get_layout().get_format())
	{
	case VK_FORMAT_R8G8B8A8_SRGB:
	case VK_FORMAT_R8G8B8A8_UNORM:
	case VK_FORMAT_R16G16B16A16_SFLOAT:
		break;

	default:
		LOGE("Unsupported input format for compression: %u\n", unsigned(input->get_layout().get_format()));
		return false;
	}

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
			if (output->signal)
				output->signal->signal_increment();
			return;
		}

		LOGI("Mapping %u bytes for texture writeout.\n",
		     unsigned(output->output->get_required_size()));

		output->enqueue_compression(group, args);
	});
	group.add_dependency(*setup_task, *dep);

	return true;
}
}
