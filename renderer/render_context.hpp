/* Copyright (c) 2017 Hans-Kristian Arntzen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

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

	void set_fog_parameters(const FogParameters &fog)
	{
		this->fog = fog;
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