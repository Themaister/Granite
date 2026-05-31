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
#include "math.hpp"
#include "muglm/muglm_impl.hpp"
#include "timer.hpp"

using namespace Granite;
using namespace Vulkan;

static BufferHandle create_buffer(Device &device, VkDeviceSize size, const void *data)
{
	BufferCreateInfo buf;
	buf.size = size;
	buf.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
	            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	buf.domain = BufferDomain::Device;
	return device.create_buffer(buf, data);
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
		CONTEXT_CREATION_ENABLE_POST_MORTEM_BIT))
		return 1;

	Device dev;
	dev.set_context(ctx);

	auto buf = create_buffer(dev, 16, nullptr);

	auto cmd = dev.request_command_buffer();

	cmd->set_program("assets://shaders/fault.comp");
	auto ptr = buf->get_device_address();

	cmd->push_constants(&ptr, 0, sizeof(ptr));

	cmd->dispatch(1, 1, 1);
	cmd->dispatch(1, 1, 1);

	ptr = 0x40000000;
	cmd->push_constants(&ptr, 0, sizeof(ptr));
	cmd->checkpoint("before should fault");
	cmd->dispatch(1, 1, 1);
	cmd->checkpoint("after should fault");

	cmd->barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
				 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);

	ptr = buf->get_device_address();
	cmd->push_constants(&ptr, 0, sizeof(ptr));
	cmd->dispatch(1, 1, 1);
	cmd->dispatch(1, 1, 1);
	cmd->dispatch(1, 1, 1);
	cmd->dispatch(1, 1, 1);
	cmd->dispatch(1, 1, 1);
	cmd->dispatch(1, 1, 1);

	Fence fence;
	dev.submit(cmd, &fence);
	fence->wait();

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
