/* Copyright (c) 2017-2024 Hans-Kristian Arntzen
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
#include "mesh_util.hpp"
#include "muglm/matrix_helper.hpp"

namespace Granite
{
void RenderPassSceneRenderer::init(const Setup &setup)
{
	setup_data = setup;
	if (setup_data.flags & SCENE_RENDERER_DEBUG_PROBES_BIT)
		setup_debug_probes();
}

void RenderPassSceneRenderer::setup_debug_probes()
{
	if (setup_data.scene)
		volumetric_diffuse_lights = &setup_data.scene->get_entity_pool().get_component_group<VolumetricDiffuseLightComponent>();
	debug_probe_mesh = Util::make_handle<DebugProbeMesh>();
}

void RenderPassSceneRenderer::resolve_full_motion_vectors(Vulkan::CommandBuffer &cmd, const RenderContext &context) const
{
	cmd.set_input_attachments(0, 0);

	struct UBO
	{
		mat4 reprojection;
		vec2 inv_resolution;
	};

	auto *ubo = cmd.allocate_typed_constant_data<UBO>(1, 0, 1);
	ubo->reprojection =
		context.get_render_parameters().unjittered_prev_view_projection *
		context.get_render_parameters().unjittered_inv_view_projection;
	ubo->inv_resolution = vec2(1.0f / cmd.get_viewport().width, 1.0f / cmd.get_viewport().height);

	Vulkan::CommandBufferUtil::draw_fullscreen_quad(cmd,
	                                                "builtin://shaders/quad.vert",
	                                                "builtin://shaders/reconstruct_mv.frag");
}

void RenderPassSceneRenderer::render_debug_probes(const Renderer &renderer, Vulkan::CommandBuffer &cmd, RenderQueue &queue,
                                                  const RenderContext &context) const
{
	if (!volumetric_diffuse_lights)
		return;

	renderer.begin(queue);

	DebugProbeMeshExtra extra = {};
	RenderInfoComponent info = {};
	info.extra_data = &extra;
	RenderableInfo renderable = {};
	renderable.renderable = debug_probe_mesh.get();
	renderable.transform = &info;

	for (auto &light_tuple : *volumetric_diffuse_lights)
	{
		auto *light = get_component<VolumetricDiffuseLightComponent>(light_tuple);
		auto *view = light->light.get_volume_view();
		if (!view)
			continue;

		uvec3 resolution = light->light.get_resolution();
		float radius = 0.1f;

		for (unsigned z = 0; z < resolution.z; z++)
		{
			for (unsigned y = 0; y < resolution.y; y++)
			{
				for (unsigned x = 0; x < resolution.x; x++)
				{
					extra.tex_coord.x = (float(x) + 0.5f) / float(resolution.x);
					extra.tex_coord.y = (float(y) + 0.5f) / float(resolution.y);
					extra.tex_coord.z = (float(z) + 0.5f) / float(resolution.z);
					extra.probe = view;
					extra.pos.x = dot(light->texture_to_world[0], vec4(extra.tex_coord, 1.0f));
					extra.pos.y = dot(light->texture_to_world[1], vec4(extra.tex_coord, 1.0f));
					extra.pos.z = dot(light->texture_to_world[2], vec4(extra.tex_coord, 1.0f));
					extra.radius = radius;

					queue.push_renderables(context, &renderable, 1);
				}
			}
		}
	}

	renderer.flush(cmd, queue, context, flush_flags);
}

static Renderer::RendererOptionFlags convert_pcf_flags(SceneRendererFlags flags)
{
	if (flags & SCENE_RENDERER_SHADOW_PCF_WIDE_BIT)
		return Renderer::SHADOW_PCF_KERNEL_WIDE_BIT;
	else
		return 0;
}

void RenderPassSceneRenderer::set_extra_flush_flags(Renderer::RendererFlushFlags flags)
{
	flush_flags = flags;
}

static RendererSuite::Type get_depth_renderer_type(SceneRendererFlags flags)
{
	if (flags & SCENE_RENDERER_FALLBACK_DEPTH_BIT)
	{
		return RendererSuite::Type::ShadowDepthDirectionalFallbackPCF;
	}
	else
	{
		return (flags & SCENE_RENDERER_SHADOW_VSM_BIT) != 0 ?
		       RendererSuite::Type::ShadowDepthDirectionalVSM :
		       RendererSuite::Type::ShadowDepthDirectionalPCF;
	}
}

void RenderPassSceneRenderer::prepare_render_pass()
{
	prepare_setup_queues();

	auto &visible = visible_per_task[0];
	auto &visible_transparent = visible_per_task_transparent[0];
	auto *context = setup_data.context;
	auto &frustum = context->get_visibility_frustum();
	auto *scene = setup_data.scene;

	auto &queue_transparent = queue_per_task_transparent[0];
	auto &queue_opaque = queue_per_task_opaque[0];
	auto &queue_depth = queue_per_task_depth[0];

	if (setup_data.flags & (SCENE_RENDERER_FORWARD_OPAQUE_BIT | SCENE_RENDERER_FORWARD_Z_PREPASS_BIT))
	{
		scene->gather_visible_render_pass_sinks(context->get_render_parameters().camera_position, visible);
		scene->gather_visible_opaque_renderables(frustum, visible);
		if ((setup_data.flags & SCENE_RENDERER_SKIP_OPAQUE_FLOATING_BIT) == 0)
			scene->gather_opaque_floating_renderables(visible);

		if (setup_data.flags & SCENE_RENDERER_FORWARD_Z_PREPASS_BIT)
			queue_depth.push_depth_renderables(*context, visible.data(), visible.size());

		if (setup_data.flags & SCENE_RENDERER_FORWARD_OPAQUE_BIT)
		{
			if ((setup_data.flags & SCENE_RENDERER_SKIP_UNBOUNDED_BIT) == 0)
				setup_data.scene->gather_unbounded_renderables(visible);
			queue_opaque.push_renderables(*context, visible.data(), visible.size());
		}
	}

	if (setup_data.flags & SCENE_RENDERER_MOTION_VECTOR_BIT)
		queue_opaque.push_motion_vector_renderables(*context, visible.data(), visible.size());

	if (setup_data.flags & SCENE_RENDERER_DEFERRED_GBUFFER_BIT)
	{
		scene->gather_visible_render_pass_sinks(context->get_render_parameters().camera_position, visible);
		if ((setup_data.flags & SCENE_RENDERER_SKIP_OPAQUE_FLOATING_BIT) == 0)
			scene->gather_opaque_floating_renderables(visible);
		if ((setup_data.flags & SCENE_RENDERER_SKIP_UNBOUNDED_BIT) == 0)
			scene->gather_unbounded_renderables(visible);
		scene->gather_visible_opaque_renderables(frustum, visible);
		queue_opaque.push_renderables(*context, visible.data(), visible.size());
	}

	if (setup_data.flags & SCENE_RENDERER_FORWARD_TRANSPARENT_BIT)
	{
		scene->gather_visible_transparent_renderables(frustum, visible_transparent);
		queue_transparent.push_renderables(*context, visible_transparent.data(), visible_transparent.size());
	}

	if (setup_data.flags & SCENE_RENDERER_DEPTH_BIT)
	{
		if (setup_data.flags & SCENE_RENDERER_DEPTH_DYNAMIC_BIT)
			scene->gather_visible_dynamic_shadow_renderables(frustum, visible);
		if (setup_data.flags & SCENE_RENDERER_DEPTH_STATIC_BIT)
			scene->gather_visible_static_shadow_renderables(frustum, visible);
		queue_depth.push_depth_renderables(*context, visible.data(), visible.size());
	}
}

void RenderPassSceneRenderer::prepare_setup_queues()
{
	auto *suite = setup_data.suite;
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
		auto type = get_depth_renderer_type(setup_data.flags);
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
	else if (setup_data.flags & SCENE_RENDERER_MOTION_VECTOR_BIT)
	{
		for (auto &queue : queue_per_task_opaque)
			suite->get_renderer(RendererSuite::Type::MotionVector).begin(queue);
	}

	if (setup_data.flags & SCENE_RENDERER_FORWARD_TRANSPARENT_BIT)
	{
		for (auto &queue : queue_per_task_transparent)
			suite->get_renderer(RendererSuite::Type::ForwardTransparent).begin(queue);
	}
}

void RenderPassSceneRenderer::enqueue_prepare_render_pass(RenderGraph &, TaskComposer &composer)
{
	auto &setup_group = composer.begin_pipeline_stage();
	setup_group.enqueue_task([this]() {
		prepare_setup_queues();
	});

	if (setup_data.flags & (SCENE_RENDERER_FORWARD_OPAQUE_BIT |
	                        SCENE_RENDERER_FORWARD_Z_PREPASS_BIT |
	                        SCENE_RENDERER_MOTION_VECTOR_BIT))
	{
		{
			auto &group = composer.begin_pipeline_stage();
			group.enqueue_task([this]() {
				setup_data.scene->gather_visible_render_pass_sinks(
						setup_data.context->get_render_parameters().camera_position,
						visible_per_task[0]);
				if ((setup_data.flags & SCENE_RENDERER_SKIP_OPAQUE_FLOATING_BIT) == 0)
					setup_data.scene->gather_opaque_floating_renderables(visible_per_task[0]);
			});
		}

		if (setup_data.flags & (SCENE_RENDERER_FORWARD_OPAQUE_BIT | SCENE_RENDERER_FORWARD_Z_PREPASS_BIT))
			Threaded::scene_gather_opaque_renderables(*setup_data.scene, composer, setup_data.context->get_visibility_frustum(), visible_per_task, MaxTasks);
		else if (setup_data.flags & SCENE_RENDERER_MOTION_VECTOR_BIT)
			Threaded::scene_gather_motion_vector_renderables(*setup_data.scene, composer, setup_data.context->get_visibility_frustum(), visible_per_task, MaxTasks);

		if (setup_data.flags & SCENE_RENDERER_FORWARD_Z_PREPASS_BIT)
		{
			Threaded::compose_parallel_push_renderables(composer, *setup_data.context, queue_per_task_depth,
			                                            visible_per_task, MaxTasks,
			                                            Threaded::PushType::Depth);
		}

		if (setup_data.flags & SCENE_RENDERER_FORWARD_OPAQUE_BIT)
		{
			if ((setup_data.flags & SCENE_RENDERER_SKIP_UNBOUNDED_BIT) == 0)
			{
				auto &group = composer.begin_pipeline_stage();
				group.enqueue_task([this]() {
					setup_data.scene->gather_unbounded_renderables(visible_per_task[0]);
				});
			}
			Threaded::compose_parallel_push_renderables(composer, *setup_data.context, queue_per_task_opaque,
			                                            visible_per_task, MaxTasks,
			                                            Threaded::PushType::Normal);
		}
		else if (setup_data.flags & SCENE_RENDERER_MOTION_VECTOR_BIT)
		{
			Threaded::compose_parallel_push_renderables(composer, *setup_data.context, queue_per_task_opaque,
			                                            visible_per_task, MaxTasks,
			                                            Threaded::PushType::MotionVector);
		}
	}

	if (setup_data.flags & SCENE_RENDERER_DEFERRED_GBUFFER_BIT)
	{
		{
			auto &group = composer.begin_pipeline_stage();
			group.enqueue_task([this]() {
				setup_data.scene->gather_visible_render_pass_sinks(setup_data.context->get_render_parameters().camera_position, visible_per_task[0]);
				if ((setup_data.flags & SCENE_RENDERER_SKIP_OPAQUE_FLOATING_BIT) == 0)
					setup_data.scene->gather_opaque_floating_renderables(visible_per_task[0]);
				if ((setup_data.flags & SCENE_RENDERER_SKIP_UNBOUNDED_BIT) == 0)
					setup_data.scene->gather_unbounded_renderables(visible_per_task[0]);
			});
		}
		Threaded::scene_gather_opaque_renderables(*setup_data.scene, composer, setup_data.context->get_visibility_frustum(), visible_per_task, MaxTasks);
		Threaded::compose_parallel_push_renderables(composer, *setup_data.context, queue_per_task_opaque,
		                                            visible_per_task, MaxTasks,
		                                            Threaded::PushType::Normal);
	}

	if (setup_data.flags & SCENE_RENDERER_FORWARD_TRANSPARENT_BIT)
	{
		Threaded::scene_gather_transparent_renderables(*setup_data.scene, composer, setup_data.context->get_visibility_frustum(), visible_per_task_transparent, MaxTasks);
		Threaded::compose_parallel_push_renderables(composer, *setup_data.context, queue_per_task_transparent,
		                                            visible_per_task_transparent, MaxTasks,
		                                            Threaded::PushType::Normal);
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

		Threaded::compose_parallel_push_renderables(composer, *setup_data.context, queue_per_task_depth,
		                                            visible_per_task, MaxTasks,
		                                            Threaded::PushType::Depth);
	}
}

void RenderPassSceneRenderer::build_render_pass_inner(Vulkan::CommandBuffer &cmd) const
{
	auto *suite = setup_data.suite;

	if (setup_data.flags & (SCENE_RENDERER_FORWARD_OPAQUE_BIT | SCENE_RENDERER_FORWARD_Z_PREPASS_BIT))
	{
		if (setup_data.flags & SCENE_RENDERER_FORWARD_Z_PREPASS_BIT)
		{
			suite->get_renderer(RendererSuite::Type::PrepassDepth).flush(cmd, queue_per_task_depth[0], *setup_data.context,
			                                                             Renderer::NO_COLOR_BIT | Renderer::SKIP_SORTING_BIT |
			                                                             flush_flags);
		}

		if (setup_data.flags & SCENE_RENDERER_FORWARD_OPAQUE_BIT)
		{
			Renderer::RendererOptionFlags opt = Renderer::SKIP_SORTING_BIT | flush_flags;
			if (setup_data.flags & (SCENE_RENDERER_FORWARD_Z_PREPASS_BIT | SCENE_RENDERER_FORWARD_Z_EXISTING_PREPASS_BIT))
				opt |= Renderer::DEPTH_STENCIL_READ_ONLY_BIT | Renderer::DEPTH_TEST_EQUAL_BIT;
			suite->get_renderer(RendererSuite::Type::ForwardOpaque).flush(cmd, queue_per_task_opaque[0], *setup_data.context, opt);

			if (setup_data.flags & SCENE_RENDERER_DEBUG_PROBES_BIT)
			{
				render_debug_probes(suite->get_renderer(RendererSuite::Type::ForwardOpaque), cmd,
				                    queue_non_tasked,
				                    *setup_data.context);
			}
		}
	}

	if (setup_data.flags & SCENE_RENDERER_MOTION_VECTOR_BIT)
	{
		if (setup_data.flags & SCENE_RENDERER_MOTION_VECTOR_FULL_BIT)
			resolve_full_motion_vectors(cmd, *setup_data.context);

		Renderer::RendererOptionFlags opt = Renderer::SKIP_SORTING_BIT |
		                                    Renderer::DEPTH_STENCIL_READ_ONLY_BIT |
		                                    Renderer::DEPTH_TEST_EQUAL_BIT |
		                                    flush_flags;
		suite->get_renderer(RendererSuite::Type::MotionVector).flush(cmd, queue_per_task_opaque[0], *setup_data.context, opt);
	}

	if (setup_data.flags & SCENE_RENDERER_DEFERRED_GBUFFER_BIT)
	{
		suite->get_renderer(RendererSuite::Type::Deferred).flush(cmd, queue_per_task_opaque[0], *setup_data.context,
		                                                         Renderer::SKIP_SORTING_BIT | flush_flags);

		if (setup_data.flags & SCENE_RENDERER_DEBUG_PROBES_BIT)
		{
			render_debug_probes(suite->get_renderer(RendererSuite::Type::Deferred), cmd,
			                    queue_non_tasked,
			                    *setup_data.context);
		}
	}

	if (setup_data.flags & SCENE_RENDERER_DEFERRED_GBUFFER_LIGHT_PREPASS_BIT)
		setup_data.deferred_lights->render_prepass_lights(cmd, queue_non_tasked, *setup_data.context);

	if (setup_data.flags & SCENE_RENDERER_DEFERRED_LIGHTING_BIT)
	{
		if (!(setup_data.flags & SCENE_RENDERER_DEFERRED_CLUSTER_BIT))
			setup_data.deferred_lights->render_lights(cmd, queue_non_tasked, *setup_data.context);
		DeferredLightRenderer::render_light(cmd, *setup_data.context, convert_pcf_flags(setup_data.flags) | flush_flags);
	}

	if (setup_data.flags & SCENE_RENDERER_FORWARD_TRANSPARENT_BIT)
	{
		suite->get_renderer(RendererSuite::Type::ForwardTransparent).flush(cmd, queue_per_task_transparent[0], *setup_data.context,
		                                                                   Renderer::DEPTH_STENCIL_READ_ONLY_BIT | Renderer::SKIP_SORTING_BIT |
		                                                                   flush_flags);
	}

	if (setup_data.flags & SCENE_RENDERER_DEPTH_BIT)
	{
		auto type = get_depth_renderer_type(setup_data.flags);
		suite->get_renderer(type).flush(cmd, queue_per_task_depth[0], *setup_data.context,
		                                Renderer::DEPTH_BIAS_BIT | Renderer::SKIP_SORTING_BIT | flush_flags);
	}
}

void RenderPassSceneRenderer::build_render_pass(Vulkan::CommandBuffer &cmd) const
{
	build_render_pass_inner(cmd);
}

void RenderPassSceneRenderer::build_render_pass(Vulkan::CommandBuffer &cmd)
{
	build_render_pass_inner(cmd);
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
