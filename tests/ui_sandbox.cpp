/* Copyright (c) 2017 Hans-Kristian Arntzen
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
#include "os.hpp"
#include "ui_manager.hpp"
#include "window.hpp"

using namespace Granite;
using namespace Vulkan;
using namespace Util;

struct UIApplication : Granite::Application
{
	UIApplication();
	void render_frame(double, double) override;
};

UIApplication::UIApplication()
{
	auto &ui = UI::UIManager::get();
	auto window = make_abstract_handle<UI::Widget, UI::Window>();
	ui.add_child(window);

	auto &win = static_cast<UI::Window &>(*window);
	win.set_fullscreen(true);
	win.show_title_bar(false);
	win.set_floating(false);
	win.set_background_color(vec4(0.0f, 1.0f, 0.0f, 1.0f));

	auto button = make_abstract_handle<UI::Widget, UI::ClickButton>();
	win.add_child(button);

	auto &btn0 = static_cast<UI::ClickButton &>(*button);
	btn0.set_floating(true);
	btn0.set_text("THIS IS A COOL BUTTON.");
	btn0.set_floating_position(vec2(50.0f));

	button = make_abstract_handle<UI::Widget, UI::ClickButton>();
	win.add_child(button);
	auto &btn1 = static_cast<UI::ClickButton &>(*button);
	btn1.set_floating(true);
	btn1.set_text("THIS IS ALSO A COOL BUTTON.");
	btn1.set_floating_position(vec2(50.0f, 80.0f));

	button = make_abstract_handle<UI::Widget, UI::ClickButton>();
	win.add_child(button);
	auto &btn2 = static_cast<UI::ClickButton &>(*button);
	btn2.set_text("#0");

	button = make_abstract_handle<UI::Widget, UI::ClickButton>();
	win.add_child(button);
	auto &btn3 = static_cast<UI::ClickButton &>(*button);
	btn3.set_text("#1");

	auto slider = make_abstract_handle<UI::Widget, UI::Slider>();
	win.add_child(slider);

	{
		auto &sli = static_cast<UI::Slider &>(*slider);
		sli.set_floating(true);
		sli.set_floating_position(vec2(100.0f));
		sli.set_text("Value");
		sli.set_size(vec2(200.0f, 30.0f));
		sli.set_label_slider_gap(10.0f);
		sli.set_color(vec4(1.0f, 0.0f, 0.0f, 1.0f));
		sli.set_orientation(UI::Slider::Orientation::Horizontal);
		sli.set_background_color(vec4(0.0f, 0.0f, 0.0f, 1.0f));
		sli.show_label(false);
		sli.show_value(false);
		sli.set_margin(5.0f);
		sli.show_tooltip(true);
	}

	slider = make_abstract_handle<UI::Widget, UI::Slider>();
	win.add_child(slider);
	{
		auto &sli = static_cast<UI::Slider &>(*slider);
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
	}

	button = make_abstract_handle<UI::Widget, UI::ToggleButton>();
	win.add_child(button);
	{
		auto &btn = static_cast<UI::ToggleButton &>(*button);
		btn.set_floating_position(vec2(100.0f, 500.0f));
		btn.set_floating(true);
		btn.set_background_color(vec4(0.0f, 0.0f, 0.0f, 1.0f));
		btn.set_text("Mjuu");
		btn.set_toggled_font_color(vec4(0.0f, 1.0f, 0.0f, 1.0f));
		btn.set_untoggled_font_color(vec4(1.0f, 0.0f, 0.0f, 1.0f));
	}
}

void UIApplication::render_frame(double, double)
{
	auto &device = get_wsi().get_device();
	auto cmd = device.request_command_buffer();
	auto rp = device.get_swapchain_render_pass(SwapchainRenderPass::Depth);
	cmd->begin_render_pass(rp);
	UI::UIManager::get().render(*cmd);
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

	Filesystem::get().register_protocol("assets", std::unique_ptr<FilesystemBackend>(new OSFilesystem(asset_dir)));
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
