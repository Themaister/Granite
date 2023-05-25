#include "global_managers_init.hpp"
#include "filesystem.hpp"
#include "device.hpp"
#include "context.hpp"

using namespace Granite;
using namespace Vulkan;

static int main_inner()
{
	Context::SystemHandles handles;
	Context ctx;
	Device device;

	VkApplicationInfo app = {};
	app.apiVersion = VK_API_VERSION_1_1;
	app.pEngineName = "vkd3d";
	ctx.set_application_info(&app);

	handles.filesystem = GRANITE_FILESYSTEM();
	ctx.set_system_handles(handles);

	if (!ctx.init_instance_and_device(nullptr, 0, nullptr, 0))
		return EXIT_FAILURE;

	device.set_context(ctx);
	auto &table = device.get_device_table();

	VkIndirectCommandsLayoutCreateInfoNV info = { VK_STRUCTURE_TYPE_INDIRECT_COMMANDS_LAYOUT_CREATE_INFO_NV };

	info.pipelineBindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
	const uint32_t stride = 16;
	info.pStreamStrides = &stride;
	info.streamCount = 1;

	VkIndirectCommandsLayoutTokenNV tokens[2] = {};

	auto *cs = device.get_shader_manager().register_compute("assets://shaders/atomic_increment.comp")->register_variant({})->get_program();

	tokens[0].sType = VK_STRUCTURE_TYPE_INDIRECT_COMMANDS_LAYOUT_TOKEN_NV;
	tokens[0].tokenType = VK_INDIRECT_COMMANDS_TOKEN_TYPE_PUSH_CONSTANT_NV;
	tokens[0].pushconstantShaderStageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	tokens[0].pushconstantPipelineLayout = cs->get_pipeline_layout()->get_layout();
	tokens[0].pushconstantSize = 4;
	tokens[1].sType = VK_STRUCTURE_TYPE_INDIRECT_COMMANDS_LAYOUT_TOKEN_NV;
	tokens[1].tokenType = (VkIndirectCommandsTokenTypeNV)8; // Magic froggery.
	tokens[1].offset = 4;
	info.pTokens = &tokens[1];
	info.tokenCount = 1;

	VkIndirectCommandsLayoutNV layout;
	if (table.vkCreateIndirectCommandsLayoutNV(device.get_device(), &info, nullptr, &layout) != VK_SUCCESS)
	{
		LOGI("Failed to create layout.\n");
		return EXIT_FAILURE;
	}

	BufferCreateInfo atomic_info = {};
	atomic_info.size = 4;
	atomic_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	atomic_info.domain = BufferDomain::CachedHost;
	atomic_info.misc = BUFFER_MISC_ZERO_INITIALIZE_BIT;
	auto atomic_buffer = device.create_buffer(atomic_info);

	const uint32_t dispatch_count_data[] = { 1, 1, 2, 3, 1, 4, 4, 4 };
	uint32_t count_value = 2;

	BufferCreateInfo count_info = {};
	count_info.size = 4;
	count_info.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
	count_info.domain = BufferDomain::LinkedDeviceHost;
	auto count_buffer = device.create_buffer(count_info, &count_value);

#define ACE 1
	auto cmd = device.request_command_buffer(ACE ? CommandBuffer::Type::AsyncCompute : CommandBuffer::Type::Generic);

	{
		cmd->set_program(cs);
		cmd->set_storage_buffer(0, 0, *atomic_buffer);

		VkPipeline pipeline = cmd->get_current_compute_pipeline();

		VkGeneratedCommandsMemoryRequirementsInfoNV generated =
				{ VK_STRUCTURE_TYPE_GENERATED_COMMANDS_MEMORY_REQUIREMENTS_INFO_NV };
		VkMemoryRequirements2 reqs = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2 };

		generated.pipeline = pipeline;
		generated.pipelineBindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
		generated.indirectCommandsLayout = layout;
		generated.maxSequencesCount = 1;

		table.vkGetGeneratedCommandsMemoryRequirementsNV(device.get_device(), &generated, &reqs);

		BufferCreateInfo bufinfo = {};
		bufinfo.size = reqs.memoryRequirements.size;
		bufinfo.domain = BufferDomain::Device;
		bufinfo.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
		bufinfo.allocation_requirements = reqs.memoryRequirements;
		auto preprocess_buffer = device.create_buffer(bufinfo);

		bufinfo.allocation_requirements = {};
		bufinfo.size = sizeof(dispatch_count_data);
		bufinfo.domain = BufferDomain::LinkedDeviceHost;
		auto indirect_buffer = device.create_buffer(bufinfo, dispatch_count_data);

		VkGeneratedCommandsInfoNV exec_info = { VK_STRUCTURE_TYPE_GENERATED_COMMANDS_INFO_NV };

		VkIndirectCommandsStreamNV stream = {};
		stream.buffer = indirect_buffer->get_buffer();
		stream.offset = 0;

		uint32_t c = 1;
		cmd->push_constants(&c, 0, sizeof(c));

		exec_info.indirectCommandsLayout = layout;
		exec_info.pipelineBindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
		exec_info.streamCount = 1;
		exec_info.pStreams = &stream;
		exec_info.preprocessSize = preprocess_buffer->get_create_info().size;
		exec_info.preprocessBuffer = preprocess_buffer->get_buffer();
		exec_info.sequencesCount = sizeof(dispatch_count_data) / 16;
		exec_info.pipeline = cmd->get_current_compute_pipeline();
		exec_info.sequencesCountBuffer = count_buffer->get_buffer();
		exec_info.sequencesCountOffset = 0;
		table.vkCmdExecuteGeneratedCommandsNV(cmd->get_command_buffer(), VK_FALSE, &exec_info);

		cmd->barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
					 VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
	}

	Fence fence;
	device.submit(cmd, &fence);
	fence->wait();

	auto *mapped = static_cast<uint32_t *>(device.map_host_buffer(*atomic_buffer, MEMORY_ACCESS_READ_BIT));
	LOGI("Result: %u\n", mapped[0]);

	uint32_t expected = 0;
	for (unsigned i = 0; i < std::min<uint32_t>(count_value, sizeof(dispatch_count_data) / 16); i++)
	{
		expected += 64 * dispatch_count_data[4 * i + 0] *
		            dispatch_count_data[4 * i + 1] *
		            dispatch_count_data[4 * i + 2] *
		            dispatch_count_data[4 * i + 3];
	}
	LOGI("Expected result: %u\n", expected);

	table.vkDestroyIndirectCommandsLayoutNV(device.get_device(), layout, nullptr);
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