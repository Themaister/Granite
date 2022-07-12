#include "device.hpp"
#include "context.hpp"
#include "global_managers_init.hpp"
#include <stdlib.h>

using namespace Vulkan;

int main()
{
	Granite::Global::init(Granite::Global::MANAGER_FEATURE_DEFAULT_BITS, 1);

	if (!Context::init_loader(nullptr))
		return EXIT_FAILURE;

	Context::SystemHandles handles = {};
	handles.filesystem = GRANITE_FILESYSTEM();
	Context ctx;
	ctx.set_system_handles(handles);
	if (!ctx.init_instance_and_device(nullptr, 0, nullptr, 0))
		return EXIT_FAILURE;

	Device device;
	device.set_context(ctx);

	uint32_t count = 0;
	const VkPerformanceCounterKHR *counters = nullptr;
	const VkPerformanceCounterDescriptionKHR *desc = nullptr;
	device.query_available_performance_counters(CommandBuffer::Type::Generic, &count, &counters, &desc);
	PerformanceQueryPool::log_available_counters(counters, desc, count);

	if (!count)
	{
		LOGE("Device does not support performance queries.\n");
		return EXIT_FAILURE;
	}

	BufferCreateInfo bufinfo = {};
	bufinfo.size = 256 * 1024 * 1024;
	bufinfo.domain = BufferDomain::Device;
	bufinfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	auto src = device.create_buffer(bufinfo);
	auto dst = device.create_buffer(bufinfo);

	std::vector<std::string> query_names;
	query_names.emplace_back("VRAM read size");
	//query_names.emplace_back("VRAM write size");
	if (!device.init_performance_counters(CommandBuffer::Type::Generic, query_names))
	{
		LOGE("Failed to initialize perf counters.\n");
		return EXIT_FAILURE;
	}

	if (!device.acquire_profiling())
	{
		LOGE("Failed to acquire profiling lock.\n");
		return EXIT_FAILURE;
	}

	auto cmd = device.request_profiled_command_buffer(CommandBuffer::Type::Generic);
	cmd->copy_buffer(*dst, *src);

	device.submit(cmd);

	device.release_profiling();
}