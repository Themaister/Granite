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
	Count
};

class Renderer : public EventHandler
{
public:
	Renderer();
	void render(Vulkan::CommandBuffer &cmd, RenderContext &context, const VisibilityList &visible);

private:
	void on_device_created(const Event &e);
	void on_device_destroyed(const Event &e);
	Vulkan::Device *device = nullptr;
	RenderQueue queue;
	ShaderSuite suite[Util::ecast(RenderableType::Count)];
};
}