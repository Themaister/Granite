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

	constexpr unsigned NumInputs = 180;
	constexpr unsigned NumRanges = 200;

	uvec2 inputs[NumInputs] = {};
	for (auto &i : inputs)
		i = uvec2(1000000000, 0);
	inputs[40] = uvec2(189, 190);
	inputs[170] = uvec2(190, 193);
	inputs[171] = uvec2(191, 194);
	inputs[172] = uvec2(192, 195);

	auto input_buffer = create_ssbo(device, inputs, sizeof(uvec2) * NumInputs);
	auto output_buffer = create_ssbo(device, nullptr, sizeof(uvec2) * NumRanges);
	cmd->set_storage_buffer(0, 0, *input_buffer);
	cmd->set_storage_buffer(0, 1, *output_buffer);

	struct Push
	{
		uint32_t num_inputs;
		uint32_t num_inputs_128;
		uint32_t num_ranges;
	} push = {};
	push.num_inputs = NumInputs;
	push.num_inputs_128 = (push.num_inputs + 127) / 128;
	push.num_ranges = NumRanges;
	cmd->push_constants(&push, 0, sizeof(push));

	cmd->dispatch((NumRanges + 127) / 128, 1, 1);
	cmd->barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
	             VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);

	Fence fence;
	device.submit(cmd, &fence);
	fence->wait();

	auto *output_data = static_cast<const uvec2 *>(device.map_host_buffer(*output_buffer, MEMORY_ACCESS_READ_BIT));
	for (uint32_t i = 0; i < NumRanges; i++)
		LOGI("Output %03u = [%u, %u]\n", i, output_data[i].x, output_data[i].y);
	for (uint32_t i = 0; i < NumInputs; i++)
		LOGI("Input %03u = [%u, %u]\n", i, inputs[i].x, inputs[i].y);
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