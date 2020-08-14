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
			auto &ref = mapped_reference[y * width + x];
			auto &dec = mapped_decoded[y * width + x];

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

static BufferHandle decode_gpu(CommandBuffer &cmd, const TextureFormatLayout &layout, VkFormat format)
{
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

#if 0
struct DebugIface : DebugChannelInterface
{
	void message(const std::string &tag, uint32_t code, uint32_t x, uint32_t y, uint32_t z,
	             uint32_t word_count, const Word *words) override
	{
		if (x == 3 && y == 0)
			LOGI("(X = %d, Y = %d), line: %d = (%d, %d, %d).\n", x, y, words[0].s32, words[1].s32, words[2].s32, words[3].s32);
	}
};
static DebugIface iface;
#endif

static bool test_astc(Device &device, VkFormat format, VkFormat readback_format)
{
	auto cmd = device.request_command_buffer();
	std::mt19937 rnd(1337);

	SceneFormats::MemoryMappedTexture tex;
	unsigned width = 4;
	unsigned height = 4;
	unsigned blocks_x = (width + 3) / 4;
	unsigned blocks_y = (height + 3) / 4;
	unsigned num_words = blocks_x * blocks_y *
	                     (TextureFormatLayout::format_block_size(format, VK_IMAGE_ASPECT_COLOR_BIT) / 4);
	tex.set_2d(format, width, height);
	if (!tex.map_write_scratch())
		return false;

	auto &layout = tex.get_layout();
	auto *d = static_cast<uint32_t *>(layout.data_opaque(0, 0, 0, 0));

	d[0] = 0;
	d[1] = 0;
	d[2] = 0;
	d[3] = 0;

	// 4x4 weight grid.
	d[0] |= 0 << 7;
	d[0] |= 2 << 5;

	// 3 bit weights.
	d[0] |= 1 << 0;
	d[0] |= 1 << 1;
	d[0] |= 1 << 4;

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

static bool test_bc6(Device &device, VkFormat format)
{
	auto cmd = device.request_command_buffer();
	std::mt19937 rnd(1337);

	//cmd->begin_debug_channel(&iface, "BC6", 16 * 1024 * 1024);

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
	if (!test_s3tc(device, VK_FORMAT_BC1_RGBA_UNORM_BLOCK, VK_FORMAT_R8G8B8A8_UNORM))
		return false;
	device.wait_idle();
	if (!test_s3tc(device, VK_FORMAT_BC1_RGB_UNORM_BLOCK, VK_FORMAT_R8G8B8A8_UNORM))
		return false;
	device.wait_idle();
	if (!test_s3tc(device, VK_FORMAT_BC1_RGBA_SRGB_BLOCK, VK_FORMAT_R8G8B8A8_SRGB))
		return false;
	device.wait_idle();
	if (!test_s3tc(device, VK_FORMAT_BC1_RGB_SRGB_BLOCK, VK_FORMAT_R8G8B8A8_SRGB))
		return false;
	device.wait_idle();
	if (!test_s3tc(device, VK_FORMAT_BC2_UNORM_BLOCK, VK_FORMAT_R8G8B8A8_UNORM))
		return false;
	device.wait_idle();
	if (!test_s3tc(device, VK_FORMAT_BC2_SRGB_BLOCK, VK_FORMAT_R8G8B8A8_SRGB))
		return false;
	device.wait_idle();
	if (!test_s3tc(device, VK_FORMAT_BC3_UNORM_BLOCK, VK_FORMAT_R8G8B8A8_UNORM))
		return false;
	device.wait_idle();
	if (!test_s3tc(device, VK_FORMAT_BC3_SRGB_BLOCK, VK_FORMAT_R8G8B8A8_SRGB))
		return false;
	device.wait_idle();
	return true;
}

static bool test_rgtc(Device &device)
{
	if (!test_rgtc(device, VK_FORMAT_BC4_UNORM_BLOCK, VK_FORMAT_R8_UNORM))
		return false;
	device.wait_idle();
	if (!test_rgtc(device, VK_FORMAT_BC5_UNORM_BLOCK, VK_FORMAT_R8G8_UNORM))
		return false;
	device.wait_idle();
	return true;
}

static bool test_etc2(Device &device)
{
	if (!test_etc2(device, VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK, VK_FORMAT_R8G8B8A8_UNORM))
		return false;
	device.wait_idle();
	if (!test_etc2(device, VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK, VK_FORMAT_R8G8B8A8_SRGB))
		return false;
	device.wait_idle();
	if (!test_etc2(device, VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK, VK_FORMAT_R8G8B8A8_UNORM))
		return false;
	device.wait_idle();
	if (!test_etc2(device, VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK, VK_FORMAT_R8G8B8A8_SRGB))
		return false;
	device.wait_idle();
	if (!test_etc2(device, VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK, VK_FORMAT_R8G8B8A8_UNORM))
		return false;
	device.wait_idle();
	if (!test_etc2(device, VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK, VK_FORMAT_R8G8B8A8_SRGB))
		return false;
	device.wait_idle();
	return true;
}

static bool test_eac(Device &device)
{
	if (!test_eac(device, VK_FORMAT_EAC_R11_UNORM_BLOCK, VK_FORMAT_R16_SFLOAT))
		return false;
	device.wait_idle();
	if (!test_eac(device, VK_FORMAT_EAC_R11G11_UNORM_BLOCK, VK_FORMAT_R16G16_SFLOAT))
		return false;
	device.wait_idle();
	return true;
}

static bool test_bc7(Device &device)
{
	if (!test_bc7(device, VK_FORMAT_BC7_SRGB_BLOCK, VK_FORMAT_R8G8B8A8_SRGB))
		return false;
	device.wait_idle();
	if (!test_bc7(device, VK_FORMAT_BC7_UNORM_BLOCK, VK_FORMAT_R8G8B8A8_UNORM))
		return false;
	device.wait_idle();
	return true;
}

static bool test_bc6(Device &device)
{
	if (!test_bc6(device, VK_FORMAT_BC6H_SFLOAT_BLOCK))
		return false;
	device.wait_idle();
	if (!test_bc6(device, VK_FORMAT_BC6H_UFLOAT_BLOCK))
		return false;
	device.wait_idle();
	return true;
}

static bool test_astc(Device &device)
{
	if (!test_astc(device, VK_FORMAT_ASTC_4x4_UNORM_BLOCK, VK_FORMAT_R16G16B16A16_SFLOAT))
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

#if 0
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
#endif
	if (!test_astc(device))
		return EXIT_FAILURE;
}