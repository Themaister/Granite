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
#include <random>

using namespace Granite;
using namespace Vulkan;

static bool test_s3tc(Device &device)
{
	auto cmd = device.request_command_buffer();
	std::mt19937 rnd(1337);

	SceneFormats::MemoryMappedTexture tex;
	unsigned width = 8;
	unsigned height = 8;
	unsigned blocks_x = (width + 3) / 4;
	unsigned blocks_y = (height + 3) / 4;
	unsigned num_words = blocks_x * blocks_y * 2;
	tex.set_2d(VK_FORMAT_BC1_RGBA_UNORM_BLOCK, width, height);
	if (!tex.map_write_scratch())
		return false;

	auto &layout = tex.get_layout();
	auto *d = static_cast<uint32_t *>(layout.data_opaque(0, 0, 0, 0));
	for (unsigned i = 0; i < num_words; i++)
		d[i] = uint32_t(rnd());

	auto decoded = decode_compressed_image(*cmd, layout);

	auto uploaded_info = ImageCreateInfo::immutable_image(layout);
	auto uploaded_staging = device.create_image_staging_buffer(layout);
	auto uploaded_tex = device.create_image_from_staging_buffer(uploaded_info, &uploaded_staging);

	auto rt_info = ImageCreateInfo::render_target(width, height, VK_FORMAT_R8G8B8A8_UNORM);
	rt_info.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	auto rt = device.create_image(rt_info);

	RenderPassInfo rp_info;
	rp_info.num_color_attachments = 1;
	rp_info.color_attachments[0] = &rt->get_view();
	rp_info.store_attachments = 1;
	cmd->begin_render_pass(rp_info);
	cmd->set_texture(0, 0, uploaded_tex->get_view(), StockSampler::NearestClamp);
	CommandBufferUtil::draw_fullscreen_quad(*cmd, "builtin://shaders/quad.vert", "builtin://shaders/blit.frag");
	cmd->end_render_pass();

	cmd->image_barrier(*rt, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
	                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
	cmd->image_barrier(*decoded, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
	                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);

	BufferCreateInfo readback_info;
	readback_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	readback_info.domain = BufferDomain::CachedHost;
	readback_info.size = width * height * sizeof(u8vec4);
	auto readback_reference = device.create_buffer(readback_info);
	auto readback_decoded = device.create_buffer(readback_info);

	cmd->copy_image_to_buffer(*readback_reference, *rt, 0, {}, { width, height, 1 }, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 });
	cmd->copy_image_to_buffer(*readback_decoded, *decoded, 0, {}, { width, height, 1 }, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 });
	cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
	             VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);

	Fence fence;
	device.submit(cmd, &fence);
	fence->wait();

	auto *mapped_reference = static_cast<const u8vec4 *>(device.map_host_buffer(*readback_reference, MEMORY_ACCESS_READ_BIT));
	auto *mapped_decoded = static_cast<const u8vec4 *>(device.map_host_buffer(*readback_decoded, MEMORY_ACCESS_READ_BIT));

	bool has_error = false;
	for (unsigned y = 0; y < height; y++)
	{
		for (unsigned x = 0; x < width; x++)
		{
			auto &ref = mapped_reference[y * width + x];
			auto &dec = mapped_decoded[y * width + x];

			if (ref.x != dec.x || ref.y != dec.y || ref.z != dec.z || ref.w != dec.w)
			{
				LOGE("(%u, %u): Reference (%u, %u, %u, %u) != (%u, %u, %u, %u).\n",
				     x, y,
				     ref.x, ref.y, ref.z, ref.w,
				     dec.x, dec.y, dec.z, dec.w);
				has_error = true;
			}
		}
	}

	return !has_error;
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
}