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
	DebugMesh,
	Skybox,
	Sprite,
	Count
};

class Renderer : public EventHandler
{
public:
	Renderer();

	void begin();
	void push_renderables(RenderContext &context, const VisibilityList &visible);
	void flush(Vulkan::CommandBuffer &cmd, RenderContext &context);

	void render_debug_aabb(RenderContext &context, const AABB &aabb, const vec4 &color);
	void render_debug_frustum(RenderContext &context, const Frustum &frustum, const vec4 &color);

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

	DebugMeshInfo &render_debug(RenderContext &context, const AABB &aabb, unsigned count);
};
}