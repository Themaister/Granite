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

#pragma once

#include "abstract_renderable.hpp"
#include "texture_manager.hpp"
#include "flat_renderer.hpp"

namespace Granite
{

namespace RenderFunctions
{
void sprite_render(Vulkan::CommandBuffer &cmd, const RenderQueueData *infos, unsigned instances);
}

struct QuadData
{
	float pos_off_x, pos_off_y, pos_scale_x, pos_scale_y;
	float tex_off_x, tex_off_y, tex_scale_x, tex_scale_y;
	float rotation[4];
	uint8_t color[4];
	float layer;
	float array_layer;
	float blend_factor;
};

struct SpriteInstanceInfo
{
	QuadData *quads;
	unsigned count;
};

struct SpriteRenderInfo
{
	const Vulkan::ImageView *textures[2] = {};
	Vulkan::Program *program = nullptr;
	Vulkan::StockSampler sampler;
	ivec4 clip_quad = ivec4(0, 0, 0x4000, 0x4000);
};

struct Sprite : AbstractRenderable
{
	DrawPipeline pipeline = DrawPipeline::Opaque;
	Vulkan::Texture *texture = nullptr;
	Vulkan::Texture *texture_alt = nullptr;
	Vulkan::StockSampler sampler = Vulkan::StockSampler::LinearWrap;

	enum ShaderVariantFlagBits
	{
		BANDLIMITED_PIXEL_BIT = 1 << 0,
		BLEND_TEXUTRE_BIT = 1 << 1,
		LUMA_TO_ALPHA_BIT = 1 << 2,
		CLEAR_ALPHA_TO_ZERO_BIT = 1 << 3,
		ALPHA_TEXTURE_BIT = 1 << 4,
		ARRAY_TEXTURE_BIT = 1 << 5
	};
	using ShaderVariantFlags = uint32_t;

	ivec2 tex_offset = ivec2(0);
	ivec2 size = ivec2(0);
	uint8_t color[4] = { 0xff, 0xff, 0xff, 0xff };
	float texture_blending_factor = 0.0f;
	bool bandlimited_pixel = false;
	bool luma_to_alpha = false;
	bool clear_alpha_to_zero = false;

	void get_sprite_render_info(const SpriteTransformInfo &transform, RenderQueue &queue) const override;
	void get_render_info(const RenderContext &, const RenderInfoComponent *, RenderQueue &) const override
	{
	}

	DrawPipeline get_mesh_draw_pipeline() const override
	{
		return pipeline;
	}
};
}