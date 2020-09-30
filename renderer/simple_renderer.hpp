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
#include "renderer.hpp"
#include "math.hpp"
#include "command_buffer.hpp"
#include "render_context.hpp"

// A trivial renderer which makes small prototypes easier to get working.

namespace Granite
{
class Scene;
class Camera;

class SimpleRenderer
{
public:
	explicit SimpleRenderer(const ShaderSuiteResolver *resolver = nullptr);

	void set_directional_light_color(const vec3 &color);
	void set_directional_light_direction(const vec3 &direction);
	void render_scene(const Camera &camera, Scene &scene, Vulkan::CommandBuffer &cmd);

private:
	Renderer renderer;
	LightingParameters lighting;
	RenderContext render_context;
	VisibilityList visible;
	RenderQueue queue;
};
}
