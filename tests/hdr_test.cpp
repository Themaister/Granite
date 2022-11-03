/* Copyright (c) 2017-2022 Hans-Kristian Arntzen
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
#include "font.hpp"
#include "ui_manager.hpp"
#include "string_helpers.hpp"
#include "global_managers.hpp"
#include "flat_renderer.hpp"
#include "application_events.hpp"

using namespace Granite;
using namespace Vulkan;

struct HDRTest : Granite::Application, Granite::EventHandler
{
	HDRTest()
	{
		EVENT_MANAGER_REGISTER(HDRTest, on_key_down, KeyboardEvent);
	}

	bool on_key_down(const KeyboardEvent &e)
	{
		if (e.get_key_state() == KeyState::Pressed)
		{
			if (e.get_key() == Key::Space)
			{
				get_wsi().set_backbuffer_format(get_wsi().get_backbuffer_format() == BackbufferFormat::sRGB ?
				                                BackbufferFormat::HDR10 : BackbufferFormat::sRGB);
			}
			else if (e.get_key() == Key::Up)
			{
				nits += 10;
			}
			else if (e.get_key() == Key::Down)
			{
				nits -= 10;
			}
		}

		nits = std::max<int>(nits, 10);
		return true;
	}

	static float convert_nits(int nits, bool hdr10)
	{
		if (hdr10)
		{
			float y = float(nits) / 10000.0f;
			constexpr float c1 = 0.8359375f;
			constexpr float c2 = 18.8515625f;
			constexpr float c3 = 18.6875f;
			constexpr float m1 = 0.1593017578125f;
			constexpr float m2 = 78.84375f;
			float num = c1 + c2 * std::pow(y, m1);
			float den = 1.0f + c3 * std::pow(y, m1);
			float n = std::pow(num / den, m2);
			return n;
		}
		else
		{
			float n = std::min<float>(nits, 100.0f) / 100.0f;
			return n;
		}
	}

	void render_frame(double, double) override
	{
		auto &wsi = get_wsi();
		auto &device = wsi.get_device();

		auto cmd = device.request_command_buffer();
		auto rp = device.get_swapchain_render_pass(SwapchainRenderPass::Depth);
		bool hdr10 = get_wsi().get_backbuffer_color_space() == VK_COLOR_SPACE_HDR10_ST2084_EXT;

		float c = convert_nits(nits, hdr10);
		for (auto &color : rp.clear_color[0].float32)
			color = c;

		cmd->begin_render_pass(rp);

		flat.begin();
		char text[1024];
		vec3 offset = { 10.0f, 10.0f, 0.0f };
		vec2 size = { cmd->get_viewport().width - 20.0f, cmd->get_viewport().height - 20.0f };

		{
			snprintf(text, sizeof(text), "HDR10 (space to toggle): %s", hdr10 ? "ON" : "OFF");
			flat.render_text(GRANITE_UI_MANAGER()->get_font(UI::FontSize::Large), text, offset, size,
			                 vec4(0.0f, 0.0f, 0.0f, 1.0f), Font::Alignment::TopLeft, 1.0f);
		}

		{
			snprintf(text, sizeof(text), "Target nits (Up / Down to change): %d", nits);
			offset.y += 30.0f;
			flat.render_text(GRANITE_UI_MANAGER()->get_font(UI::FontSize::Normal), text, offset, size,
			                 vec4(0.0f, 0.0f, 0.0f, 1.0f), Font::Alignment::TopLeft, 1.0f);
		}

		flat.flush(*cmd, vec3(0.0f), { cmd->get_viewport().width, cmd->get_viewport().height, 1.0f });

		cmd->end_render_pass();
		device.submit(cmd);
	}

	int nits = 100;
	FlatRenderer flat;
};

namespace Granite
{
Application *application_create(int, char **)
{
	GRANITE_APPLICATION_SETUP_FILESYSTEM();

	try
	{
		auto *app = new HDRTest();
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
}
