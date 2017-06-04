#pragma once

#include "renderer.hpp"
#include "font.hpp"

namespace Granite
{
struct SpriteTransformInfo
{
	SpriteTransformInfo() = default;

	SpriteTransformInfo(const vec3 &pos, const vec2 &scale = vec2(1.0f, 1.0f),
	                    const mat2 &rot = mat2(1.0f),
	                    const uvec4 &clip = uvec4(0u, 0u, ~0u, ~0u))
		: position(pos),
	      scale(scale),
	      rotation(rot),
	      clip(clip)
	{
	}

	vec3 position = vec3(0.0f);
	vec2 scale = vec2(1.0f);
	mat2 rotation = mat2(1.0f);
	ivec4 clip = ivec4(-0x10000, -0x10000, 0x20000, 0x20000);
};

struct SpriteInfo
{
	AbstractRenderable *sprite;
	SpriteTransformInfo transform;
};
using SpriteList = std::vector<SpriteInfo>;

class FlatRenderer : public EventHandler
{
public:
	FlatRenderer();

	void begin();
	void push_sprite(const SpriteInfo &info);
	void push_sprites(const SpriteList &visible);

	void render_text(const Font &font, const char *text,
	                 const vec3 &offset, const vec2 &size,
	                 Font::Alignment alignment = Font::Alignment::TopLeft, float scale = 1.0f);

	void flush(Vulkan::CommandBuffer &cmd, const vec2 &camera_pos, const vec2 &camera_size);

private:
	void on_device_created(const Event &e);
	void on_device_destroyed(const Event &e);
	Vulkan::Device *device = nullptr;
	RenderQueue queue;
	ShaderSuite suite[Util::ecast(RenderableType::Count)];
};
}