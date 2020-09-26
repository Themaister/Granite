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
void RenderPassSceneRenderer::init(const Setup &setup)
{
	setup_data = setup;
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
	if (setup_data.flags & (SCENE_RENDERER_FORWARD_OPAQUE_BIT | SCENE_RENDERER_FORWARD_Z_PREPASS_BIT))
	{
		visible.clear();

		if (setup_data.flags & (SCENE_RENDERER_FORWARD_OPAQUE_BIT | SCENE_RENDERER_FORWARD_Z_PREPASS_BIT))
		{
			setup_data.scene->gather_visible_opaque_renderables(setup_data.context->get_visibility_frustum(), visible);
			setup_data.scene->gather_visible_render_pass_sinks(setup_data.context->get_render_parameters().camera_position, visible);
		}

		if (setup_data.flags & SCENE_RENDERER_FORWARD_Z_PREPASS_BIT)
		{
			setup_data.depth->begin(queue);
			setup_data.depth->push_renderables(queue, *setup_data.context, visible);
			setup_data.depth->flush(cmd, queue, *setup_data.context, Renderer::NO_COLOR_BIT);
		}

		if (setup_data.flags & SCENE_RENDERER_FORWARD_OPAQUE_BIT)
		{
			setup_data.scene->gather_unbounded_renderables(visible);

			setup_data.forward->set_mesh_renderer_options_from_lighting(*setup_data.context->get_lighting_parameters());
			setup_data.forward->set_mesh_renderer_options(
					setup_data.forward->get_mesh_renderer_options() |
					convert_pcf_flags(setup_data.flags) |
					((setup_data.flags & SCENE_RENDERER_FORWARD_Z_PREPASS_BIT) ? Renderer::ALPHA_TEST_DISABLE_BIT : 0));
			setup_data.forward->begin(queue);
			setup_data.forward->push_renderables(queue, *setup_data.context, visible);

			Renderer::RendererOptionFlags opt = 0;
			if (setup_data.flags & SCENE_RENDERER_FORWARD_Z_PREPASS_BIT)
				opt |= Renderer::DEPTH_STENCIL_READ_ONLY_BIT | Renderer::DEPTH_TEST_EQUAL_BIT;

			setup_data.forward->flush(cmd, queue, *setup_data.context, opt);
		}
	}

	if (setup_data.flags & SCENE_RENDERER_DEFERRED_GBUFFER_BIT)
	{
		visible.clear();
		setup_data.scene->gather_visible_opaque_renderables(setup_data.context->get_visibility_frustum(), visible);
		setup_data.scene->gather_visible_render_pass_sinks(setup_data.context->get_render_parameters().camera_position, visible);
		setup_data.scene->gather_unbounded_renderables(visible);
		setup_data.deferred->begin(queue);
		setup_data.deferred->push_renderables(queue, *setup_data.context, visible);
		setup_data.deferred->flush(cmd, queue, *setup_data.context);
	}

	if (setup_data.flags & SCENE_RENDERER_DEFERRED_GBUFFER_LIGHT_PREPASS_BIT)
		setup_data.deferred_lights->render_prepass_lights(cmd, *setup_data.context);

	if (setup_data.flags & SCENE_RENDERER_DEFERRED_LIGHTING_BIT)
	{
		if (!(setup_data.flags & SCENE_RENDERER_DEFERRED_CLUSTER_BIT))
			setup_data.deferred_lights->render_lights(cmd, *setup_data.context, convert_pcf_flags(setup_data.flags));
		DeferredLightRenderer::render_light(cmd, *setup_data.context, convert_pcf_flags(setup_data.flags));
	}

	if (setup_data.flags & SCENE_RENDERER_FORWARD_TRANSPARENT_BIT)
	{
		visible.clear();
		setup_data.scene->gather_visible_transparent_renderables(setup_data.context->get_visibility_frustum(), visible);
		setup_data.forward->set_mesh_renderer_options_from_lighting(*setup_data.context->get_lighting_parameters());
		setup_data.forward->set_mesh_renderer_options(
				setup_data.forward->get_mesh_renderer_options() | convert_pcf_flags(setup_data.flags));
		setup_data.forward->begin(queue);
		setup_data.forward->push_renderables(queue, *setup_data.context, visible);
		setup_data.forward->flush(cmd, queue, *setup_data.context, Renderer::DEPTH_STENCIL_READ_ONLY_BIT);
	}

	if (setup_data.flags & SCENE_RENDERER_DEPTH_BIT)
	{
		visible.clear();
		setup_data.depth->set_mesh_renderer_options((setup_data.flags & SCENE_RENDERER_DEPTH_VSM_BIT) ? Renderer::SHADOW_VSM_BIT : 0);
		setup_data.depth->begin(queue);
		if (setup_data.flags & SCENE_RENDERER_DEPTH_DYNAMIC_BIT)
			setup_data.scene->gather_visible_dynamic_shadow_renderables(setup_data.context->get_visibility_frustum(), visible);
		if (setup_data.flags & SCENE_RENDERER_DEPTH_STATIC_BIT)
			setup_data.scene->gather_visible_static_shadow_renderables(setup_data.context->get_visibility_frustum(), visible);
		setup_data.depth->push_depth_renderables(queue, *setup_data.context, visible);
		setup_data.depth->flush(cmd, queue, *setup_data.context, Renderer::DEPTH_BIAS_BIT);
	}
}

void RenderPassSceneRenderer::set_clear_color(const VkClearColorValue &value)
{
	clear_color_value = value;
}

bool RenderPassSceneRenderer::get_clear_color(unsigned, VkClearColorValue *value) const
{
	if (value)
		*value = clear_color_value;
	return true;
}

bool RenderPassSceneRenderer::render_pass_can_multithread() const
{
	return true;
}

void RenderPassSceneRendererConditional::set_need_render_pass(bool need_)
{
	need = need_;
}

bool RenderPassSceneRendererConditional::need_render_pass() const
{
	return need;
}

bool RenderPassSceneRendererConditional::render_pass_is_conditional() const
{
	return true;
}
}
