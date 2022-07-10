#include "context.hpp"
#include "device.hpp"
#include "logging.hpp"

#ifndef _WIN32
#include <unistd.h>
#endif

using namespace Vulkan;

static bool run_test(Device &device)
{
	BufferHandle readback_buffer;
	BufferHandle write_buffer;
	BufferCreateInfo info = {};
	info.size = 64 * 1024 * sizeof(uint32_t);
	info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	info.domain = BufferDomain::CachedHost;
	readback_buffer = device.create_buffer(info);
	info.size = sizeof(uint32_t);
	info.domain = BufferDomain::Device;
	write_buffer = device.create_buffer(info);

	// Ping-pong between queues using external semaphore handles.
	// Same device, need external memory to test further.

	for (uint32_t i = 0; i < 1024; i++)
	{
		auto fill_cmd = device.request_command_buffer();
		fill_cmd->fill_buffer(*write_buffer, i, 0, sizeof(uint32_t));
		device.submit(fill_cmd);

		auto external = device.request_binary_semaphore_external();
		device.submit_empty(CommandBuffer::Type::Generic, nullptr, external.get());

		ExternalHandle handle = external->export_to_opaque_handle();
		if (!validate_handle(handle))
			break;

		auto import = device.request_binary_semaphore_external();
		if (!import->import_from_opaque_handle(handle))
		{
			::close(handle);
			break;
		}

		device.add_wait_semaphore(CommandBuffer::Type::AsyncTransfer, import, VK_PIPELINE_STAGE_TRANSFER_BIT, true);
		auto copy_cmd = device.request_command_buffer(CommandBuffer::Type::AsyncTransfer);
		const VkBufferCopy copy = { 0, i * sizeof(uint32_t), sizeof(uint32_t) };
		copy_cmd->copy_buffer(*readback_buffer, *write_buffer, &copy, 1);
		device.submit(copy_cmd);

		external = device.request_binary_semaphore_external();
		device.submit_empty(CommandBuffer::Type::AsyncTransfer, nullptr, external.get());

		handle = external->export_to_opaque_handle();
		if (!validate_handle(handle))
			break;

		import = device.request_binary_semaphore_external();
		if (!import->import_from_opaque_handle(handle))
		{
			::close(handle);
			break;
		}

		device.add_wait_semaphore(CommandBuffer::Type::Generic, import, VK_PIPELINE_STAGE_TRANSFER_BIT, true);

		device.next_frame_context();
	}

	device.wait_idle();
	auto *ptr = static_cast<const uint32_t *>(device.map_host_buffer(*readback_buffer, MEMORY_ACCESS_READ_BIT));
	for (uint32_t i = 0; i < 1024; i++)
	{
		if (ptr[i] != i)
		{
			LOGE("Expected %u, got %u.\n", i, ptr[i]);
			return false;
		}
	}

	return true;
}

int main()
{
	if (!Context::init_loader(nullptr))
	{
		LOGE("Failed.\n");
		return EXIT_FAILURE;
	}

	Context ctx;
	if (!ctx.init_instance_and_device(nullptr, 0, nullptr, 0))
		return EXIT_FAILURE;

	Device dev;
	dev.set_context(ctx);
	if (!run_test(dev))
		return EXIT_FAILURE;
}