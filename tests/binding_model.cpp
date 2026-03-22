/* Copyright (c) 2017-2026 Hans-Kristian Arntzen
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

#include "application.hpp"
#include "global_managers_init.hpp"
#include "os_filesystem.hpp"
#include "device.hpp"
#include "thread_group.hpp"
#include "context.hpp"

using namespace Granite;
using namespace Vulkan;

static BufferHandle create_buffer(Device &device, VkDeviceSize size, const void *data)
{
	BufferCreateInfo buf;
	buf.size = size;
	buf.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
	            VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT |
	            VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;
	buf.domain = BufferDomain::CachedHost;
	return device.create_buffer(buf, data);
}

static void assert_that(bool check, const char *str, int line)
{
	if (!check)
	{
		LOGE("Failed: %s on line %d\n", str, line);
		exit(EXIT_FAILURE);
	}
}

#define ASSERT_THAT(cond) assert_that(cond, #cond, __LINE__)

static void test_inline_bda(Device &device, bool many)
{
	auto cmd = device.request_command_buffer();
	cmd->set_program("assets://shaders/binding_model/inline_bda.comp", {{"MANY", many ? 1 : 0 }});

	const uint32_t data[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	BufferHandle buffers[8];

	for (int i = 0; i < 8; i++)
		buffers[i] = create_buffer(device, sizeof(uint32_t), &data[i]);

	for (int i = 0; i < 4; i++)
	{
		cmd->set_storage_buffer(i, 0, *buffers[2 * i + 0]);
		cmd->set_uniform_buffer(i, 1, *buffers[2 * i + 1]);
		cmd->set_storage_buffer(i, 2, *buffers[2 * i + 0]);
		// This goes past the threshold for inline hoist.
		cmd->set_uniform_buffer(i, 3, *buffers[2 * i + 1]);
	}

	cmd->dispatch(1, 1, 1);
	cmd->barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
	             VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);

	Fence fence;
	device.submit(cmd, &fence);
	fence->wait();
	device.next_frame_context();

	for (int i = 0; i < 8; i++)
	{
		auto *ptr = static_cast<uint32_t *>(device.map_host_buffer(*buffers[i], MEMORY_ACCESS_READ_BIT));

		if (i & 1)
			ASSERT_THAT(ptr[0] == data[i]);
		else
			ASSERT_THAT(ptr[0] == data[i + 0] + (many ? 2 : 1) * data[i + 1]);

		device.unmap_host_buffer(*buffers[i], MEMORY_ACCESS_READ_BIT);
	}
}

static void test_buffer_descriptor(Device &device)
{
	// One SSBO uses OpArrayLength, which requires actual descriptors.
	auto cmd = device.request_command_buffer();
	cmd->set_program("assets://shaders/binding_model/buffer_descriptor.comp");

	const uint32_t data[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	BufferHandle buffers[4];

	for (int i = 0; i < 4; i++)
		buffers[i] = create_buffer(device, 2 * sizeof(uint32_t), &data[2 * i]);

	cmd->set_storage_buffer(0, 0, *buffers[0]);
	cmd->set_storage_buffer(0, 1, *buffers[1]);
	cmd->set_uniform_buffer(0, 2, *buffers[2]);
	cmd->set_uniform_buffer(1, 0, *buffers[3]);

	cmd->dispatch(1, 1, 1);
	cmd->barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
				 VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);

	Fence fence;
	device.submit(cmd, &fence);
	fence->wait();
	device.next_frame_context();

	auto *ptr = static_cast<uint32_t *>(device.map_host_buffer(*buffers[0], MEMORY_ACCESS_READ_BIT));
	ASSERT_THAT(ptr[0] == data[0] + data[2] + data[3] + 2 + data[4] + data[6]);
	device.unmap_host_buffer(*buffers[0], MEMORY_ACCESS_READ_BIT);
}

static void test_buffer_view(Device &device, bool many, bool arrayed)
{
	auto cmd = device.request_command_buffer();
	cmd->set_program("assets://shaders/binding_model/texel_buffer.comp",
	                 {{"MANY", many ? 1 : 0}, {"ARRAYED", arrayed ? 1 : 0}});

	BufferViewHandle views[4];
	BufferHandle buffers[4];

	for (int i = 0; i < 4; i++)
	{
		BufferViewCreateInfo info = {};

		uint32_t data[8];
		for (int j = 0; j < 8; j++)
			data[j] = i * 100 + j;

		buffers[i] = create_buffer(device, sizeof(data), data);
		info.buffer = buffers[i].get();
		info.format = VK_FORMAT_R32G32_UINT;
		info.offset = 0;
		info.range = 8 * sizeof(uint32_t);
		views[i] = device.create_buffer_view(info);
	}

	for (int set = 0; set < 4; set++)
	{
		cmd->set_storage_buffer_view(set, 0, *views[set]);
		for (int i = 1; i < 16; i++)
			cmd->set_buffer_view(set, i, *views[i % 4]);
	}

	cmd->dispatch(1, 1, 1);
	cmd->barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
				 VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);

	Fence fence;
	device.submit(cmd, &fence);
	fence->wait();
	device.next_frame_context();

	uint32_t sum_x = 4872;
	uint32_t sum_y = 4900;

	uint32_t reference[4][8] = {};
	for (int y = 0; y < 4; y++)
		for (int x = 0; x < 8; x++)
			reference[y][x] = y * 100 + x;

	for (int y = 0; y < 4; y++)
	{
		reference[y][2 * y + 0] = sum_x + y + (arrayed ? 1224 : 0);
		reference[y][2 * y + 1] = sum_y + y + (many ? 1 : 0) + (arrayed ? 1232 : 0);
	}

	for (int i = 0; i < 4; i++)
	{
		auto *ptr = static_cast<uint32_t *>(device.map_host_buffer(*buffers[i], MEMORY_ACCESS_READ_BIT));
		for (int j = 0; j < 8; j++)
			ASSERT_THAT(reference[i][j] == ptr[j]);
		device.unmap_host_buffer(*buffers[i], MEMORY_ACCESS_READ_BIT);
	}
}

static void test_inline_texture(Device &device, bool combined, bool arrayed)
{
	auto cmd = device.request_command_buffer();
	cmd->set_program("assets://shaders/binding_model/textures.comp",
	                 {{"COMBINED", combined ? 1 : 0}, {"ARRAYED", arrayed ? 1 : 0}});

	auto info = ImageCreateInfo::immutable_2d_image(7, 7, VK_FORMAT_R8G8B8A8_UNORM);
	uint32_t data[7][7];

	for (int y = 0; y < 7; y++)
		for (int x = 0; x < 7; x++)
			data[y][x] = ((x ^ y) & 1) ? 0x20202020 : 0;

	const ImageInitialData init = { data, 0, 0 };
	auto tex0 = device.create_image(info, &init);

	for (int y = 0; y < 7; y++)
		for (int x = 0; x < 7; x++)
			data[y][x] = ((x ^ y) & 1) ? 0 : 0x40404040;

	auto tex1 = device.create_image(info, &init);

	info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	info.layout = ImageLayout::General;
	info.initial_layout = VK_IMAGE_LAYOUT_GENERAL;
	info.width = 8;
	info.height = 8;
	auto storage_tex = device.create_image(info);

	const float inv_res[] = { 1.0f / 7.0f, 1.0f / 7.0f };
	cmd->push_constants(&inv_res, 0, sizeof(inv_res));

	cmd->set_storage_texture(0, 0, storage_tex->get_view());
	if (arrayed)
	{
		if (combined)
		{
			cmd->set_texture(0, 1, tex0->get_view(), StockSampler::NearestClamp);
			cmd->set_texture(0, 2, tex1->get_view(), StockSampler::NearestWrap);
		}
		else
		{
			cmd->set_sampler(0, 1, StockSampler::NearestClamp);
			cmd->set_sampler(0, 2, StockSampler::NearestWrap);
			cmd->set_texture(0, 3, tex0->get_view());
			cmd->set_texture(0, 4, tex1->get_view());
		}
	}
	else
	{
		cmd->set_sampler(0, 1, StockSampler::NearestClamp);
		cmd->set_texture(0, 2, tex0->get_view(), StockSampler::NearestClamp);
		cmd->set_sampler(0, 3, StockSampler::NearestWrap);
		cmd->set_texture(0, 4, tex1->get_view(), StockSampler::NearestWrap);
	}
	cmd->dispatch(1, 1, 1);

	cmd->barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
	             VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);

	BufferCreateInfo bufinfo = {};
	bufinfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	bufinfo.domain = BufferDomain::CachedHost;
	bufinfo.size = 8 * 8 * sizeof(uint32_t);
	auto buf = device.create_buffer(bufinfo);

	cmd->copy_image_to_buffer(*buf, *storage_tex, 0, {}, {8, 8, 1},
	                          0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1});
	cmd->barrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
	             VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);

	Fence fence;
	device.submit(cmd, &fence);
	fence->wait();
	device.next_frame_context();

	auto *ptr = static_cast<const uint32_t *>(device.map_host_buffer(*buf, MEMORY_ACCESS_READ_BIT));
	for (int y = 0; y < 8; y++)
	{
		for (int x = 0; x < 8; x++)
		{
			uint32_t expected;
			if (x < 7 && y < 7)
				expected = ((x ^ y) & 1) ? 0x20202020 : 0x40404040;
			else if (y < 7)
				expected = ((6 ^ y) & 1) ? 0x20202020 : 0x40404040;
			else if (x < 7)
				expected = ((x ^ 6) & 1) ? 0x20202020 : 0x40404040;
			else
				expected = 0x40404040;

			ASSERT_THAT(expected == ptr[y * 8 + x]);
		}
	}
	device.unmap_host_buffer(*buf, MEMORY_ACCESS_READ_BIT);
}

static int main_inner()
{
	if (!Context::init_loader(nullptr))
		return 1;

	Context ctx;

	Context::SystemHandles handles;
	handles.filesystem = GRANITE_FILESYSTEM();
	handles.thread_group = GRANITE_THREAD_GROUP();
	ctx.set_system_handles(handles);

	if (!ctx.init_instance_and_device(nullptr, 0, nullptr, 0,
		CONTEXT_CREATION_ENABLE_DESCRIPTOR_HEAP_BIT))
		return 1;

	Device dev;
	dev.set_context(ctx);

#if 0
	// PUSH_ADDRESS
	test_inline_bda(dev, false);
	// INDIRECT_ADDRESS
	test_inline_bda(dev, true);
	// HEAP_PUSH_INDEX
	test_buffer_descriptor(dev);
#endif

	// Inline push index
	test_buffer_view(dev, false, false);
	// Heap slice for set 0 due to pressure
	test_buffer_view(dev, true, false);
	// Heap slice for set 1 due to arrays
	test_buffer_view(dev, false, true);
	// Heap slice for set 0 and 1
	test_buffer_view(dev, true, true);

#if 0
	test_inline_texture(dev, false, false);
	test_inline_texture(dev, true, false);
	test_inline_texture(dev, false, true);
	test_inline_texture(dev, true, true);
#endif

	return 0;
}

int main()
{
	Global::init();
	Filesystem::setup_default_filesystem(GRANITE_FILESYSTEM(), ASSET_DIRECTORY);
	int ret = main_inner();
	Global::deinit();
	return ret;
}
