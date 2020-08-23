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

#include "device.hpp"
#include "context.hpp"
#include "global_managers.hpp"
#include "texture_decoder.hpp"
#include "memory_mapped_texture.hpp"
#include "math.hpp"
#include "muglm/muglm_impl.hpp"
#include <random>

#ifdef HAVE_ASTC_DECODER
#include "astcenc.h"
#endif

using namespace Granite;
using namespace Vulkan;

static BufferHandle readback_image(CommandBuffer &cmd, const Image &image)
{
	BufferCreateInfo readback_info;
	readback_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	readback_info.domain = BufferDomain::CachedHost;
	readback_info.size = image.get_width() * image.get_height() *
	                     TextureFormatLayout::format_block_size(image.get_format(),
	                                                            VK_IMAGE_ASPECT_COLOR_BIT);
	auto readback_buffer = cmd.get_device().create_buffer(readback_info);

	cmd.copy_image_to_buffer(*readback_buffer, image, 0,
	                         {}, { image.get_width(), image.get_height(), 1 },
	                         0, 0,
	                         { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 });

	cmd.barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
	            VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
	return readback_buffer;
}

static bool compare_r8(Device &device,
                       const Buffer &reference, const Buffer &decoded,
                       unsigned width, unsigned height, int max_diff)
{
	auto *mapped_reference = static_cast<const uint8_t *>(device.map_host_buffer(reference, MEMORY_ACCESS_READ_BIT));
	auto *mapped_decoded = static_cast<const uint8_t *>(device.map_host_buffer(decoded, MEMORY_ACCESS_READ_BIT));

	for (unsigned y = 0; y < height; y++)
	{
		for (unsigned x = 0; x < width; x++)
		{
			auto &ref = mapped_reference[y * width + x];
			auto &dec = mapped_decoded[y * width + x];

			int diff_r = muglm::abs(int(ref) - int(dec));

			if (diff_r > max_diff)
			{
				LOGE("(%u, %u): Reference (%u) != (%u).\n",
				     x, y, ref, dec);
				return false;
			}
		}
	}

	return true;
}

static bool compare_r16f(Device &device,
                         const Buffer &reference, const Buffer &decoded,
                         unsigned width, unsigned height)
{
	auto *mapped_reference = static_cast<const uint16_t *>(device.map_host_buffer(reference, MEMORY_ACCESS_READ_BIT));
	auto *mapped_decoded = static_cast<const uint16_t *>(device.map_host_buffer(decoded, MEMORY_ACCESS_READ_BIT));

	for (unsigned y = 0; y < height; y++)
	{
		for (unsigned x = 0; x < width; x++)
		{
			auto &ref = mapped_reference[y * width + x];
			auto &dec = mapped_decoded[y * width + x];

			int diff_r = muglm::abs(int(ref) - int(dec));

			if (diff_r)
			{
				LOGE("(%u, %u): Reference (%u) != (%u).\n",
				     x, y, ref, dec);
				return false;
			}
		}
	}

	return true;
}

static bool compare_rg8(Device &device,
                          const Buffer &reference, const Buffer &decoded,
                          unsigned width, unsigned height, int max_diff)
{
	auto *mapped_reference = static_cast<const u8vec2 *>(device.map_host_buffer(reference, MEMORY_ACCESS_READ_BIT));
	auto *mapped_decoded = static_cast<const u8vec2 *>(device.map_host_buffer(decoded, MEMORY_ACCESS_READ_BIT));

	for (unsigned y = 0; y < height; y++)
	{
		for (unsigned x = 0; x < width; x++)
		{
			auto &ref = mapped_reference[y * width + x];
			auto &dec = mapped_decoded[y * width + x];

			int diff_r = muglm::abs(int(ref.x) - int(dec.x));
			int diff_g = muglm::abs(int(ref.y) - int(dec.y));
			int diff_max = muglm::max(diff_r, diff_g);

			if (diff_max > max_diff)
			{
				LOGE("(%u, %u): Reference (%u, %u) != (%u, %u).\n",
				     x, y,
				     ref.x, ref.y, dec.x, dec.y);
				return false;
			}
		}
	}

	return true;
}

static bool compare_rg16f(Device &device,
                          const Buffer &reference, const Buffer &decoded,
                          unsigned width, unsigned height)
{
	auto *mapped_reference = static_cast<const u16vec2 *>(device.map_host_buffer(reference, MEMORY_ACCESS_READ_BIT));
	auto *mapped_decoded = static_cast<const u16vec2 *>(device.map_host_buffer(decoded, MEMORY_ACCESS_READ_BIT));

	for (unsigned y = 0; y < height; y++)
	{
		for (unsigned x = 0; x < width; x++)
		{
			auto &ref = mapped_reference[y * width + x];
			auto &dec = mapped_decoded[y * width + x];

			int diff_r = muglm::abs(int(ref.x) - int(dec.x));
			int diff_g = muglm::abs(int(ref.y) - int(dec.y));
			int diff_max = muglm::max(diff_r, diff_g);

			if (diff_max)
			{
				LOGE("(%u, %u): Reference (%u, %u) != (%u, %u).\n",
				     x, y,
				     ref.x, ref.y, dec.x, dec.y);
				return false;
			}
		}
	}

	return true;
}

static bool compare_rgba8(Device &device,
                          const Buffer &reference, const Buffer &decoded,
                          unsigned width, unsigned height, int max_diff)
{
	auto *mapped_reference = static_cast<const u8vec4 *>(device.map_host_buffer(reference, MEMORY_ACCESS_READ_BIT));
	auto *mapped_decoded = static_cast<const u8vec4 *>(device.map_host_buffer(decoded, MEMORY_ACCESS_READ_BIT));

	for (unsigned y = 0; y < height; y++)
	{
		for (unsigned x = 0; x < width; x++)
		{
			auto &ref = mapped_reference[y * width + x];
			auto &dec = mapped_decoded[y * width + x];

			int diff_r = muglm::abs(int(ref.x) - int(dec.x));
			int diff_g = muglm::abs(int(ref.y) - int(dec.y));
			int diff_b = muglm::abs(int(ref.z) - int(dec.z));
			int diff_a = muglm::abs(int(ref.w) - int(dec.w));
			int diff_max = muglm::max(muglm::max(diff_r, diff_g), muglm::max(diff_b, diff_a));

			if (diff_max > max_diff)
			{
				LOGE("(%u, %u): Reference (%u, %u, %u, %u) != (%u, %u, %u, %u).\n",
				     x, y,
				     ref.x, ref.y, ref.z, ref.w,
				     dec.x, dec.y, dec.z, dec.w);
				return false;
			}
		}
	}

	return true;
}

