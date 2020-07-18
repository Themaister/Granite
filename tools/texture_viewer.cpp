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
#include "flat_renderer.hpp"
#include "math.hpp"
#include "ui_manager.hpp"
#include "string_helpers.hpp"

using namespace Granite;
using namespace Vulkan;

struct TextureViewerApplication : Granite::Application, Granite::EventHandler
{
	TextureViewerApplication(std::string path_)
	    : path(std::move(path_))
	{
		EVENT_MANAGER_REGISTER_LATCH(TextureViewerApplication, on_device_create, on_device_destroy, DeviceCreatedEvent);
		EVENT_MANAGER_REGISTER(TextureViewerApplication, on_key_pressed, KeyboardEvent);
	}

	bool on_key_pressed(const KeyboardEvent &e)
	{
		if (e.get_key_state() == KeyState::Pressed)
		{
			switch (e.get_key())
			{
			case Key::Left:
				if (layer)
					layer--;
				break;

			case Key::Right:
				layer++;
				break;

			case Key::Up:
				level++;
				break;

			case Key::Down:
				if (level)
					level--;
				break;

			case Key::R:
				swiz.r = swiz.g = swiz.b = swiz.a = VK_COMPONENT_SWIZZLE_R;
				break;

			case Key::G:
				swiz.r = swiz.g = swiz.b = swiz.a = VK_COMPONENT_SWIZZLE_G;
				break;

			case Key::B:
				swiz.r = swiz.g = swiz.b = swiz.a = VK_COMPONENT_SWIZZLE_B;
				break;

			case Key::A:
				swiz.r = swiz.g = swiz.b = swiz.a = VK_COMPONENT_SWIZZLE_A;
				break;

			case Key::Space:
				swiz = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
				break;

			default:
				break;
			}
		}
		return true;
	}

	void on_device_create(const DeviceCreatedEvent &e)
	{
		texture = e.get_device().get_texture_manager().request_texture(path);
	}

	void on_device_destroy(const DeviceCreatedEvent &)
	{
		texture = nullptr;
	}

	void render_frame(double, double) override
	{
		auto &wsi = get_wsi();
		auto &device = wsi.get_device();

		auto cmd = device.request_command_buffer();

		auto &img = *texture->get_image();
		layer = std::min(layer, img.get_create_info().layers - 1u);
		level = std::min(level, img.get_create_info().levels - 1u);

		ImageViewCreateInfo view_info;
		view_info.image = &img;
		view_info.view_type = VK_IMAGE_VIEW_TYPE_2D;
		view_info.levels = 1;
		view_info.layers = 1;
		view_info.base_layer = layer;
		view_info.base_level = level;
		view_info.swizzle = swiz;
		auto view = device.create_image_view(view_info);

		wsi.set_backbuffer_srgb(Vulkan::format_is_srgb(img.get_format()));

		cmd->begin_render_pass(device.get_swapchain_render_pass(SwapchainRenderPass::ColorOnly));
		cmd->set_texture(0, 0, *view, StockSampler::NearestClamp);
		CommandBufferUtil::draw_fullscreen_quad(*cmd,
		                                        "builtin://shaders/quad.vert",
		                                        "builtin://shaders/blit.frag");

		renderer.begin();
		auto &font = Global::ui_manager()->get_font(UI::FontSize::Normal);
		auto text = Util::join("Layer: ", layer, " Level: ", level, " Format: ", unsigned(img.get_format()));
		renderer.render_text(font, text.c_str(), vec3(0.0f), vec2(1000.0f));
		renderer.render_text(font, text.c_str(), vec3(-2.0f, 2.0f, 0.5f), vec2(1000.0f), vec4(0.0f, 0.0f, 0.0f, 1.0f));
		renderer.flush(*cmd, vec3(-10.0f, -10.0f, 0.0f), vec3(cmd->get_viewport().width, cmd->get_viewport().height, 1.0f));

		cmd->end_render_pass();
		device.submit(cmd);
	}

	FlatRenderer renderer;
	unsigned layer = 0;
	unsigned level = 0;

	Texture *texture = nullptr;
	std::string path;
	VkComponentMapping swiz = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
};

namespace Granite
{
Application *application_create(int argc, char **argv)
{
	application_dummy();

	if (argc != 2)
	{
		LOGE("Usage: texture-viewer [path.{jpg,png,gtx}].\n");
		return nullptr;
	}

	try
	{
		auto *app = new TextureViewerApplication(argv[1]);
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
}
