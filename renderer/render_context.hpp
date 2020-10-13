/* Copyright (c) 2017-2020 Hans-Kristian Arntzen
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
#include "application_wsi_events.hpp"

namespace Granite
{

class RenderContext
{
public:
	void set_scene(Scene *scene_)
	{
		scene = scene_;
	}

	void set_camera(const mat4 &projection, const mat4 &view);
	void set_camera(const Camera &camera);

	void set_shadow_cascades(const mat4 cascades[NumShadowCascades]);

	const RenderParameters &get_render_parameters() const
	{
		return camera;
	}

	void set_lighting_parameters(const LightingParameters *lighting_)
	{
		lighting = lighting_;
	}

	const LightingParameters *get_lighting_parameters() const
	{
		return lighting;
	}

	const Frustum &get_visibility_frustum() const
	{
		return frustum;
	}

	Vulkan::Device &get_device() const
	{
		return *device;
	}

	void set_device(Vulkan::Device *device);

private:
	Vulkan::Device *device = nullptr;
	const Scene *scene = nullptr;
	RenderParameters camera;
	const LightingParameters *lighting;
	Frustum frustum;
};
}