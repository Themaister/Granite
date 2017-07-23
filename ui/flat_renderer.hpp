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

	SpriteTransformInfo(const vec3 &pos, const vec2 &scale = vec2(1.0f, 1.0f),
	                    const mat2 &rot = mat2(1.0f),
	                    const ivec4 &clip = ivec4(0, 0, 0x4000, 0x4000))
		: position(pos),
	      scale(scale),
	      rotation(rot),
	      clip(clip)
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
	FlatRenderer();

	void begin();
	void push_sprite(const SpriteInfo &info);
	void push_sprites(const SpriteList &visible);

	void render_quad(const vec3 &offset, const vec2 &size, const vec4 &color);
	void render_textured_quad(const Vulkan::ImageView &view, const vec3 &offset, const vec2 &size,
	                          const vec2 &tex_offset, const vec2 &tex_size,
	                          bool transparent = false,
	                          const vec4 &color = vec4(1.0f),
	                          Vulkan::StockSampler sampler = Vulkan::StockSampler::LinearClamp);

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
	void on_device_created(const Event &e);
	void on_device_destroyed(const Event &e);
	Vulkan::Device *device = nullptr;
	RenderQueue queue;
	ShaderSuite suite[Util::ecast(RenderableType::Count)];

	struct Scissor
	{
		vec2 offset;
		vec2 size;
	};
	std::vector<Scissor> scissor_stack;

	void render_quad(const Vulkan::ImageView *view, Vulkan::StockSampler sampler,
	                 const vec3 &offset, const vec2 &size, const vec2 &tex_offset, const vec2 &tex_size, const vec4 &color,
	                 bool transparent);

	void build_scissor(ivec4 &clip, const vec2 &minimum, const vec2 &maximum) const;
};
}