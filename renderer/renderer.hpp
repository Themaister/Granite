#pragma once

#include "vulkan_events.hpp"
#include "scene.hpp"
#include "shader_suite.hpp"

namespace Granite
{
enum class RenderableType
{
	Mesh,
	Skybox,
	Quad,
	Count
};

struct SpriteInfo
{
	AbstractRenderableHandle sprite;
	vec3 position;
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