static bool compare_rgba16f(Device &device,
                            const Buffer &reference, const Buffer &decoded,
                            unsigned width, unsigned height)
{
	auto *mapped_reference = static_cast<const u16vec4 *>(device.map_host_buffer(reference, MEMORY_ACCESS_READ_BIT));
	auto *mapped_decoded = static_cast<const u16vec4 *>(device.map_host_buffer(decoded, MEMORY_ACCESS_READ_BIT));

	for (unsigned y = 0; y < height; y++)
	{
		for (unsigned x = 0; x < width; x++)
		{
			auto ref = mapped_reference[y * width + x];
			auto dec = mapped_decoded[y * width + x];

			int diff_r = muglm::abs(int(ref.x) - int(dec.x));
			int diff_g = muglm::abs(int(ref.y) - int(dec.y));
			int diff_b = muglm::abs(int(ref.z) - int(dec.z));
			int diff_a = muglm::abs(int(ref.w) - int(dec.w));
			int diff_max = muglm::max(muglm::max(diff_r, diff_g), muglm::max(diff_b, diff_a));

			if (diff_max)
			{
				LOGE("(%u, %u): Reference (%u, %u, %u, %u) != (%u, %u, %u, %u).\n",
				     x, y,
				     ref.x, ref.y, ref.z, ref.w,
				     dec.x, dec.y, dec.z, dec.w);
				return false;
			}
		}
	}

	return true;
}

#ifdef HAVE_ASTC_DECODER
static BufferHandle decode_astc_cpu(Device &device, const TextureFormatLayout &layout, VkFormat readback_format)
{
	astcenc_config config = {};
	uint32_t block_width, block_height;
	TextureFormatLayout::format_block_dim(layout.get_format(), block_width, block_height);
	bool srgb = Vulkan::format_is_srgb(readback_format);
	astcenc_init_config(srgb ? ASTCENC_PRF_LDR_SRGB : ASTCENC_PRF_HDR, block_width, block_height, 1, ASTCENC_PRE_FAST, 0, config);

	astcenc_context *ctx = nullptr;
	if (astcenc_context_alloc(config, 1, &ctx) != ASTCENC_SUCCESS)
		return {};

	astcenc_image image = {};
	image.dim_pad = 0;
	image.dim_x = layout.get_width();
	image.dim_y = layout.get_height();
	image.dim_z = 1;

	Vulkan::BufferCreateInfo buffer_info = {};
	buffer_info.size = layout.get_width() * layout.get_height() * (srgb ? 4 : 8);
	buffer_info.domain = BufferDomain::CachedHost;
	buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	auto buffer = device.create_buffer(buffer_info);


	uint16_t **p_rows16 = nullptr;
	uint8_t **p_rows8 = nullptr;
	std::vector<uint16_t *> rows16;
	std::vector<uint8_t *> rows8;

	if (srgb)
	{
		auto *mapped = static_cast<uint8_t *>(device.map_host_buffer(*buffer, MEMORY_ACCESS_WRITE_BIT));
		rows8.reserve(layout.get_height());
		for (unsigned y = 0; y < layout.get_height(); y++)
			rows8.push_back(mapped + y * layout.get_width() * 4);
		p_rows8 = rows8.data();
		image.data8 = &p_rows8;
	}
	else
	{
		auto *mapped = static_cast<uint16_t *>(device.map_host_buffer(*buffer, MEMORY_ACCESS_WRITE_BIT));
		rows16.reserve(layout.get_height());
		for (unsigned y = 0; y < layout.get_height(); y++)
			rows16.push_back(mapped + y * layout.get_width() * 4);
		p_rows16 = rows16.data();
		image.data16 = &p_rows16;
	}

	if (astcenc_decompress_image(ctx, static_cast<const uint8_t *>(layout.data()),
	                             layout.get_layer_size(0), image, {
		                             ASTCENC_SWZ_R,
		                             ASTCENC_SWZ_G,
		                             ASTCENC_SWZ_B,
		                             ASTCENC_SWZ_A }) != ASTCENC_SUCCESS)
	{
		buffer.reset();
	}
	astcenc_context_free(ctx);
	return buffer;
}
#endif

static BufferHandle decode_gpu(CommandBuffer &cmd, const TextureFormatLayout &layout, VkFormat format)
{
#ifdef HAVE_ASTC_DECODER
	if (Vulkan::format_compression_type(layout.get_format()) == Vulkan::FormatCompressionType::ASTC)
		return decode_astc_cpu(cmd.get_device(), layout, format);
#endif

	auto &device = cmd.get_device();
	auto uploaded_info = ImageCreateInfo::immutable_image(layout);
	uploaded_info.initial_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	uploaded_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	auto uploaded_staging = device.create_image_staging_buffer(layout);
	auto uploaded_tex = device.create_image_from_staging_buffer(uploaded_info, &uploaded_staging);

	auto rt_info = ImageCreateInfo::render_target(layout.get_width(), layout.get_height(), format);
	rt_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	rt_info.initial_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	auto rt = device.create_image(rt_info);

	cmd.blit_image(*rt, *uploaded_tex,
	               {}, { int(layout.get_width()), int(layout.get_height()), 1 },
	               {}, { int(layout.get_width()), int(layout.get_height()), 1 },
	               0, 0,
	               0, 0, 1, VK_FILTER_NEAREST);

	cmd.image_barrier(*rt, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
	                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);

	return readback_image(cmd, *rt);
}

