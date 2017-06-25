#pragma once

#include "math.hpp"
#include "scene.hpp"
#include "render_queue.hpp"
#include "camera.hpp"
#include "render_parameters.hpp"
#include "frustum.hpp"
#include "device.hpp"
#include "vulkan_events.hpp"

namespace Granite
{

class RenderContext : public EventHandler
{
public:
	RenderContext();

	void set_scene(Scene *scene)
	{
		this->scene = scene;
	}

	void set_queue(RenderQueue *queue)
	{
		this->queue = queue;
	}

	void set_camera(const mat4 &projection, const mat4 &view);
	void set_camera(const Camera &camera);

	const RenderParameters &get_render_parameters() const
	{
		return camera;
	}

	const FogParameters &get_fog_parameters() const
	{
		return fog;
	}

	const Frustum &get_visibility_frustum() const
	{
		return frustum;
	}

	Vulkan::Device &get_device() const
	{
		return *device;
	}

private:
	void on_device_created(const Event &e);
	void on_device_destroyed(const Event &e);
	Vulkan::Device *device = nullptr;
	Scene *scene = nullptr;
	RenderQueue *queue = nullptr;
	RenderParameters camera;
	FogParameters fog;
	Frustum frustum;
};
}