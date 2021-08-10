#include "global_managers_init.hpp"
#include "os_filesystem.hpp"
#include "device.hpp"
#include "math.hpp"
#include <random>

using namespace Granite;
using namespace Vulkan;

static BufferHandle create_ssbo(Device &device, const void *data, size_t size)
{
	BufferCreateInfo info = {};
	info.size = size;
	info.domain = BufferDomain::CachedHost;
	info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	return device.create_buffer(info, data);
}

static int main_inner()
{
	Context ctx;
	Context::SystemHandles handles;
	handles.filesystem = GRANITE_FILESYSTEM();
	ctx.set_system_handles(handles);
	if (!ctx.init_instance_and_device(nullptr, 0, nullptr, 0))
		return EXIT_FAILURE;
	Device device;
	device.set_context(ctx);

	auto &features = device.get_device_features();
	constexpr VkSubgroupFeatureFlags required =
	    VK_SUBGROUP_FEATURE_SHUFFLE_BIT |
	    VK_SUBGROUP_FEATURE_BASIC_BIT;
	if ((features.subgroup_properties.supportedOperations & required) != required)
		return EXIT_FAILURE;
	if (!device.supports_subgroup_size_log2(true, 5, 7))
		return EXIT_FAILURE;

	auto cmd = device.request_command_buffer();
	cmd->set_program("assets://shaders/bit_transpose.comp");
	cmd->set_subgroup_size_log2(true, 5, 7);
	cmd->enable_subgroup_size_control(true);

	uvec4 inputs[128] = {};
	std::mt19937 rnd(1234);
	for (auto &inp : inputs)
		for (int i = 0; i < 4; i++)
			inp[i] = uint32_t(rnd());

	auto input_buffer = create_ssbo(device, inputs, sizeof(inputs));
	auto output_buffer = create_ssbo(device, nullptr, sizeof(inputs));
	cmd->set_storage_buffer(0, 0, *input_buffer);
	cmd->set_storage_buffer(0, 1, *output_buffer);
	cmd->dispatch(1, 1, 1);
	cmd->barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
	             VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);

	Fence fence;
	device.submit(cmd, &fence);
	fence->wait();

	auto *output_data = static_cast<const uvec4 *>(device.map_host_buffer(*output_buffer, MEMORY_ACCESS_READ_BIT));
	for (uint32_t i = 0; i < 128; i++)
		LOGI("Output %03u = 0x%08x%08x%08x%08x\n", i, output_data[i].w, output_data[i].z, output_data[i].y, output_data[i].x);
	for (uint32_t i = 0; i < 128; i++)
		LOGI("Input %03u = 0x%08x%08x%08x%08x\n", i, inputs[i].w, inputs[i].z, inputs[i].y, inputs[i].x);

	const auto read_bit = [](const uvec4 *values, unsigned x, unsigned y) -> bool {
		return (values[y][x / 32] & (1u << (x & 31))) != 0;
	};

	for (unsigned y = 0; y < 128; y++)
	{
		for (unsigned x = 0; x < 128; x++)
		{
			bool reference = read_bit(inputs, y, x);
			bool actual = read_bit(output_data, x, y);
			if (reference != actual)
			{
				LOGE("Mismatch at %u, %u.\n", x, y);
				return EXIT_FAILURE;
			}
		}
	}
	LOGI("Success!\n");
	device.unmap_host_buffer(*output_buffer, MEMORY_ACCESS_READ_BIT);
	return EXIT_SUCCESS;
}

int main()
{
	Global::init(Global::MANAGER_FEATURE_FILESYSTEM_BIT);
	if (!Context::init_loader(nullptr))
		return EXIT_FAILURE;
	Filesystem::setup_default_filesystem(GRANITE_FILESYSTEM(), ASSET_DIRECTORY);
	int ret = main_inner();
	Global::deinit();
	return ret;
}