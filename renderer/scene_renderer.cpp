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

#include "scene_renderer.hpp"

namespace Granite
{
void RenderPassSceneRenderer::set_context(const RenderContext *context_)
{
	context = context_;
}

void RenderPassSceneRenderer::set_scene(Scene *scene_)
{
	scene = scene_;
}

void RenderPassSceneRenderer::set_renderer(Renderer *forward, Renderer *deferred, Renderer *depth)
{
	forward_renderer = forward;
	deferred_renderer = deferred;
	depth_renderer = depth;
}

void RenderPassSceneRenderer::set_flags(SceneRendererFlags flags_)
{
	flags = flags_;
}

void RenderPassSceneRenderer::set_deferred_lights(DeferredLights *lights)
{
	deferred_lights = lights;
}

static Renderer::RendererOptionFlags convert_pcf_flags(SceneRendererFlags flags)
{
	if (flags & SCENE_RENDERER_PCF_3X_BIT)
		return Renderer::SHADOW_PCF_KERNEL_WIDTH_3_BIT;
	else if (flags & SCENE_RENDERER_PCF_5X_BIT)
		return Renderer::SHADOW_PCF_KERNEL_WIDTH_5_BIT;
	else
		return 0;
}

void RenderPassSceneRenderer::build_render_pass(Vulkan::CommandBuffer &cmd)
{
	visible.clear();

	if (flags & (SCENE_RENDERER_FORWARD_OPAQUE_BIT | SCENE_RENDERER_FORWARD_Z_PREPASS_BIT))
	{
		if (flags & (SCENE_RENDERER_FORWARD_OPAQUE_BIT | SCENE_RENDERER_FORWARD_Z_PREPASS_BIT))
		{
			scene->gather_visible_opaque_renderables(context->get_visibility_frustum(), visible);
			scene->gather_visible_render_pass_sinks(context->get_render_parameters().camera_position, visible);
		}

		if (flags & SCENE_RENDERER_FORWARD_Z_PREPASS_BIT)
		{
			depth_renderer->begin();
			depth_renderer->push_renderables(*context, visible);
			depth_renderer->flush(cmd, *context, Renderer::NO_COLOR_BIT);
		}

		if (flags & SCENE_RENDERER_FORWARD_OPAQUE_BIT)
		{
			scene->gather_unbounded_renderables(visible);

			forward_renderer->set_mesh_renderer_options_from_lighting(*context->get_lighting_parameters());
			forward_renderer->set_mesh_renderer_options(
					forward_renderer->get_mesh_renderer_options() |
					convert_pcf_flags(flags) |
					((flags & SCENE_RENDERER_FORWARD_Z_PREPASS_BIT) ? Renderer::ALPHA_TEST_DISABLE_BIT : 0));
			forward_renderer->begin();
			forward_renderer->push_renderables(*context, visible);

			Renderer::RendererOptionFlags opt = 0;
			if (flags & SCENE_RENDERER_FORWARD_Z_PREPASS_BIT)
				opt |= Renderer::DEPTH_STENCIL_READ_ONLY_BIT | Renderer::DEPTH_TEST_EQUAL_BIT;

			forward_renderer->flush(cmd, *context, opt);
		}
	}

	if (flags & SCENE_RENDERER_DEFERRED_GBUFFER_BIT)
	{
		scene->gather_visible_opaque_renderables(context->get_visibility_frustum(), visible);
		scene->gather_visible_render_pass_sinks(context->get_render_parameters().camera_position, visible);
		scene->gather_unbounded_renderables(visible);
		deferred_renderer->begin();
		deferred_renderer->push_renderables(*context, visible);
		deferred_renderer->flush(cmd, *context);
	}

	if (flags & SCENE_RENDERER_DEFERRED_GBUFFER_LIGHT_PREPASS_BIT)
		deferred_lights->render_prepass_lights(cmd, *context);

	if (flags & SCENE_RENDERER_DEFERRED_LIGHTING_BIT)
	{
		if (!(flags & SCENE_RENDERER_DEFERRED_CLUSTER_BIT))
			deferred_lights->render_lights(cmd, *context, convert_pcf_flags(flags));
		DeferredLightRenderer::render_light(cmd, *context, convert_pcf_flags(flags));
	}

	if (flags & SCENE_RENDERER_FORWARD_TRANSPARENT_BIT)
	{
		visible.clear();
		scene->gather_visible_transparent_renderables(context->get_visibility_frustum(), visible);
		forward_renderer->set_mesh_renderer_options_from_lighting(*context->get_lighting_parameters());
		forward_renderer->set_mesh_renderer_options(
				forward_renderer->get_mesh_renderer_options() | convert_pcf_flags(flags));
		forward_renderer->begin();
		forward_renderer->push_renderables(*context, visible);
		forward_renderer->flush(cmd, *context, Renderer::DEPTH_STENCIL_READ_ONLY_BIT);
	}
}
}
