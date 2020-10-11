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

#include "simple_renderer.hpp"
#include "muglm/muglm_impl.hpp"

namespace Granite
{
SimpleRenderer::SimpleRenderer(const ShaderSuiteResolver *resolver)
	: renderer(RendererType::GeneralForward, resolver)
{
	lighting.directional.color = vec3(1.0f);
	lighting.directional.direction = vec3(0.0f, 1.0f, 0.0f);
	renderer.set_mesh_renderer_options_from_lighting(lighting);
}

void SimpleRenderer::set_directional_light_color(const vec3 &color)
{
	lighting.directional.color = color;
}

void SimpleRenderer::set_directional_light_direction(const vec3 &direction)
{
	lighting.directional.direction = normalize(direction);
}

void SimpleRenderer::render_scene(const Camera &camera, Scene &scene, Vulkan::CommandBuffer &cmd)
{
	scene.update_all_transforms();
	render_context.set_camera(camera);
	render_context.set_lighting_parameters(&lighting);

	visible.clear();
	scene.gather_unbounded_renderables(visible);
	scene.gather_visible_opaque_renderables(render_context.get_visibility_frustum(), visible);
	scene.gather_visible_transparent_renderables(render_context.get_visibility_frustum(), visible);
	renderer.begin(queue);
	queue.push_renderables(render_context, visible);
	renderer.flush(cmd, queue, render_context);
}
}
