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
	info.domain = BufferDomain::Device;
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

	constexpr bool UseOptimized = true;
	bool support_optimized =
	    UseOptimized &&
		(features.vk11_props.subgroupSupportedOperations & required) == required &&
		device.supports_subgroup_size_log2(true, 5, 7);

	auto cmd = device.request_command_buffer();

	if (support_optimized)
	{
		LOGI("Testing optimized shader.\n");
		cmd->set_program("builtin://shaders/lights/clusterer_bindless_z_range_opt.comp");
		cmd->set_subgroup_size_log2(true, 5, 7);
		cmd->enable_subgroup_size_control(true);
	}
	else
	{
		LOGI("Testing naive shader.\n");
		cmd->set_program("builtin://shaders/lights/clusterer_bindless_z_range.comp");
	}

	constexpr unsigned NumInputs = 4 * 1024;
	constexpr unsigned NumRanges = 4 * 1024;

	std::vector<uvec2> inputs(NumInputs);
	for (auto &i : inputs)
		i = uvec2(1000000000, 0);

	auto input_buffer = create_ssbo(device, inputs.data(), sizeof(uvec2) * NumInputs);
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

	constexpr unsigned NumIterations = 1000;
	auto begin_ts = cmd->write_timestamp(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
	for (unsigned i = 0; i < NumIterations; i++)
	{
		if (support_optimized)
			cmd->dispatch((NumRanges + 127) / 128, 1, 1);
		else
			cmd->dispatch((NumRanges + 63) / 64, 1, 1);

		cmd->barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
		             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT);
	}
	auto end_ts = cmd->write_timestamp(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

	device.submit(cmd);
	device.wait_idle();
	double t = device.convert_device_timestamp_delta(begin_ts->get_timestamp_ticks(), end_ts->get_timestamp_ticks());
	LOGI("Time per iteration: %.3f ms.\n", 1000.0 * t / double(NumIterations));

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