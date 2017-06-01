#pragma once

#include "vulkan_events.hpp"
#include "scene.hpp"
#include "shader_suite.hpp"

namespace Granite
{
class Sprite;
enum class RenderableType
{
	Mesh,
	Skybox,
	Sprite,
	Count
};

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
	uvec4 clip = uvec4(0u, 0u, ~0u, ~0u);
};

struct SpriteInfo
{
	AbstractRenderable *sprite;
	SpriteTransformInfo transform;
};
using SpriteList = std::vector<SpriteInfo>;

class Renderer : public EventHandler
{
public:
	Renderer();
	void render(Vulkan::CommandBuffer &cmd, RenderContext &context, const VisibilityList &visible);
	void render_sprites(Vulkan::CommandBuffer &cmd, const vec2 &camera_pos, const vec2 &camera_size, const SpriteList &visible);

	RenderQueue &get_render_queue()
	{
		return queue;
	}

private:
	void on_device_created(const Event &e);
	void on_device_destroyed(const Event &e);
	Vulkan::Device *device = nullptr;
	RenderQueue queue;
	ShaderSuite suite[Util::ecast(RenderableType::Count)];
};
}