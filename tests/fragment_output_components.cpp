/* Copyright (c) 2017-2020 Hans-Kristian Arntzen
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

#include "application.hpp"
#include "command_buffer.hpp"
#include "device.hpp"
#include "os_filesystem.hpp"
#include "math.hpp"
#include <string.h>
#include "cli_parser.hpp"

using namespace Granite;
using namespace Vulkan;

struct FragmentOutputComponents : Granite::Application, Granite::EventHandler
{
	FragmentOutputComponents(unsigned fb_components, unsigned output_components, unsigned ubo_index)
		: fb_components(fb_components), output_components(output_components), index(ubo_index)
	{
		EVENT_MANAGER_REGISTER_LATCH(FragmentOutputComponents, on_device_created, on_device_destroyed, DeviceCreatedEvent);
	}

	static VkFormat components_to_format(unsigned c)
	{
		assert(c <= 4 && c > 0);
		static const VkFormat fmts[] = { VK_FORMAT_R8_UNORM, VK_FORMAT_R8G8_UNORM, VK_FORMAT_B10G11R11_UFLOAT_PACK32, VK_FORMAT_R8G8B8A8_UNORM };
		return fmts[c - 1];
	}

	void on_device_created(const DeviceCreatedEvent &e)
	{
		ImageCreateInfo info = ImageCreateInfo::render_target(1280, 720, components_to_format(fb_components));
		info.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
		info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		render_target = e.get_device().create_image(info, nullptr);
	}

	void on_device_destroyed(const DeviceCreatedEvent &)
	{
		render_target.reset();
	}

	void render_frame(double, double)
	{
		auto &wsi = get_wsi();
		auto &device = wsi.get_device();

		RenderPassInfo rp_info;
		rp_info.clear_color[0].float32[0] = 1.0f;
		rp_info.clear_color[0].float32[1] = 1.0f;
		rp_info.clear_color[0].float32[2] = 1.0f;
		rp_info.clear_color[0].float32[3] = 1.0f;
		rp_info.num_color_attachments = 1;
		rp_info.color_attachments[0] = &render_target->get_view();
		rp_info.store_attachments = 1;
		rp_info.clear_attachments = 1;

		auto cmd = device.request_command_buffer();

		cmd->image_barrier(*render_target, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
		                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
		cmd->begin_render_pass(rp_info);
		*cmd->allocate_typed_constant_data<vec4>(0, 0, 1) = vec4(1.0f, 0.0f, 0.0f, 1.0f);
		*cmd->allocate_typed_constant_data<vec4>(0, 1, 1) = vec4(0.0f, 1.0f, 0.0f, 1.0f);
		*cmd->allocate_typed_constant_data<vec4>(0, 2, 1) = vec4(0.0f, 0.0f, 1.0f, 1.0f);
		*cmd->allocate_typed_constant_data<vec4>(0, 3, 1) = vec4(1.0f, 1.0f, 1.0f, 1.0f);
		cmd->push_constants(&index, 0, sizeof(index));
		CommandBufferUtil::draw_fullscreen_quad(*cmd, "builtin://shaders/quad.vert", "assets://shaders/fill_flat.frag", {{ "OUTPUT_COMPONENTS", output_components }});
		cmd->end_render_pass();
		cmd->image_barrier(*render_target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

		auto rp = device.get_swapchain_render_pass(SwapchainRenderPass::Depth);
		rp.clear_color[0].float32[0] = 0.1f;
		rp.clear_color[0].float32[1] = 0.2f;
		rp.clear_color[0].float32[2] = 0.3f;
		cmd->begin_render_pass(rp);
		cmd->set_texture(0, 0, render_target->get_view(), StockSampler::LinearClamp);
		CommandBufferUtil::draw_fullscreen_quad(*cmd, "builtin://shaders/quad.vert", "builtin://shaders/blit.frag");
		cmd->end_render_pass();
		device.submit(cmd);
	}

	ImageHandle render_target;
	unsigned fb_components;
	unsigned output_components;
	uint32_t index;
};

namespace Granite
{
Application *application_create(int argc, char **argv)
{
	application_dummy();

#ifdef ASSET_DIRECTORY
	const char *asset_dir = getenv("ASSET_DIRECTORY");
	if (!asset_dir)
		asset_dir = ASSET_DIRECTORY;

	Global::filesystem()->register_protocol("assets", std::unique_ptr<FilesystemBackend>(new OSFilesystem(asset_dir)));
#endif

	unsigned fb_components = 4;
	unsigned output_components = 4;
	unsigned ubo_index = 0;

	Util::CLICallbacks cbs;
	cbs.add("--fb-components", [&](Util::CLIParser &parser) { fb_components = parser.next_uint(); });
	cbs.add("--output-components", [&](Util::CLIParser &parser) { output_components = parser.next_uint(); });
	cbs.add("--ubo-index", [&](Util::CLIParser &parser) { ubo_index = parser.next_uint(); });
	Util::CLIParser parser(std::move(cbs), argc - 1, argv + 1);

	if (!parser.parse())
		return nullptr;
	else if (parser.is_ended_state())
		return nullptr;

	try
	{
		auto *app = new FragmentOutputComponents(fb_components, output_components, ubo_index);
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
}