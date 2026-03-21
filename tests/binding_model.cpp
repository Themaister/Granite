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
		LOGE("Failed: %s on line %d\n", str, line);
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

static int main_inner()
{
	if (!Context::init_loader(nullptr))
		return 1;

	Context ctx;

	Context::SystemHandles handles;
	handles.filesystem = GRANITE_FILESYSTEM();
	handles.thread_group = GRANITE_THREAD_GROUP();
	ctx.set_system_handles(handles);

	if (!ctx.init_instance_and_device(nullptr, 0, nullptr, 0))
		return 1;

	Device dev;
	dev.set_context(ctx);

	test_inline_bda(dev, false);
	test_inline_bda(dev, true);

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
