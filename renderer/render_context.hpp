#pragma once

#include "math.hpp"
#include "scene.hpp"
#include "render_queue.hpp"
#include "camera.hpp"
#include "render_parameters.hpp"
#include "frustum.hpp"

namespace Granite
{

class RenderContext
{
public:
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

	const Frustum &get_visibility_frustum() const
	{
		return frustum;
	}

private:
	Scene *scene = nullptr;
	RenderQueue *queue = nullptr;
	RenderParameters camera;
	Frustum frustum;
};
}