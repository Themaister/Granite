/* Copyright (c) 2017-2024 Hans-Kristian Arntzen
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

#include "vulkan_headers.hpp"
#include "device.hpp"
#include "global_managers_init.hpp"
#include "thread_group.hpp"
#include <string.h>

using namespace Vulkan;

static uint8_t *align_ptr(uint8_t *ptr, size_t align)
{
	return reinterpret_cast<uint8_t *>((reinterpret_cast<uintptr_t>(ptr) + align - 1) & ~uintptr_t(align - 1));
}

int main()
{
	Granite::Global::init();
	if (!Context::init_loader(nullptr))
		return EXIT_FAILURE;

	Context ctx;

	Context::SystemHandles handles;
	handles.filesystem = GRANITE_FILESYSTEM();
	handles.thread_group = GRANITE_THREAD_GROUP();
	ctx.set_system_handles(handles);

	if (!ctx.init_instance_and_device(nullptr, 0, nullptr, 0))
		return EXIT_FAILURE;

	Device device;
	device.set_context(ctx);

	auto align = device.get_device_features().host_memory_properties.minImportedHostPointerAlignment;
	std::vector<uint8_t> import_buffer(align * 2);
	uint8_t *ptr = align_ptr(import_buffer.data(), align);

	BufferCreateInfo info = {};
	info.domain = BufferDomain::CachedHost;
	info.size = align;
	info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	auto buffer = device.create_imported_host_buffer(info, VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT, ptr);

	if (!buffer)
		return EXIT_FAILURE;

	info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	auto dst_buffer = device.create_buffer(info);

	auto cmd = device.request_command_buffer();
	cmd->copy_buffer(*dst_buffer, *buffer);

	memset(ptr, 0xab, align);
	device.submit(cmd);
	device.wait_idle();

	auto *dst_mapped = device.map_host_buffer(*dst_buffer, MEMORY_ACCESS_READ_BIT);
	if (memcmp(dst_mapped, ptr, align) != 0)
	{
		LOGE("Failure!\n");
		return EXIT_FAILURE;
	}
	else
		LOGI(":3\n");
	device.unmap_host_buffer(*dst_buffer, MEMORY_ACCESS_READ_BIT);
}
