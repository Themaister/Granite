#include "application.hpp"
#include "global_managers_init.hpp"
#include "os_filesystem.hpp"
#include "device.hpp"
#include "thread_group.hpp"
#include "context.hpp"
#include "environment.hpp"

using namespace Granite;
using namespace Vulkan;

static int main_inner()
{
	if (!Context::init_loader(nullptr))
		return 1;

	Context ctx;

	Context::SystemHandles handles;
	handles.filesystem = GRANITE_FILESYSTEM();
	handles.thread_group = GRANITE_THREAD_GROUP();
	ctx.set_system_handles(handles);

	if (!ctx.init_instance_and_device(nullptr, 0, nullptr, 0, CONTEXT_CREATION_ENABLE_ROBUSTNESS_2_BIT))
		return 1;

	Device dev;
	dev.set_context(ctx);

	dev.init_renderdoc_capture();
	dev.begin_renderdoc_capture();

	auto cmd = dev.request_command_buffer();
	cmd->set_program("assets://shaders/robustness2.comp");

	static const vec4 input_data[] = {
		vec4(10, 11, 12, 13),
		vec4(14, 15, 16, 17),
		vec4(20, 21, 22, 23),
		vec4(24, 25, 26, 27),
	};

	BufferCreateInfo buf;
	buf.size = sizeof(input_data);
	buf.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	buf.domain = BufferDomain::CachedHost;
	auto input_ssbo = dev.create_buffer(buf, input_data);

	buf.size = sizeof(vec4) * 2;
	auto output_ssbo = dev.create_buffer(buf);

	cmd->set_storage_buffer(0, 0, *input_ssbo, 0, 32);
	cmd->set_storage_buffer(0, 1, *output_ssbo);
	cmd->dispatch(1, 1, 1);
	cmd->barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
				 VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
	dev.submit(cmd);
	dev.wait_idle();

	auto *ptr = static_cast<const float *>(dev.map_host_buffer(*output_ssbo, MEMORY_ACCESS_READ_BIT));
	for (unsigned i = 0; i < 8; i++)
		LOGI("Output %u = %f\n", i, ptr[i]);

	dev.end_renderdoc_capture();

	return 0;
}

int main()
{
	Global::init();

#ifdef ASSET_DIRECTORY
	auto asset_dir = Util::get_environment_string("ASSET_DIRECTORY", ASSET_DIRECTORY);
	GRANITE_FILESYSTEM()->register_protocol("assets", std::unique_ptr<FilesystemBackend>(new OSFilesystem(asset_dir)));
#endif
	int ret = main_inner();
	Global::deinit();
	return ret;
}