static BufferHandle decode_compute(CommandBuffer &cmd, const TextureFormatLayout &layout)
{
	auto compressed = decode_compressed_image(cmd, layout);
	if (!compressed)
		return {};
	cmd.image_barrier(*compressed, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
	                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
	return readback_image(cmd, *compressed);
}

template <bool dual_plane>
static bool test_astc_weights(Device &device, VkFormat format, VkFormat readback_format)
{
	auto cmd = device.request_command_buffer();
	std::mt19937 rnd(1337);
	SceneFormats::MemoryMappedTexture tex;
	unsigned width = 2048;
	unsigned height = 2048;

	unsigned block_width, block_height;
	Vulkan::TextureFormatLayout::format_block_dim(format, block_width, block_height);

	unsigned blocks_x = (width + block_width - 1) / block_width;
	unsigned blocks_y = (height + block_height - 1) / block_height;
	unsigned num_blocks = blocks_x * blocks_y;
	tex.set_2d(format, width, height);
	if (!tex.map_write_scratch())
		return false;

	auto &layout = tex.get_layout();
	auto *d = static_cast<uint32_t *>(layout.data_opaque(0, 0, 0, 0));

	// Expose all possible weight encoding formats.
	for (unsigned i = 0; i < num_blocks; i++, d += 4)
	{
		d[0] = 0;
		d[1] = 0;
		d[2] = 0;
		d[3] = 0;

		d[0] |= uint32_t(dual_plane) << 10;

		unsigned weight_bits = i & 15;
		while ((weight_bits & 6) == 0)
			weight_bits++;

		d[0] |= ((weight_bits >> 3) & 1) << 9;
		d[0] |= ((weight_bits >> 2) & 1) << 1;
		d[0] |= ((weight_bits >> 1) & 1) << 0;
		d[0] |= ((weight_bits >> 0) & 1) << 4;

		// Endpoint type
		d[0] |= 0 << 13;

		// 4x4 weight grid.
		d[0] |= 0 << 7;
		d[0] |= 2 << 5;

		// Randomize endpoint and weights.
		d[0] |= uint32_t(rnd()) << 17;
		d[1] = uint32_t(rnd());
		d[2] = uint32_t(rnd());
		d[3] = uint32_t(rnd());
	}

	auto readback_reference = decode_gpu(*cmd, layout, readback_format);
	auto readback_decoded = decode_compute(*cmd, layout);
	if (!readback_decoded)
	{
		device.submit_discard(cmd);
		return false;
	}

	Fence fence;
	device.submit(cmd, &fence);
	fence->wait();

	if (readback_format == VK_FORMAT_R16G16B16A16_SFLOAT)
		return compare_rgba16f(device, *readback_reference, *readback_decoded, width, height);
	else if (readback_format == VK_FORMAT_R8G8B8A8_SRGB)
		return compare_rgba8(device, *readback_reference, *readback_decoded, width, height, 0);
	else
		return false;
}

static bool test_astc_endpoint_formats(Device &device, VkFormat format, VkFormat readback_format)
{
	auto cmd = device.request_command_buffer();
	std::mt19937 rnd(1337);
	SceneFormats::MemoryMappedTexture tex;
	unsigned width = 8092;
	unsigned height = 8092;

	unsigned block_width, block_height;
	Vulkan::TextureFormatLayout::format_block_dim(format, block_width, block_height);

	unsigned blocks_x = (width + block_width - 1) / block_width;
	unsigned blocks_y = (height + block_height - 1) / block_height;
	unsigned num_blocks = blocks_x * blocks_y;
	tex.set_2d(format, width, height);
	if (!tex.map_write_scratch())
		return false;

	auto &layout = tex.get_layout();
	auto *d = static_cast<uint32_t *>(layout.data_opaque(0, 0, 0, 0));

	// Expose all possible weight encoding formats.
	for (unsigned i = 0; i < num_blocks; i++, d += 4)
	{
		d[0] = 0;
		d[1] = 0;
		d[2] = 0;
		d[3] = 0;

		unsigned weight_bits = (i >> 4) & 15;
		while ((weight_bits & 6) == 0)
			weight_bits++;

		d[0] |= ((weight_bits >> 3) & 1) << 9;
		d[0] |= ((weight_bits >> 2) & 1) << 1;
		d[0] |= ((weight_bits >> 1) & 1) << 0;
		d[0] |= ((weight_bits >> 0) & 1) << 4;

		// Endpoint type
		d[0] |= (i & 15) << 13;

		// 4x4 weight grid.
		d[0] |= 0 << 7;
		d[0] |= 2 << 5;

		// Randomize endpoint and weights.
		d[0] |= uint32_t(rnd()) << 17;
		d[1] = uint32_t(rnd());
		d[2] = uint32_t(rnd());
		d[3] = uint32_t(rnd());
	}

	auto readback_reference = decode_gpu(*cmd, layout, readback_format);
	auto readback_decoded = decode_compute(*cmd, layout);
	if (!readback_decoded)
	{
		device.submit_discard(cmd);
		return false;
	}

	Fence fence;
	device.submit(cmd, &fence);
	fence->wait();

	if (readback_format == VK_FORMAT_R16G16B16A16_SFLOAT)
		return compare_rgba16f(device, *readback_reference, *readback_decoded, width, height);
	else if (readback_format == VK_FORMAT_R8G8B8A8_SRGB)
		return compare_rgba8(device, *readback_reference, *readback_decoded, width, height, 0);
	else
		return false;
}

template <bool dual_plane>
static bool test_astc_partitions(Device &device, VkFormat format, VkFormat readback_format)
{
	auto cmd = device.request_command_buffer();
	std::mt19937 rnd(1339);
	SceneFormats::MemoryMappedTexture tex;
	unsigned width = 2048;
	unsigned height = 2048;

	unsigned block_width, block_height;
	Vulkan::TextureFormatLayout::format_block_dim(format, block_width, block_height);

	unsigned blocks_x = (width + block_width - 1) / block_width;
	unsigned blocks_y = (height + block_height - 1) / block_height;
	unsigned num_blocks = blocks_x * blocks_y;
	tex.set_2d(format, width, height);
	if (!tex.map_write_scratch())
		return false;

	auto &layout = tex.get_layout();
	auto *d = static_cast<uint32_t *>(layout.data_opaque(0, 0, 0, 0));

	// Expose all possible weight encoding formats.
	for (unsigned i = 0; i < num_blocks; i++, d += 4)
	{
		d[0] = 0;
		d[1] = 0;
		d[2] = 0;
		d[3] = 0;

		d[0] |= uint32_t(dual_plane) << 10;

		unsigned weight_bits = 5;
		d[0] |= ((weight_bits >> 3) & 1) << 9;
		d[0] |= ((weight_bits >> 2) & 1) << 1;
		d[0] |= ((weight_bits >> 1) & 1) << 0;
		d[0] |= ((weight_bits >> 0) & 1) << 4;

		// 4x4 weight grid.
		d[0] |= 0 << 7;
		d[0] |= 2 << 5;

		unsigned seed = i & 1023;
		unsigned num_partitions_1 = (i >> 10) & 3;

		d[0] |= num_partitions_1 << 11;
		d[0] |= seed << 13;

		// Constant CEM, variable endpoint type.
		d[0] |= ((i >> 12) & 0xf) << 25;

		// Randomize endpoint and weights.
		d[0] |= uint32_t(rnd()) << 29;
		d[1] = uint32_t(rnd());
		d[2] = uint32_t(rnd());
		d[3] = uint32_t(rnd());
	}

	auto readback_reference = decode_gpu(*cmd, layout, readback_format);
	auto readback_decoded = decode_compute(*cmd, layout);
	if (!readback_decoded)
	{
		device.submit_discard(cmd);
		return false;
	}

	Fence fence;
	device.submit(cmd, &fence);
	fence->wait();

	if (readback_format == VK_FORMAT_R16G16B16A16_SFLOAT)
		return compare_rgba16f(device, *readback_reference, *readback_decoded, width, height);
	else if (readback_format == VK_FORMAT_R8G8B8A8_SRGB)
		return compare_rgba8(device, *readback_reference, *readback_decoded, width, height, 0);
	else
		return false;
}

template <bool dual_plane>
static bool test_astc_partitions_complex(Device &device, VkFormat format, VkFormat readback_format)
{
	auto cmd = device.request_command_buffer();
	std::mt19937 rnd(1338);
	SceneFormats::MemoryMappedTexture tex;
	unsigned width = 2048;
	unsigned height = 2048;

	unsigned block_width, block_height;
	Vulkan::TextureFormatLayout::format_block_dim(format, block_width, block_height);

	unsigned blocks_x = (width + block_width - 1) / block_width;
	unsigned blocks_y = (height + block_height - 1) / block_height;
	unsigned num_blocks = blocks_x * blocks_y;
	tex.set_2d(format, width, height);
	if (!tex.map_write_scratch())
		return false;

	auto &layout = tex.get_layout();
	auto *d = static_cast<uint32_t *>(layout.data_opaque(0, 0, 0, 0));

	// Expose all possible weight encoding formats.
	for (unsigned i = 0; i < num_blocks; i++, d += 4)
	{
		d[0] = 0;
		d[1] = 0;
		d[2] = 0;
		d[3] = 0;

		d[0] |= uint32_t(dual_plane) << 10;

		constexpr unsigned weight_bits = dual_plane ? 2 : 5;
		d[0] |= ((weight_bits >> 3) & 1) << 9;
		d[0] |= ((weight_bits >> 2) & 1) << 1;
		d[0] |= ((weight_bits >> 1) & 1) << 0;
		d[0] |= ((weight_bits >> 0) & 1) << 4;

		// 4x4 weight grid.
		d[0] |= 0 << 7;
		d[0] |= 2 << 5;

		unsigned seed = i & 1023;
		unsigned num_partitions_1 = (i >> 10) & 3;

		d[0] |= num_partitions_1 << 11;
		d[0] |= seed << 13;

		d[0] |= ((i >> 12) & 0x3f) << 23;

		// Randomize endpoint and weights.
		d[0] |= uint32_t(rnd()) << 29;
		d[1] = uint32_t(rnd());
		d[2] = uint32_t(rnd());
		d[3] = uint32_t(rnd());
	}

	auto readback_reference = decode_gpu(*cmd, layout, readback_format);
	auto readback_decoded = decode_compute(*cmd, layout);
	if (!readback_decoded)
	{
		device.submit_discard(cmd);
		return false;
	}

	Fence fence;
	device.submit(cmd, &fence);
	fence->wait();

	if (readback_format == VK_FORMAT_R16G16B16A16_SFLOAT)
		return compare_rgba16f(device, *readback_reference, *readback_decoded, width, height);
	else if (readback_format == VK_FORMAT_R8G8B8A8_SRGB)
		return compare_rgba8(device, *readback_reference, *readback_decoded, width, height, 0);
	else
		return false;
}

static bool test_astc_void_extent(Device &device, VkFormat format, VkFormat readback_format)
{
	auto cmd = device.request_command_buffer();
	std::mt19937 rnd(1338);
	SceneFormats::MemoryMappedTexture tex;
	unsigned width = 2048;
	unsigned height = 2048;

	unsigned block_width, block_height;
	Vulkan::TextureFormatLayout::format_block_dim(format, block_width, block_height);

	unsigned blocks_x = (width + block_width - 1) / block_width;
	unsigned blocks_y = (height + block_height - 1) / block_height;
	unsigned num_blocks = blocks_x * blocks_y;
	tex.set_2d(format, width, height);
	if (!tex.map_write_scratch())
		return false;

	auto &layout = tex.get_layout();
	auto *d = static_cast<uint32_t *>(layout.data_opaque(0, 0, 0, 0));

	// Expose all possible weight encoding formats.
	for (unsigned i = 0; i < num_blocks; i++, d += 4)
	{
		d[0] = 0;
		d[1] = 0;
		d[2] = 0;
		d[3] = 0;

		d[0] |= 0x1fc;

		// HDR vs LDR selector.
		bool HDR = (i & 1) != 0;
		d[0] |= uint32_t(HDR) << 9;

		// Reserved bits must be 1.
		d[0] |= 3 << 10;

		// All 1s for S coord.
		if (i & 2)
		{
			d[0] |= ~0u << 12;
			d[1] |= (1 << 6) - 1;
		}

		// All 1s for T coord
		if (i & 4)
			d[1] = ~0u << 6;

		d[0] |= uint32_t(rnd()) << 12;
		d[1] |= uint32_t(rnd());

		if (HDR)
		{
			// Inputs must be finite, or we get undefined behavior.
			auto r = uint16_t(rnd());
			auto g = uint16_t(rnd());
			auto b = uint16_t(rnd());
			auto a = uint16_t(rnd());
			const auto fixup = [](uint16_t &v) { if (((v & 0x7fff) >> 10) >= 0x1f) v = 0; };
			fixup(r);
			fixup(g);
			fixup(b);
			fixup(a);
			d[2] |= uint32_t(r);
			d[2] |= uint32_t(g) << 16;
			d[3] |= uint32_t(b);
			d[3] |= uint32_t(a) << 16;
		}
		else
		{
			d[2] = uint32_t(rnd());
			d[3] = uint32_t(rnd());
		}
	}

	auto readback_reference = decode_gpu(*cmd, layout, readback_format);
	auto readback_decoded = decode_compute(*cmd, layout);
	if (!readback_decoded)
	{
		device.submit_discard(cmd);
		return false;
	}

	Fence fence;
	device.submit(cmd, &fence);
	fence->wait();

	if (readback_format == VK_FORMAT_R16G16B16A16_SFLOAT)
		return compare_rgba16f(device, *readback_reference, *readback_decoded, width, height);
	else if (readback_format == VK_FORMAT_R8G8B8A8_SRGB)
		return compare_rgba8(device, *readback_reference, *readback_decoded, width, height, 0);
	else
		return false;
}

static bool test_astc_block_mode(Device &device, VkFormat format, VkFormat readback_format)
{
	auto cmd = device.request_command_buffer();
	std::mt19937 rnd(1338);
	SceneFormats::MemoryMappedTexture tex;
	unsigned width = 8092;
	unsigned height = 8092;

	unsigned block_width, block_height;
	Vulkan::TextureFormatLayout::format_block_dim(format, block_width, block_height);

	unsigned blocks_x = (width + block_width - 1) / block_width;
	unsigned blocks_y = (height + block_height - 1) / block_height;
	unsigned num_blocks = blocks_x * blocks_y;
	tex.set_2d(format, width, height);
	if (!tex.map_write_scratch())
		return false;

	auto &layout = tex.get_layout();
	auto *d = static_cast<uint32_t *>(layout.data_opaque(0, 0, 0, 0));

	// Expose all possible weight encoding formats.
	for (unsigned i = 0; i < num_blocks; i++, d += 4)
	{
		d[0] = 0;
		d[1] = 0;
		d[2] = 0;
		d[3] = 0;

		// Exhaustively test all possible block modes, randomize all other data.
		d[0] = i & 0x3ff;
		d[0] |= uint32_t(rnd()) << 11;
		d[1] = uint32_t(rnd());
		d[2] = uint32_t(rnd());
		d[3] = uint32_t(rnd());
	}

	auto readback_reference = decode_gpu(*cmd, layout, readback_format);
	auto readback_decoded = decode_compute(*cmd, layout);
	if (!readback_decoded)
	{
		device.submit_discard(cmd);
		return false;
	}

	Fence fence;
	device.submit(cmd, &fence);
	fence->wait();

	if (readback_format == VK_FORMAT_R16G16B16A16_SFLOAT)
		return compare_rgba16f(device, *readback_reference, *readback_decoded, width, height);
	else if (readback_format == VK_FORMAT_R8G8B8A8_SRGB)
		return compare_rgba8(device, *readback_reference, *readback_decoded, width, height, 0);
	else
		return false;
}

static bool test_astc(Device &device)
{
	static const VkFormat unorm_formats[] = {
		VK_FORMAT_ASTC_4x4_UNORM_BLOCK,
		VK_FORMAT_ASTC_5x4_UNORM_BLOCK,
		VK_FORMAT_ASTC_5x5_UNORM_BLOCK,
		VK_FORMAT_ASTC_6x5_UNORM_BLOCK,
		VK_FORMAT_ASTC_6x6_UNORM_BLOCK,
		VK_FORMAT_ASTC_8x5_UNORM_BLOCK,
		VK_FORMAT_ASTC_8x6_UNORM_BLOCK,
		VK_FORMAT_ASTC_8x8_UNORM_BLOCK,
		VK_FORMAT_ASTC_10x5_UNORM_BLOCK,
		VK_FORMAT_ASTC_10x6_UNORM_BLOCK,
		VK_FORMAT_ASTC_10x8_UNORM_BLOCK,
		VK_FORMAT_ASTC_10x10_UNORM_BLOCK,
		VK_FORMAT_ASTC_12x10_UNORM_BLOCK,
		VK_FORMAT_ASTC_12x12_UNORM_BLOCK,
	};

	static const VkFormat srgb_formats[] = {
		VK_FORMAT_ASTC_4x4_SRGB_BLOCK,
		VK_FORMAT_ASTC_5x4_SRGB_BLOCK,
		VK_FORMAT_ASTC_5x5_SRGB_BLOCK,
		VK_FORMAT_ASTC_6x5_SRGB_BLOCK,
		VK_FORMAT_ASTC_6x6_SRGB_BLOCK,
		VK_FORMAT_ASTC_8x5_SRGB_BLOCK,
		VK_FORMAT_ASTC_8x6_SRGB_BLOCK,
		VK_FORMAT_ASTC_8x8_SRGB_BLOCK,
		VK_FORMAT_ASTC_10x5_SRGB_BLOCK,
		VK_FORMAT_ASTC_10x6_SRGB_BLOCK,
		VK_FORMAT_ASTC_10x8_SRGB_BLOCK,
		VK_FORMAT_ASTC_10x10_SRGB_BLOCK,
		VK_FORMAT_ASTC_12x10_SRGB_BLOCK,
		VK_FORMAT_ASTC_12x12_SRGB_BLOCK,
	};

	const auto test_formats = [&](bool (*func)(Device &, VkFormat, VkFormat)) -> bool {
		for (auto format : srgb_formats)
		{
			uint32_t w, h;
			Vulkan::TextureFormatLayout::format_block_dim(format, w, h);
			LOGI(" ... %u x %u sRGB\n", w, h);
			if (!func(device, format, VK_FORMAT_R8G8B8A8_SRGB))
			{
				LOGE("    ... FAILED!\n");
				return false;
			}
			else
				LOGI("    ... Success!\n");

			device.wait_idle();
		}

		for (auto format : unorm_formats)
		{
			uint32_t w, h;
			Vulkan::TextureFormatLayout::format_block_dim(format, w, h);
			LOGI(" ... %u x %u UNORM\n", w, h);
			if (!func(device, format, VK_FORMAT_R16G16B16A16_SFLOAT))
			{
				LOGE("    ... FAILED!\n");
				return false;
			}
			else
				LOGI("    ... Success!\n");

			device.wait_idle();
		}
		return true;
	};

	const auto test = [&](bool (*func)(Device &, VkFormat, VkFormat)) -> bool {
		if (!func(device, VK_FORMAT_ASTC_4x4_UNORM_BLOCK, VK_FORMAT_R16G16B16A16_SFLOAT))
		{
			LOGE("    ... FAILED!\n");
			return false;
		}
		else
			LOGI("    ... Success!\n");

		device.wait_idle();
		return true;
	};

	LOGI("Testing ASTC weight encoding and interpolation ...\n");
	if (!test_formats(test_astc_weights<false>))
		return false;
	LOGI("Testing ASTC dual plane encoding ...\n");
	if (!test_formats(test_astc_weights<true>))
		return false;
	LOGI("Testing ASTC endpoint formats ...\n");
	if (!test(test_astc_endpoint_formats))
		return false;
	LOGI("Testing ASTC multi-partition ...\n");
	if (!test(test_astc_partitions<false>))
		return false;
	LOGI("Testing ASTC multi-partition with dual-plane ...\n");
	if (!test(test_astc_partitions<true>))
		return false;
	LOGI("Testing ASTC multi-partition complex encoding ...\n");
	if (!test(test_astc_partitions_complex<false>))
		return false;
	LOGI("Testing ASTC multi-partition with dual-plane encoding ...\n");
	if (!test(test_astc_partitions_complex<true>))
		return false;
	LOGI("Testing ASTC void extent.\n");
	if (!test(test_astc_void_extent))
		return false;
	LOGI("Testing ASTC block mode.\n");
	if (!test_formats(test_astc_block_mode))
		return false;

	return true;
}

static bool test_bc6(Device &device, VkFormat format)
{
	auto cmd = device.request_command_buffer();
	std::mt19937 rnd(1337);

	SceneFormats::MemoryMappedTexture tex;
	unsigned width = 4096;
	unsigned height = 4096;
	unsigned blocks_x = (width + 3) / 4;
	unsigned blocks_y = (height + 3) / 4;
	unsigned num_words = blocks_x * blocks_y *
	                     (TextureFormatLayout::format_block_size(format, VK_IMAGE_ASPECT_COLOR_BIT) / 4);
	tex.set_2d(format, width, height);
	if (!tex.map_write_scratch())
		return false;

	auto &layout = tex.get_layout();
	auto *d = static_cast<uint32_t *>(layout.data_opaque(0, 0, 0, 0));
	for (unsigned i = 0; i < num_words; i++)
	{
		uint32_t w = rnd();
		d[i] = w;
	}

	auto readback_reference = decode_gpu(*cmd, layout, VK_FORMAT_R16G16B16A16_SFLOAT);
	auto readback_decoded = decode_compute(*cmd, layout);
	if (!readback_decoded)
	{
		device.submit_discard(cmd);
		return false;
	}

	Fence fence;
	device.submit(cmd, &fence);
	fence->wait();

	return compare_rgba16f(device, *readback_reference, *readback_decoded, width, height);
}

static bool test_bc7(Device &device, VkFormat format, VkFormat readback_format)
{
	auto cmd = device.request_command_buffer();
	std::mt19937 rnd(1337);

	SceneFormats::MemoryMappedTexture tex;
	unsigned width = 4096;
	unsigned height = 4096;
	unsigned blocks_x = (width + 3) / 4;
	unsigned blocks_y = (height + 3) / 4;
	unsigned num_words = blocks_x * blocks_y *
	                     (TextureFormatLayout::format_block_size(format, VK_IMAGE_ASPECT_COLOR_BIT) / 4);
	tex.set_2d(format, width, height);
	if (!tex.map_write_scratch())
		return false;

	auto &layout = tex.get_layout();
	auto *d = static_cast<uint32_t *>(layout.data_opaque(0, 0, 0, 0));
	for (unsigned i = 0; i < num_words; i++)
	{
		uint32_t w = rnd();
		d[i] = w;
	}

	auto readback_reference = decode_gpu(*cmd, layout, readback_format);
	auto readback_decoded = decode_compute(*cmd, layout);
	if (!readback_decoded)
	{
		device.submit_discard(cmd);
		return false;
	}

	Fence fence;
	device.submit(cmd, &fence);
	fence->wait();

	return compare_rgba8(device, *readback_reference, *readback_decoded, width, height, 0);
}

static bool test_eac(Device &device, VkFormat format, VkFormat readback_format)
{
	auto cmd = device.request_command_buffer();
	std::mt19937 rnd(1337);

	SceneFormats::MemoryMappedTexture tex;
	unsigned width = 2048;
	unsigned height = 2048;
	unsigned blocks_x = (width + 3) / 4;
	unsigned blocks_y = (height + 3) / 4;
	unsigned num_words = blocks_x * blocks_y *
	                     (TextureFormatLayout::format_block_size(format, VK_IMAGE_ASPECT_COLOR_BIT) / 4);
	tex.set_2d(format, width, height);
	if (!tex.map_write_scratch())
		return false;

	auto &layout = tex.get_layout();
	auto *d = static_cast<uint32_t *>(layout.data_opaque(0, 0, 0, 0));
	for (unsigned i = 0; i < num_words; i++)
	{
		uint32_t w = rnd();
		d[i] = w;
	}

	auto readback_reference = decode_gpu(*cmd, layout, readback_format);
	auto readback_decoded = decode_compute(*cmd, layout);
	if (!readback_decoded)
	{
		device.submit_discard(cmd);
		return false;
	}

	Fence fence;
	device.submit(cmd, &fence);
	fence->wait();

	if (readback_format == VK_FORMAT_R16G16_SFLOAT)
		return compare_rg16f(device, *readback_reference, *readback_decoded, width, height);
	else if (readback_format == VK_FORMAT_R16_SFLOAT)
		return compare_r16f(device, *readback_reference, *readback_decoded, width, height);
	else
		return false;
}

static bool test_etc2(Device &device, VkFormat format, VkFormat readback_format)
{
	auto cmd = device.request_command_buffer();
	std::mt19937 rnd(1337);

	SceneFormats::MemoryMappedTexture tex;
	unsigned width = 2048;
	unsigned height = 2048;
	unsigned blocks_x = (width + 3) / 4;
	unsigned blocks_y = (height + 3) / 4;
	unsigned num_words = blocks_x * blocks_y *
	                     (TextureFormatLayout::format_block_size(format, VK_IMAGE_ASPECT_COLOR_BIT) / 4);
	tex.set_2d(format, width, height);
	if (!tex.map_write_scratch())
		return false;

	auto &layout = tex.get_layout();
	auto *d = static_cast<uint32_t *>(layout.data_opaque(0, 0, 0, 0));
	for (unsigned i = 0; i < num_words; i++)
	{
		uint32_t w = rnd();
		d[i] = w;
	}

	auto readback_reference = decode_gpu(*cmd, layout, readback_format);
	auto readback_decoded = decode_compute(*cmd, layout);
	if (!readback_decoded)
	{
		device.submit_discard(cmd);
		return false;
	}

	Fence fence;
	device.submit(cmd, &fence);
	fence->wait();

	return compare_rgba8(device, *readback_reference, *readback_decoded, width, height, 0);
}

static bool test_rgtc(Device &device, VkFormat format, VkFormat readback_format)
{
	auto cmd = device.request_command_buffer();
	std::mt19937 rnd(1337);

	SceneFormats::MemoryMappedTexture tex;
	unsigned width = 2048;
	unsigned height = 2048;
	unsigned blocks_x = (width + 3) / 4;
	unsigned blocks_y = (height + 3) / 4;
	unsigned num_words = blocks_x * blocks_y *
	                     (TextureFormatLayout::format_block_size(format, VK_IMAGE_ASPECT_COLOR_BIT) / 4);
	tex.set_2d(format, width, height);
	if (!tex.map_write_scratch())
		return false;

	auto &layout = tex.get_layout();
	auto *d = static_cast<uint32_t *>(layout.data_opaque(0, 0, 0, 0));
	for (unsigned i = 0; i < num_words; i++)
		d[i] = uint32_t(rnd());

	auto readback_reference = decode_gpu(*cmd, layout, readback_format);
	auto readback_decoded = decode_compute(*cmd, layout);
	if (!readback_decoded)
	{
		device.submit_discard(cmd);
		return false;
	}

	Fence fence;
	device.submit(cmd, &fence);
	fence->wait();

	if (readback_format == VK_FORMAT_R8_UNORM)
		return compare_r8(device, *readback_reference, *readback_decoded, width, height, 1);
	else if (readback_format == VK_FORMAT_R8G8_UNORM)
		return compare_rg8(device, *readback_reference, *readback_decoded, width, height, 1);
	else
		return false;
}

static bool test_s3tc(Device &device, VkFormat format, VkFormat readback_format)
{
	auto cmd = device.request_command_buffer();
	std::mt19937 rnd(1337);

	SceneFormats::MemoryMappedTexture tex;
	unsigned width = 2048;
	unsigned height = 2048;
	unsigned blocks_x = (width + 3) / 4;
	unsigned blocks_y = (height + 3) / 4;
	unsigned num_words = blocks_x * blocks_y *
	                     (TextureFormatLayout::format_block_size(format, VK_IMAGE_ASPECT_COLOR_BIT) / 4);
	tex.set_2d(format, width, height);
	if (!tex.map_write_scratch())
		return false;

	auto &layout = tex.get_layout();
	auto *d = static_cast<uint32_t *>(layout.data_opaque(0, 0, 0, 0));
	for (unsigned i = 0; i < num_words; i++)
		d[i] = uint32_t(rnd());

	auto readback_reference = decode_gpu(*cmd, layout, readback_format);
	auto readback_decoded = decode_compute(*cmd, layout);
	if (!readback_decoded)
	{
		device.submit_discard(cmd);
		return false;
	}

	Fence fence;
	device.submit(cmd, &fence);
	fence->wait();

	return compare_rgba8(device, *readback_reference, *readback_decoded, width, height, 1);
}

static bool test_s3tc(Device &device)
{
	LOGI("Testing BC1 RGBA UNORM.\n");
	if (!test_s3tc(device, VK_FORMAT_BC1_RGBA_UNORM_BLOCK, VK_FORMAT_R8G8B8A8_UNORM))
		return false;
	device.wait_idle();
	LOGI("Testing BC1 RGB UNORM.\n");
	if (!test_s3tc(device, VK_FORMAT_BC1_RGB_UNORM_BLOCK, VK_FORMAT_R8G8B8A8_UNORM))
		return false;
	device.wait_idle();
	LOGI("Testing BC1 sRGBA UNORM.\n");
	if (!test_s3tc(device, VK_FORMAT_BC1_RGBA_SRGB_BLOCK, VK_FORMAT_R8G8B8A8_SRGB))
		return false;
	device.wait_idle();
	LOGI("Testing BC1 sRGB UNORM.\n");
	if (!test_s3tc(device, VK_FORMAT_BC1_RGB_SRGB_BLOCK, VK_FORMAT_R8G8B8A8_SRGB))
		return false;
	device.wait_idle();
	LOGI("Testing BC2 UNORM.\n");
	if (!test_s3tc(device, VK_FORMAT_BC2_UNORM_BLOCK, VK_FORMAT_R8G8B8A8_UNORM))
		return false;
	device.wait_idle();
	LOGI("Testing BC2 sRGB.\n");
	if (!test_s3tc(device, VK_FORMAT_BC2_SRGB_BLOCK, VK_FORMAT_R8G8B8A8_SRGB))
		return false;
	device.wait_idle();
	LOGI("Testing BC3 UNORM.\n");
	if (!test_s3tc(device, VK_FORMAT_BC3_UNORM_BLOCK, VK_FORMAT_R8G8B8A8_UNORM))
		return false;
	device.wait_idle();
	LOGI("Testing BC3 sRGB.\n");
	if (!test_s3tc(device, VK_FORMAT_BC3_SRGB_BLOCK, VK_FORMAT_R8G8B8A8_SRGB))
		return false;
	device.wait_idle();
	return true;
}

static bool test_rgtc(Device &device)
{
	LOGI("Testing BC4 UNORM.\n");
	if (!test_rgtc(device, VK_FORMAT_BC4_UNORM_BLOCK, VK_FORMAT_R8_UNORM))
		return false;
	device.wait_idle();
	LOGI("Testing BC5 UNORM.\n");
	if (!test_rgtc(device, VK_FORMAT_BC5_UNORM_BLOCK, VK_FORMAT_R8G8_UNORM))
		return false;
	device.wait_idle();
	return true;
}

static bool test_etc2(Device &device)
{
	LOGI("Testing ETC2 RGB UNORM.\n");
	if (!test_etc2(device, VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK, VK_FORMAT_R8G8B8A8_UNORM))
		return false;
	device.wait_idle();
	LOGI("Testing ETC2 RGB sRGB.\n");
	if (!test_etc2(device, VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK, VK_FORMAT_R8G8B8A8_SRGB))
		return false;
	device.wait_idle();
	LOGI("Testing ETC2 RGB8A1 UNORM.\n");
	if (!test_etc2(device, VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK, VK_FORMAT_R8G8B8A8_UNORM))
		return false;
	device.wait_idle();
	LOGI("Testing ETC2 RGB8A1 sRGB.\n");
	if (!test_etc2(device, VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK, VK_FORMAT_R8G8B8A8_SRGB))
		return false;
	device.wait_idle();
	LOGI("Testing ETC2 RGB8A8 UNORM.\n");
	if (!test_etc2(device, VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK, VK_FORMAT_R8G8B8A8_UNORM))
		return false;
	device.wait_idle();
	LOGI("Testing ETC2 RGB8A8 sRGB.\n");
	if (!test_etc2(device, VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK, VK_FORMAT_R8G8B8A8_SRGB))
		return false;
	device.wait_idle();
	return true;
}

static bool test_eac(Device &device)
{
	LOGI("Testing EAC R11 UNORM.\n");
	if (!test_eac(device, VK_FORMAT_EAC_R11_UNORM_BLOCK, VK_FORMAT_R16_SFLOAT))
		return false;
	device.wait_idle();
	LOGI("Testing EAC R11G11 UNORM.\n");
	if (!test_eac(device, VK_FORMAT_EAC_R11G11_UNORM_BLOCK, VK_FORMAT_R16G16_SFLOAT))
		return false;
	device.wait_idle();
	return true;
}

static bool test_bc7(Device &device)
{
	LOGI("Testing BC7 sRGB.\n");
	if (!test_bc7(device, VK_FORMAT_BC7_SRGB_BLOCK, VK_FORMAT_R8G8B8A8_SRGB))
		return false;
	device.wait_idle();
	LOGI("Testing BC7 UNORM.\n");
	if (!test_bc7(device, VK_FORMAT_BC7_UNORM_BLOCK, VK_FORMAT_R8G8B8A8_UNORM))
		return false;
	device.wait_idle();
	return true;
}

static bool test_bc6(Device &device)
{
	LOGI("Testing BC6 SFLOAT.\n");
	if (!test_bc6(device, VK_FORMAT_BC6H_SFLOAT_BLOCK))
		return false;
	device.wait_idle();
	LOGI("Testing BC6 UFLOAT.\n");
	if (!test_bc6(device, VK_FORMAT_BC6H_UFLOAT_BLOCK))
		return false;
	device.wait_idle();
	return true;
}

int main()
{
	Global::init(Global::MANAGER_FEATURE_ALL_BITS, 1);

	if (!Context::init_loader(nullptr))
		return EXIT_FAILURE;

	Context ctx;
	ctx.set_num_thread_indices(2);
	if (!ctx.init_instance_and_device(nullptr, 0, nullptr, 0))
		return EXIT_FAILURE;

	Device device;
	device.set_context(ctx);

	if (!test_s3tc(device))
		return EXIT_FAILURE;
	if (!test_rgtc(device))
		return EXIT_FAILURE;
	if (!test_etc2(device))
		return EXIT_FAILURE;
	if (!test_eac(device))
		return EXIT_FAILURE;
	if (!test_bc7(device))
		return EXIT_FAILURE;
	if (!test_bc6(device))
		return EXIT_FAILURE;
	if (!test_astc(device))
		return EXIT_FAILURE;
}
