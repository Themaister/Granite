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

class Renderer : public EventHandler
{
public:
	Renderer();
	void render(Vulkan::CommandBuffer &cmd, RenderContext &context, const VisibilityList &visible);

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