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
#include <thread>
#include <mutex>
#include "thread_id.hpp"

using namespace Vulkan;

static uint8_t *align_ptr(uint8_t *ptr, size_t align)
{
	return reinterpret_cast<uint8_t *>((reinterpret_cast<uintptr_t>(ptr) + align - 1) & ~uintptr_t(align - 1));
}

static constexpr uint32_t align = 2 * 1024 * 1024;

static std::mutex global_submit_lock;
static constexpr bool GLOBAL_LOCK = false;

static void thread_looper(Device *device, const Vulkan::Buffer *buf)
{
	Util::register_thread_index(0);

	BufferCreateInfo info = {};
	info.domain = BufferDomain::CachedHost;
	info.size = align;
	info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	for (unsigned i = 0; i < 100; i++)
	{
		auto dst_buffer = device->create_buffer(info);
		for (unsigned j = 0; j < 100; j++)
		{
			auto cmd = device->request_command_buffer();
			cmd->copy_buffer(*dst_buffer, *buf);
			if (GLOBAL_LOCK)
				global_submit_lock.lock();
			device->submit(cmd);
			device->flush_frame();
			if (GLOBAL_LOCK)
				global_submit_lock.unlock();
		}
		device->next_frame_context();
	}
}

int main()
{
	Granite::Global::init();
	if (!Context::init_loader(nullptr))
		return EXIT_FAILURE;

	Context ctx0, ctx1;

	Context::SystemHandles handles;
	ctx0.set_system_handles(handles);
	ctx1.set_system_handles(handles);

	if (!ctx0.init_instance_and_device(nullptr, 0, nullptr, 0))
		return EXIT_FAILURE;
	if (!ctx1.init_instance_and_device(nullptr, 0, nullptr, 0))
		return EXIT_FAILURE;

	Device device0, device1;
	device0.set_context(ctx0);
	device1.set_context(ctx1);

	std::vector<uint8_t> import_buffer(align * 2);
	uint8_t *ptr0 = align_ptr(import_buffer.data(), align);
	memset(ptr0, 0xab, align);

	BufferCreateInfo info = {};
	info.domain = BufferDomain::Host;
	info.size = align;
	info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

	auto buffer0 = device0.create_imported_host_buffer(info, VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT, ptr0);
	if (!buffer0)
		return EXIT_FAILURE;

	auto buffer1 = device1.create_buffer(info);
	if (!buffer1)
		return EXIT_FAILURE;

	std::thread thr0(&thread_looper, &device0, buffer0.get());
	std::thread thr1(&thread_looper, &device1, buffer1.get());
	thr0.join();
	thr1.join();
}
