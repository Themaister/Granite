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

#include "renderer.hpp"
#include "font.hpp"
#include "sprite.hpp"
#include <vector>

namespace Granite
{
struct SpriteTransformInfo
{
	SpriteTransformInfo() = default;

	SpriteTransformInfo(const vec3 &pos, const vec2 &scale_ = vec2(1.0f, 1.0f),
	                    const mat2 &rot = mat2(1.0f),
	                    const ivec4 &clip_ = ivec4(0, 0, 0x4000, 0x4000))
		: position(pos),
	      scale(scale_),
	      rotation(rot),
	      clip(clip_)
	{
	}

	vec3 position = vec3(0.0f);
	vec2 scale = vec2(1.0f);
	mat2 rotation = mat2(1.0f);
	ivec4 clip = ivec4(0, 0, 0x4000, 0x4000);
};

struct SpriteInfo
{
	AbstractRenderable *sprite;
	SpriteTransformInfo transform;
};
using SpriteList = std::vector<SpriteInfo>;

struct LineInfo
{
	vec3 *positions;
	vec4 *colors;
	unsigned count;
};

struct LineStripInfo
{
	Vulkan::Program *program;
	ivec4 clip = ivec4(0, 0, 0x4000, 0x4000);
};

class FlatRenderer : public EventHandler
{
public:
	explicit FlatRenderer(const ShaderSuiteResolver *resolver = nullptr);

	void begin();
	void push_sprite(const SpriteInfo &info);
	void push_sprites(const SpriteList &visible);

	void render_quad(const vec3 &offset, const vec2 &size, const vec4 &color);

	void render_textured_quad(const Vulkan::ImageView &view, const vec3 &offset, const vec2 &size,
	                          const vec2 &tex_offset, const vec2 &tex_size,
	                          DrawPipeline pipeline,
	                          const vec4 &color = vec4(1.0f),
	                          Vulkan::StockSampler sampler = Vulkan::StockSampler::LinearClamp,
	                          unsigned layer = 0);

	void render_text(const Font &font, const char *text,
	                 const vec3 &offset, const vec2 &size,
	                 const vec4 &color = vec4(1.0f),
	                 Font::Alignment alignment = Font::Alignment::TopLeft, float scale = 1.0f);

	void flush(Vulkan::CommandBuffer &cmd, const vec3 &camera_pos, const vec3 &camera_size);
	void render_line_strip(const vec2 *offsets, float layer, unsigned count, const vec4 &color);

	void reset_scissor();
	void push_scissor(const vec2 &offset, const vec2 &size);
	void pop_scissor();

private:
	void on_device_created(const Vulkan::DeviceCreatedEvent &e);
	void on_device_destroyed(const Vulkan::DeviceCreatedEvent &e);
	Vulkan::Device *device = nullptr;
	const ShaderSuiteResolver *resolver = nullptr;
	RenderQueue queue;
	ShaderSuite suite[Util::ecast(RenderableType::Count)];

	struct Scissor
	{
		vec2 offset;
		vec2 size;
	};
	std::vector<Scissor> scissor_stack;

	void render_quad(const Vulkan::ImageView *view, unsigned layer, Vulkan::StockSampler sampler,
	                 const vec3 &offset, const vec2 &size, const vec2 &tex_offset, const vec2 &tex_size, const vec4 &color,
	                 DrawPipeline pipeline);

	void build_scissor(ivec4 &clip, const vec2 &minimum, const vec2 &maximum) const;
};
}