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

	SpriteTransformInfo(const vec3 &pos)
		: position(pos)
	{
	}

	SpriteTransformInfo(const vec3 &pos, const vec2 &scale, const mat2 &rot)
		: position(pos),
	      scale(scale),
	      rotation(rot)
	{
	}

	vec3 position = vec3(0.0f);
	vec2 scale = vec2(1.0f);
	mat2 rotation = mat2(1.0f);
};

struct SpriteInfo
{
	Sprite *sprite;
	SpriteTransformInfo transform;
};
using SpriteList = std::vector<SpriteInfo>;

class Renderer : public EventHandler
{
public:
	Renderer();
	void render(Vulkan::CommandBuffer &cmd, RenderContext &context, const VisibilityList &visible);
	void render_sprites(Vulkan::CommandBuffer &cmd, const vec2 &camera_pos, const vec2 &camera_size, const SpriteList &visible);

private:
	void on_device_created(const Event &e);
	void on_device_destroyed(const Event &e);
	Vulkan::Device *device = nullptr;
	RenderQueue queue;
	ShaderSuite suite[Util::ecast(RenderableType::Count)];
};
}