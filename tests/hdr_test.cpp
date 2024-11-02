/* Copyright (c) 2017-2024 Hans-Kristian Arntzen
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
#include "math.hpp"
#include "muglm/muglm_impl.hpp"
#include "muglm/matrix_helper.hpp"

using namespace Granite;
using namespace Vulkan;

struct Metadata
{
	float max_cll;
	float max_fall;
	float min_lum;
};

constexpr size_t MetadataCount = 10;

static const Metadata metadata[MetadataCount] = {
	{ 200.0f, 20.0f, 0.1f },
	{ 400.0f, 50.0f, 0.15f },
	{ 1000.0f, 200.0f, 0.25f },
	{ 2000.0f, 300.0f, 0.5f },
	{ 4000.0f, 400.0f, 1.0f },
	{ 200.0f, 20.0f, 0.01f },
	{ 400.0f, 50.0f, 0.015f },
	{ 1000.0f, 200.0f, 0.025f },
	{ 2000.0f, 300.0f, 0.05f },
	{ 4000.0f, 400.0f, 0.10f },
};

static float linear_to_srgb(float col)
{
	if (col <= 0.0031308f)
		return col * 12.92f;
	else
		return 1.055f * muglm::pow(col, 1.0f / 2.4f) - 0.055f;
}

static float convert_nits(float nits, bool hdr10)
{
	if (hdr10)
	{
		// PQ
		float y = nits / 10000.0f;
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
		return linear_to_srgb(n);
	}
}

// From https://mina86.com/2019/srgb-xyz-matrix/
static vec3 convert_primary(const VkXYColorEXT &xy)
{
	float X = xy.x / xy.y;
	float Y = 1.0f;
	float Z = (1.0f - xy.x - xy.y) / xy.y;
	return vec3(X, Y, Z);
}

static mat3 compute_xyz_matrix(const VkHdrMetadataEXT &metadata)
{
	vec3 red = convert_primary(metadata.displayPrimaryRed);
	vec3 green = convert_primary(metadata.displayPrimaryGreen);
	vec3 blue = convert_primary(metadata.displayPrimaryBlue);
	vec3 white = convert_primary(metadata.whitePoint);

	vec3 component_scale = inverse(mat3(red, green, blue)) * white;
	return mat3(red * component_scale.x, green * component_scale.y, blue * component_scale.z);
}

struct HDRTest : Granite::Application, Granite::EventHandler
{
	HDRTest()
	{
		EVENT_MANAGER_REGISTER(HDRTest, on_key_down, KeyboardEvent);
		get_wsi().set_backbuffer_format(BackbufferFormat::UNORM);
	}

	bool on_key_down(const KeyboardEvent &e)
	{
		if (e.get_key_state() == KeyState::Pressed)
		{
			if (e.get_key() == Key::Space)
			{
				get_wsi().set_backbuffer_format(get_wsi().get_backbuffer_format() == BackbufferFormat::UNORM ?
				                                BackbufferFormat::HDR10 : BackbufferFormat::UNORM);
			}
			else if (e.get_key() == Key::Up)
			{
				nits += 10;
			}
			else if (e.get_key() == Key::Down)
			{
				nits -= 10;
			}
			else if (e.get_key() == Key::M)
			{
				auto meta = get_wsi().get_hdr_metadata();
				metadata_index = (metadata_index + 1) % MetadataCount;
				meta.maxContentLightLevel = metadata[metadata_index].max_cll;
				meta.maxLuminance = meta.maxContentLightLevel;
				meta.maxFrameAverageLightLevel = metadata[metadata_index].max_fall;
				meta.minLuminance = metadata[metadata_index].min_lum;
				get_wsi().set_hdr_metadata(meta);
			}
		}

		nits = std::max<int>(nits, 10);
		return true;
	}

	void render_frame(double, double) override
	{
		auto &wsi = get_wsi();
		auto &device = wsi.get_device();

		auto cmd = device.request_command_buffer();
		auto rp = device.get_swapchain_render_pass(SwapchainRenderPass::Depth);
		bool hdr10 = get_wsi().get_backbuffer_color_space() == VK_COLOR_SPACE_HDR10_ST2084_EXT;

		float minlum_reference = convert_nits(get_wsi().get_hdr_metadata().minLuminance, hdr10);
		rp.clear_color[0].float32[0] = minlum_reference;
		rp.clear_color[0].float32[1] = minlum_reference;
		rp.clear_color[0].float32[2] = minlum_reference;

		cmd->begin_render_pass(rp);

		flat.begin();
		char text[1024];
		vec3 offset = { 10.0f, 10.0f, 0.0f };
		vec2 size = { cmd->get_viewport().width - 20.0f, cmd->get_viewport().height - 20.0f };

		float nit400_reference = convert_nits(400.0f, hdr10);

		{
			snprintf(text, sizeof(text), "HDR10 (space to toggle): %s", hdr10 ? "ON" : "OFF");
			flat.render_text(GRANITE_UI_MANAGER()->get_font(UI::FontSize::Large), text, offset, size,
			                 vec4(nit400_reference, nit400_reference, 0.0f, 1.0f), Font::Alignment::TopLeft);
		}

		{
			snprintf(text, sizeof(text), "Target nits of gradient (Up / Down to change): %d", nits);
			offset.y += 30.0f;
			flat.render_text(GRANITE_UI_MANAGER()->get_font(UI::FontSize::Normal), text, offset, size,
			                 vec4(nit400_reference, nit400_reference, 0.0f, 1.0f), Font::Alignment::TopLeft);
		}

		if (hdr10)
		{
			auto &meta = get_wsi().get_hdr_metadata();
			snprintf(text, sizeof(text), "Metadata: ST.2086 primaries [MaxCLL/MaxLum = %f] [MaxFALL = %f] [MinLum = %f] (M to toggle)",
			         meta.maxContentLightLevel, meta.maxFrameAverageLightLevel, meta.minLuminance);
		}

		offset.y += 30.0f;

		if (hdr10)
		{
			flat.render_text(GRANITE_UI_MANAGER()->get_font(UI::FontSize::Normal), text, offset, size,
			                 vec4(nit400_reference, nit400_reference, 0.0f, 1.0f), Font::Alignment::TopLeft);
		}

		// D65 is always assumed in Vulkan. See Table 48. Color Spaces and Attributes.
		// sRGB in Vulkan uses BT709 primaries.

		VkHdrMetadataEXT rec709 = {};
		rec709.displayPrimaryRed = { 0.640f, 0.330f };
		rec709.displayPrimaryGreen = { 0.3f, 0.6f };
		rec709.displayPrimaryBlue = { 0.150f, 0.060f };
		rec709.whitePoint = { 0.3127f, 0.3290f };
		const mat3 srgb_to_xyz = compute_xyz_matrix(rec709);
		const mat3 xyz_to_srgb = inverse(srgb_to_xyz);

		const mat3 st2020_to_xyz = compute_xyz_matrix(get_wsi().get_hdr_metadata());
		const mat3 xyz_to_st2020 = inverse(st2020_to_xyz);

		cmd->set_opaque_state();
		cmd->set_program("assets://shaders/hdrtest_srgb_gradient.vert", "assets://shaders/hdrtest_srgb_gradient.frag");
		cmd->set_specialization_constant_mask(1);
		cmd->set_specialization_constant(0, uint32_t(hdr10));

		{
			snprintf(text, sizeof(text), "sRGB gradient [0, 100] nits (sRGB gamma curve)");
			offset.y += 50.0f;
			flat.render_text(GRANITE_UI_MANAGER()->get_font(UI::FontSize::Normal), text, offset, size,
			                 vec4(nit400_reference, nit400_reference, 0.0f, 1.0f), Font::Alignment::TopLeft);

			offset.y += 30.0f;

			vec2 vertex_coords[6] = { { 1280.0f, offset.y },
			                          { 0, offset.y },
			                          { 1280.0f, offset.y + 100.0f },
			                          { 0, offset.y + 100.0f },
			                          { 1280.0f, offset.y + 100.0f },
			                          { 0, offset.y } };

			for (auto &v : vertex_coords)
			{
				v /= vec2(cmd->get_viewport().width, cmd->get_viewport().height);
				v *= 2.0f;
				v -= 1.0f;
			}

			memcpy(cmd->allocate_vertex_data(0, sizeof(vertex_coords), sizeof(vec2)), vertex_coords,
			       sizeof(vertex_coords));
			cmd->set_vertex_attrib(0, 0, VK_FORMAT_R32G32_SFLOAT, 0);

			*cmd->allocate_typed_constant_data<float>(0, 1, 1) = 100.0f;
			cmd->draw(6);

			offset.y += 120.0f;
		}

		{
			snprintf(text, sizeof(text), "ST.2084 gradient [0, %d] nits (sRGB gamma curve)", nits);
			flat.render_text(GRANITE_UI_MANAGER()->get_font(UI::FontSize::Normal), text, offset, size,
			                 vec4(nit400_reference, nit400_reference, 0.0f, 1.0f), Font::Alignment::TopLeft);
			offset.y += 30.0f;

			vec2 vertex_coords[6] = { { 1280.0f, offset.y },
			                          { 0, offset.y },
			                          { 1280.0f, offset.y + 100.0f },
			                          { 0, offset.y + 100.0f },
			                          { 1280.0f, offset.y + 100.0f },
			                          { 0, offset.y } };

			for (auto &v : vertex_coords)
			{
				v /= vec2(cmd->get_viewport().width, cmd->get_viewport().height);
				v *= 2.0f;
				v -= 1.0f;
			}

			memcpy(cmd->allocate_vertex_data(0, sizeof(vertex_coords), sizeof(vec2)), vertex_coords,
			       sizeof(vertex_coords));
			cmd->set_vertex_attrib(0, 0, VK_FORMAT_R32G32_SFLOAT, 0);

			*cmd->allocate_typed_constant_data<float>(0, 1, 1) = float(nits);
			cmd->draw(6);

			offset.y += 120.0f;
		}

		cmd->set_opaque_state();
		cmd->set_program("assets://shaders/hdrtest.vert", "assets://shaders/hdrtest.frag");
		cmd->set_specialization_constant_mask(1);
		cmd->set_specialization_constant(0, uint32_t(hdr10));

		{
			snprintf(text, sizeof(text), "sRGB/BT.709 gradient saturated triangle (%d nits)", nits);
			flat.render_text(GRANITE_UI_MANAGER()->get_font(UI::FontSize::Normal), text, offset, size,
			                 vec4(nit400_reference, nit400_reference, 0.0f, 1.0f), Font::Alignment::TopLeft);
			vec2 vertex_coords[3] = { { 400.0f, offset.y + 30.0f },
			                          { 400.0f - 350.0f, offset.y + 350.0f },
			                          { 400.0f + 350.0f, offset.y + 350.0f } };

			for (auto &v : vertex_coords)
			{
				v /= vec2(cmd->get_viewport().width, cmd->get_viewport().height);
				v *= 2.0f;
				v -= 1.0f;
			}

			memcpy(cmd->allocate_vertex_data(0, sizeof(vertex_coords), sizeof(vec2)), vertex_coords,
			       sizeof(vertex_coords));
			cmd->set_vertex_attrib(0, 0, VK_FORMAT_R32G32_SFLOAT, 0);

			auto *primary_transfer = cmd->allocate_typed_constant_data<mat4>(0, 0, 1);
			if (hdr10)
				*primary_transfer = mat4(xyz_to_st2020 * srgb_to_xyz);
			else
				*primary_transfer = mat4(1.0f);

			*cmd->allocate_typed_constant_data<float>(0, 1, 1) = float(nits);
			cmd->draw(3);
		}

		{
			snprintf(text, sizeof(text), "ST.2020 gradient saturated triangle (%d nits)", nits);
			flat.render_text(GRANITE_UI_MANAGER()->get_font(UI::FontSize::Normal), text, vec3(800.0f, 0.0f, 0.0f) + offset, size,
			                 vec4(nit400_reference, nit400_reference, 0.0f, 1.0f), Font::Alignment::TopLeft);
			vec2 vertex_coords[3] = { { 1200.0f, offset.y + 30.0f },
			                          { 1200.0f - 350.0f, offset.y + 350.0f },
			                          { 1200.0f + 350.0f, offset.y + 350.0f } };

			for (auto &v : vertex_coords)
			{
				v /= vec2(cmd->get_viewport().width, cmd->get_viewport().height);
				v *= 2.0f;
				v -= 1.0f;
			}

			memcpy(cmd->allocate_vertex_data(0, sizeof(vertex_coords), sizeof(vec2)), vertex_coords,
			       sizeof(vertex_coords));
			cmd->set_vertex_attrib(0, 0, VK_FORMAT_R32G32_SFLOAT, 0);

			auto *primary_transfer = cmd->allocate_typed_constant_data<mat4>(0, 0, 1);
			if (!hdr10)
				*primary_transfer = mat4(xyz_to_srgb * st2020_to_xyz);
			else
				*primary_transfer = mat4(1.0f);

			*cmd->allocate_typed_constant_data<float>(0, 1, 1) = float(nits);
			cmd->draw(3);
		}

		flat.flush(*cmd, vec3(0.0f), { cmd->get_viewport().width, cmd->get_viewport().height, 1.0f });
		cmd->end_render_pass();
		device.submit(cmd);
	}

	int nits = 100;
	int metadata_index = 0;
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
