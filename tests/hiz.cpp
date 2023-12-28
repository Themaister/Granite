#include "device.hpp"
#include "command_buffer.hpp"
#include "context.hpp"
#include "math.hpp"
#include "global_managers_init.hpp"
#include "filesystem.hpp"
#include "thread_group.hpp"
#include <stdlib.h>

using namespace Vulkan;
using namespace Granite;

struct Push
{
	mat2 z_transform;
	ivec2 resolution;
	vec2 inv_resolution;
	int mips;
	uint target_counter;
};

int main()
{
	Global::init();
	Filesystem::setup_default_filesystem(GRANITE_FILESYSTEM(), ASSET_DIRECTORY);

	Context::SystemHandles handles = {};
	handles.filesystem = GRANITE_FILESYSTEM();
	handles.thread_group = GRANITE_THREAD_GROUP();

	Context ctx;
	ctx.set_system_handles(handles);
	if (!Context::init_loader(nullptr))
		return EXIT_FAILURE;
	if (!ctx.init_instance_and_device(nullptr, 0, nullptr, 0))
		return EXIT_FAILURE;

	Device dev;
	dev.set_context(ctx);

	float values[128][128];
	for (int y = 0; y < 128; y++)
		for (int x = 0; x < 128; x++)
			values[y][x] = float(y * 128 + x);

	auto info = ImageCreateInfo::immutable_2d_image(128, 128, VK_FORMAT_R32_SFLOAT);
	info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
	ImageInitialData init = {};
	init.data = values;
	auto img = dev.create_image(info, &init);

	info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	info.initial_layout = VK_IMAGE_LAYOUT_GENERAL;
	info.levels = 8;
	auto storage_img = dev.create_image(info);

	BufferCreateInfo buffer_info = {};
	buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	buffer_info.domain = BufferDomain::Device;
	buffer_info.size = sizeof(uint32_t);
	buffer_info.misc = BUFFER_MISC_ZERO_INITIALIZE_BIT;
	auto counter_buffer = dev.create_buffer(buffer_info);

	ImageViewHandle views[8];
	for (unsigned i = 0; i < 8; i++)
	{
		ImageViewCreateInfo view = {};
		view.image = storage_img.get();
		view.format = VK_FORMAT_R32_SFLOAT;
		view.view_type = VK_IMAGE_VIEW_TYPE_2D;
		view.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
		view.base_level = i;
		view.layers = 1;
		view.levels = 1;
		views[i] = dev.create_image_view(view);
	}

	Push push = {};
	push.z_transform = mat2(1.0f);
	push.inv_resolution = vec2(1.0f / 128.0f);
	push.resolution = ivec2(128, 128);
	push.mips = 8;
	push.target_counter = 4;

	bool has_renderdoc = Device::init_renderdoc_capture();
	if (has_renderdoc)
		dev.begin_renderdoc_capture();

	auto cmd = dev.request_command_buffer();
	cmd->set_program("builtin://shaders/post/hiz.comp");
	for (unsigned i = 0; i < 14; i++)
		cmd->set_storage_texture(0, i, *views[i < 8 ? i : 7]);
	cmd->set_texture(1, 0, img->get_view(), StockSampler::NearestClamp);
	cmd->set_storage_buffer(1, 1, *counter_buffer);
	cmd->push_constants(&push, 0, sizeof(push));
	cmd->enable_subgroup_size_control(true);
	cmd->set_subgroup_size_log2(true, 4, 7);
	cmd->dispatch(2, 2, 1);
	dev.submit(cmd);

	if (has_renderdoc)
		dev.end_renderdoc_capture();
}
