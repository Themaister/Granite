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

#include <click_button.hpp>
#include <slider.hpp>
#include <toggle_button.hpp>
#include "application.hpp"
#include "os_filesystem.hpp"
#include "ui_manager.hpp"
#include "window.hpp"

using namespace Granite;
using namespace Vulkan;
using namespace Util;

struct UIApplication : Application, public EventHandler
{
	UIApplication();
	void render_frame(double, double) override;

	void on_device_created(const DeviceCreatedEvent &e);
	void on_device_destroyed(const DeviceCreatedEvent &e);
};

UIApplication::UIApplication()
{
	EVENT_MANAGER_REGISTER_LATCH(UIApplication, on_device_created, on_device_destroyed, DeviceCreatedEvent);
}

void UIApplication::on_device_destroyed(const DeviceCreatedEvent &)
{
}

void UIApplication::on_device_created(const DeviceCreatedEvent &e)
{
	auto &device = e.get_device();
	auto &ui = *Global::ui_manager();
	ui.reset_children();

	auto window = make_handle<UI::Window>();
	ui.add_child(window);

	window->set_fullscreen(true);
	window->show_title_bar(false);
	window->set_floating(false);
	window->set_background_color(vec4(0.0f, 1.0f, 0.0f, 1.0f));
	window->set_background_image(device.get_texture_manager().request_texture("builtin://textures/checkerboard.png"));

	auto button = make_handle<UI::ClickButton>();
	window->add_child(button);

	button->set_floating(true);
	button->set_text("THIS IS A COOL BUTTON.");
	button->set_floating_position(vec2(50.0f));

	button = make_handle<UI::ClickButton>();
	window->add_child(button);
	button->set_floating(true);
	button->set_text("THIS IS ALSO A COOL BUTTON.");
	button->set_floating_position(vec2(50.0f, 80.0f));

	button = make_handle<UI::ClickButton>();
	window->add_child(button);
	button->set_text("#0");

	button = make_handle<UI::ClickButton>();
	window->add_child(button);
	button->set_text("#1");

	auto slider = make_handle<UI::Slider>();
	window->add_child(slider);

	{
		slider->set_floating(true);
		slider->set_floating_position(vec2(100.0f));
		slider->set_text("Value");
		slider->set_size(vec2(200.0f, 30.0f));
		slider->set_label_slider_gap(10.0f);
		slider->set_color(vec4(1.0f, 0.0f, 0.0f, 1.0f));
		slider->set_orientation(UI::Slider::Orientation::Horizontal);
		slider->set_background_color(vec4(0.0f, 0.0f, 0.0f, 1.0f));
		slider->show_label(false);
		slider->show_value(false);
		slider->set_margin(5.0f);
		slider->show_tooltip(true);
		slider->set_background_image(device.get_texture_manager().request_texture("builtin://textures/checkerboard.png"));
		slider->set_background_color(vec4(1.0f));
	}

	slider = make_handle<UI::Slider>();
	window->add_child(slider);
	{
		auto &sli = *slider;
		sli.set_floating(true);
		sli.set_floating_position(vec2(500.0f, 100.0f));
		sli.set_text("Value");
		sli.set_size(vec2(30.0f, 200.0f));
		sli.set_label_slider_gap(0.0f);
		sli.set_color(vec4(1.0f, 0.0f, 0.0f, 1.0f));
		sli.set_orientation(UI::Slider::Orientation::Vertical);
		sli.set_background_color(vec4(0.0f, 0.0f, 0.0f, 1.0f));
		sli.show_label(false);
		sli.show_value(false);
		sli.set_margin(5.0f);
		sli.show_tooltip(true);
		sli.set_background_image(device.get_texture_manager().request_texture("builtin://textures/checkerboard.png"));
		sli.set_background_color(vec4(1.0f));
	}

	auto toggle_button = make_handle<UI::ToggleButton>();
	window->add_child(toggle_button);
	{
		auto &btn = *toggle_button;
		btn.set_floating_position(vec2(100.0f, 500.0f));
		btn.set_floating(true);
		btn.set_background_color(vec4(0.0f, 0.0f, 0.0f, 1.0f));
		btn.set_text("Mjuu");
		btn.set_toggled_font_color(vec4(0.0f, 1.0f, 0.0f, 1.0f));
		btn.set_untoggled_font_color(vec4(1.0f, 0.0f, 0.0f, 1.0f));
		btn.set_background_image(device.get_texture_manager().request_texture("builtin://textures/checkerboard.png"));
		btn.set_background_color(vec4(1.0f));
	}
}

void UIApplication::render_frame(double, double)
{
	auto &device = get_wsi().get_device();
	auto cmd = device.request_command_buffer();
	auto rp = device.get_swapchain_render_pass(SwapchainRenderPass::Depth);
	cmd->begin_render_pass(rp);
	Global::ui_manager()->render(*cmd);
	cmd->end_render_pass();
	device.submit(cmd);
}

namespace Granite
{
Application *application_create(int, char **)
{
	application_dummy();

#ifdef ASSET_DIRECTORY
	const char *asset_dir = getenv("ASSET_DIRECTORY");
	if (!asset_dir)
		asset_dir = ASSET_DIRECTORY;

	Global::filesystem()->register_protocol("assets", std::unique_ptr<FilesystemBackend>(new OSFilesystem(asset_dir)));
#endif

	try
	{
		auto *app = new UIApplication();
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
}
