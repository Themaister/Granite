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
#include "threaded_scene.hpp"

namespace Granite
{
void RenderPassSceneRenderer::init(const Setup &setup)
{
	setup_data = setup;
}

static Renderer::RendererOptionFlags convert_pcf_flags(SceneRendererFlags flags)
{
	if (flags & SCENE_RENDERER_SHADOW_PCF_3X_BIT)
		return Renderer::SHADOW_PCF_KERNEL_WIDTH_3_BIT;
	else if (flags & SCENE_RENDERER_SHADOW_PCF_5X_BIT)
		return Renderer::SHADOW_PCF_KERNEL_WIDTH_5_BIT;
	else
		return 0;
}

void RenderPassSceneRenderer::enqueue_prepare_render_pass(TaskComposer &composer, const Vulkan::RenderPassInfo &,
                                                          unsigned, VkSubpassContents &contents)
{
	auto *suite = setup_data.suite;
	contents = VK_SUBPASS_CONTENTS_INLINE;
	for (auto &visible : visible_per_task)
		visible.clear();
	for (auto &visible : visible_per_task_transparent)
		visible.clear();

	// Setup renderer options in main thread.
	if (setup_data.flags & SCENE_RENDERER_FORWARD_Z_PREPASS_BIT)
	{
		for (auto &queue : queue_per_task_depth)
			suite->get_renderer(RendererSuite::Type::PrepassDepth).begin(queue);
	}
	else if (setup_data.flags & SCENE_RENDERER_DEPTH_BIT)
	{
		auto type = (setup_data.flags & SCENE_RENDERER_SHADOW_VSM_BIT) != 0 ?
				RendererSuite::Type::ShadowDepthDirectionalVSM :
				RendererSuite::Type::ShadowDepthDirectionalPCF;
		for (auto &queue : queue_per_task_depth)
			suite->get_renderer(type).begin(queue);
	}

	if (setup_data.flags & SCENE_RENDERER_FORWARD_OPAQUE_BIT)
	{
		for (auto &queue : queue_per_task_opaque)
			suite->get_renderer(RendererSuite::Type::ForwardOpaque).begin(queue);
	}
	else if (setup_data.flags & SCENE_RENDERER_DEFERRED_GBUFFER_BIT)
	{
		for (auto &queue : queue_per_task_opaque)
			suite->get_renderer(RendererSuite::Type::Deferred).begin(queue);
	}

	if (setup_data.flags & SCENE_RENDERER_FORWARD_TRANSPARENT_BIT)
	{
		for (auto &queue : queue_per_task_transparent)
			suite->get_renderer(RendererSuite::Type::ForwardTransparent).begin(queue);
	}

	if (setup_data.flags & (SCENE_RENDERER_FORWARD_OPAQUE_BIT | SCENE_RENDERER_FORWARD_Z_PREPASS_BIT))
	{
		{
			auto &group = composer.begin_pipeline_stage();
			group.enqueue_task([this]() {
				setup_data.scene->gather_visible_render_pass_sinks(
						setup_data.context->get_render_parameters().camera_position,
						visible_per_task[0]);
			});
		}
		Threaded::scene_gather_opaque_renderables(*setup_data.scene, composer, setup_data.context->get_visibility_frustum(), visible_per_task, MaxTasks);

		if (setup_data.flags & SCENE_RENDERER_FORWARD_Z_PREPASS_BIT)
			Threaded::compose_parallel_push_renderables(composer, *setup_data.context, queue_per_task_depth, visible_per_task, MaxTasks);

		if (setup_data.flags & SCENE_RENDERER_FORWARD_OPAQUE_BIT)
		{
			{
				auto &group = composer.begin_pipeline_stage();
				group.enqueue_task([this]() {
					setup_data.scene->gather_unbounded_renderables(visible_per_task[0]);
				});
			}
			Threaded::compose_parallel_push_renderables(composer, *setup_data.context, queue_per_task_opaque, visible_per_task, MaxTasks);
		}
	}

	if (setup_data.flags & SCENE_RENDERER_DEFERRED_GBUFFER_BIT)
	{
		{
			auto &group = composer.begin_pipeline_stage();
			group.enqueue_task([this]() {
				setup_data.scene->gather_visible_render_pass_sinks(setup_data.context->get_render_parameters().camera_position, visible_per_task[0]);
				setup_data.scene->gather_unbounded_renderables(visible_per_task[0]);
			});
		}
		Threaded::scene_gather_opaque_renderables(*setup_data.scene, composer, setup_data.context->get_visibility_frustum(), visible_per_task, MaxTasks);
		Threaded::compose_parallel_push_renderables(composer, *setup_data.context, queue_per_task_opaque, visible_per_task, MaxTasks);
	}

	if (setup_data.flags & SCENE_RENDERER_FORWARD_TRANSPARENT_BIT)
	{
		Threaded::scene_gather_transparent_renderables(*setup_data.scene, composer, setup_data.context->get_visibility_frustum(), visible_per_task_transparent, MaxTasks);
		Threaded::compose_parallel_push_renderables(composer, *setup_data.context, queue_per_task_transparent, visible_per_task_transparent, MaxTasks);
	}

	if (setup_data.flags & SCENE_RENDERER_DEPTH_BIT)
	{
		if (setup_data.flags & SCENE_RENDERER_DEPTH_DYNAMIC_BIT)
		{
			Threaded::scene_gather_dynamic_shadow_renderables(*setup_data.scene, composer,
			                                                  setup_data.context->get_visibility_frustum(),
			                                                  visible_per_task, nullptr, MaxTasks);
		}

		if (setup_data.flags & SCENE_RENDERER_DEPTH_STATIC_BIT)
		{
			Threaded::scene_gather_static_shadow_renderables(*setup_data.scene, composer,
			                                                 setup_data.context->get_visibility_frustum(),
			                                                 visible_per_task, nullptr, MaxTasks);
		}

		Threaded::compose_parallel_push_renderables(composer, *setup_data.context, queue_per_task_depth, visible_per_task, MaxTasks);
	}
}

void RenderPassSceneRenderer::build_render_pass(Vulkan::CommandBuffer &cmd)
{
	auto *suite = setup_data.suite;

	if (setup_data.flags & (SCENE_RENDERER_FORWARD_OPAQUE_BIT | SCENE_RENDERER_FORWARD_Z_PREPASS_BIT))
	{
		if (setup_data.flags & SCENE_RENDERER_FORWARD_Z_PREPASS_BIT)
		{
			suite->get_renderer(RendererSuite::Type::PrepassDepth).flush(cmd, queue_per_task_depth[0], *setup_data.context,
			                                                             Renderer::NO_COLOR_BIT | Renderer::SKIP_SORTING_BIT);
		}

		if (setup_data.flags & SCENE_RENDERER_FORWARD_OPAQUE_BIT)
		{
			Renderer::RendererOptionFlags opt = Renderer::SKIP_SORTING_BIT;
			if (setup_data.flags & (SCENE_RENDERER_FORWARD_Z_PREPASS_BIT | SCENE_RENDERER_FORWARD_Z_EXISTING_PREPASS_BIT))
				opt |= Renderer::DEPTH_STENCIL_READ_ONLY_BIT | Renderer::DEPTH_TEST_EQUAL_BIT;
			suite->get_renderer(RendererSuite::Type::ForwardOpaque).flush(cmd, queue_per_task_opaque[0], *setup_data.context, opt);
		}
	}

	if (setup_data.flags & SCENE_RENDERER_DEFERRED_GBUFFER_BIT)
		suite->get_renderer(RendererSuite::Type::Deferred).flush(cmd, queue_per_task_opaque[0], *setup_data.context, Renderer::SKIP_SORTING_BIT);

	if (setup_data.flags & SCENE_RENDERER_DEFERRED_GBUFFER_LIGHT_PREPASS_BIT)
		setup_data.deferred_lights->render_prepass_lights(cmd, queue_non_tasked, *setup_data.context);

	if (setup_data.flags & SCENE_RENDERER_DEFERRED_LIGHTING_BIT)
	{
		if (!(setup_data.flags & SCENE_RENDERER_DEFERRED_CLUSTER_BIT))
			setup_data.deferred_lights->render_lights(cmd, queue_non_tasked, *setup_data.context);
		DeferredLightRenderer::render_light(cmd, *setup_data.context, convert_pcf_flags(setup_data.flags));
	}

	if (setup_data.flags & SCENE_RENDERER_FORWARD_TRANSPARENT_BIT)
	{
		suite->get_renderer(RendererSuite::Type::ForwardTransparent).flush(cmd, queue_per_task_transparent[0], *setup_data.context,
		                                                                   Renderer::DEPTH_STENCIL_READ_ONLY_BIT | Renderer::SKIP_SORTING_BIT);
	}

	if (setup_data.flags & SCENE_RENDERER_DEPTH_BIT)
	{
		auto type = (setup_data.flags & SCENE_RENDERER_SHADOW_VSM_BIT) != 0 ?
		            RendererSuite::Type::ShadowDepthDirectionalVSM :
		            RendererSuite::Type::ShadowDepthDirectionalPCF;
		suite->get_renderer(type).flush(cmd, queue_per_task_depth[0], *setup_data.context,
		                                Renderer::DEPTH_BIAS_BIT | Renderer::SKIP_SORTING_BIT);
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
}
