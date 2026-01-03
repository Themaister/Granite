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

	VK_ASSERT(setup.layers <= MaxTasks);
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

	// Only fixed function meshlets should be renderered here.
	if ((flush_flags & Renderer::MESH_ASSET_PHASE_1_BIT) &&
	    !(setup_data.flags & SCENE_RENDERER_MOTION_VECTOR_BIT))
		return;

	auto &visible = visible_per_task[0];
	auto &visible_transparent = visible_per_task_transparent[0];
	auto *context = setup_data.context;
	auto &frustum = context->get_visibility_frustum();
	auto *scene = setup_data.scene;

	auto &queue_transparent = queue_per_task_transparent[0];
	auto &queue_opaque = queue_per_task_opaque[0];
	auto &queue_depth = queue_per_task_depth[0];

	if (setup_data.flags & (SCENE_RENDERER_FORWARD_OPAQUE_BIT | SCENE_RENDERER_Z_PREPASS_BIT))
	{
		scene->gather_visible_render_pass_sinks(context->get_render_parameters().camera_position, visible);
		scene->gather_visible_opaque_renderables(frustum, visible);
		if ((setup_data.flags & SCENE_RENDERER_SKIP_OPAQUE_FLOATING_BIT) == 0)
			scene->gather_opaque_floating_renderables(visible);

		if (setup_data.flags & SCENE_RENDERER_Z_PREPASS_BIT)
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
	if (setup_data.flags & SCENE_RENDERER_Z_PREPASS_BIT)
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

	// Only fixed function meshlets should be renderered here.
	if ((flush_flags & Renderer::MESH_ASSET_PHASE_1_BIT) &&
	    !(setup_data.flags & SCENE_RENDERER_MOTION_VECTOR_BIT))
		return;

	bool layered = render_pass_is_separate_layered();
	auto num_tasks = layered ? setup_data.layers : unsigned(MaxTasks);

	unsigned gather_iterations = layered ? num_tasks : 1;
	unsigned tasks_per_gather = layered ? 1 : unsigned(MaxTasks);

	if (setup_data.flags & (SCENE_RENDERER_FORWARD_OPAQUE_BIT |
	                        SCENE_RENDERER_Z_PREPASS_BIT |
	                        SCENE_RENDERER_MOTION_VECTOR_BIT))
	{
		if (!layered)
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

		for (unsigned gather_iter = 0; gather_iter < gather_iterations; gather_iter++)
		{
			if (setup_data.flags & (SCENE_RENDERER_FORWARD_OPAQUE_BIT | SCENE_RENDERER_Z_PREPASS_BIT))
			{
				Threaded::scene_gather_opaque_renderables(*setup_data.scene, composer,
				                                          setup_data.context[gather_iter].get_visibility_frustum(),
				                                          &visible_per_task[gather_iter], tasks_per_gather);
			}
			else if (setup_data.flags & SCENE_RENDERER_MOTION_VECTOR_BIT)
			{
				Threaded::scene_gather_motion_vector_renderables(*setup_data.scene, composer,
				                                                 setup_data.context[gather_iter].get_visibility_frustum(),
				                                                 &visible_per_task[gather_iter], tasks_per_gather);
			}
		}

		if (setup_data.flags & SCENE_RENDERER_Z_PREPASS_BIT)
		{
			Threaded::compose_parallel_push_renderables(composer, setup_data.context, queue_per_task_depth,
			                                            visible_per_task, num_tasks,
			                                            Threaded::PushType::Depth, layered);
		}

		if (setup_data.flags & SCENE_RENDERER_FORWARD_OPAQUE_BIT)
		{
			if (!layered && (setup_data.flags & SCENE_RENDERER_SKIP_UNBOUNDED_BIT) == 0)
			{
				auto &group = composer.begin_pipeline_stage();
				group.enqueue_task([this]() {
					setup_data.scene->gather_unbounded_renderables(visible_per_task[0]);
				});
			}
			Threaded::compose_parallel_push_renderables(composer, setup_data.context, queue_per_task_opaque,
			                                            visible_per_task, num_tasks,
			                                            Threaded::PushType::Normal, layered);
		}
		else if (setup_data.flags & SCENE_RENDERER_MOTION_VECTOR_BIT)
		{
			Threaded::compose_parallel_push_renderables(composer, setup_data.context, queue_per_task_opaque,
			                                            visible_per_task, num_tasks,
			                                            Threaded::PushType::MotionVector, layered);
		}
	}

	if (setup_data.flags & SCENE_RENDERER_DEFERRED_GBUFFER_BIT)
	{
		if (!layered)
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

		for (unsigned gather_iter = 0; gather_iter < gather_iterations; gather_iter++)
		{
			Threaded::scene_gather_opaque_renderables(*setup_data.scene, composer,
			                                          setup_data.context[gather_iter].get_visibility_frustum(), visible_per_task,
			                                          tasks_per_gather);
		}

		Threaded::compose_parallel_push_renderables(composer, setup_data.context, queue_per_task_opaque,
		                                            visible_per_task, num_tasks,
		                                            Threaded::PushType::Normal, layered);
	}

	if (setup_data.flags & SCENE_RENDERER_FORWARD_TRANSPARENT_BIT)
	{
		for (unsigned gather_iter = 0; gather_iter < gather_iterations; gather_iter++)
		{
			Threaded::scene_gather_transparent_renderables(*setup_data.scene, composer,
			                                               setup_data.context[gather_iter].get_visibility_frustum(),
			                                               &visible_per_task_transparent[gather_iter], tasks_per_gather);
		}

		Threaded::compose_parallel_push_renderables(composer, setup_data.context, queue_per_task_transparent,
		                                            visible_per_task_transparent, num_tasks,
		                                            Threaded::PushType::Normal, layered);
	}

	if (setup_data.flags & SCENE_RENDERER_DEPTH_BIT)
	{
		for (unsigned gather_iter = 0; gather_iter < gather_iterations; gather_iter++)
		{
			if (setup_data.flags & SCENE_RENDERER_DEPTH_DYNAMIC_BIT)
			{
				Threaded::scene_gather_dynamic_shadow_renderables(*setup_data.scene, composer,
				                                                  setup_data.context[gather_iter].get_visibility_frustum(),
				                                                  &visible_per_task[gather_iter], nullptr, tasks_per_gather);
			}

			if (setup_data.flags & SCENE_RENDERER_DEPTH_STATIC_BIT)
			{
				Threaded::scene_gather_static_shadow_renderables(*setup_data.scene, composer,
				                                                 setup_data.context[gather_iter].get_visibility_frustum(),
				                                                 &visible_per_task[gather_iter], nullptr, tasks_per_gather);
			}
		}

		Threaded::compose_parallel_push_renderables(composer, setup_data.context, queue_per_task_depth,
		                                            visible_per_task, num_tasks,
		                                            Threaded::PushType::Depth, layered);
	}
}

void RenderPassSceneRenderer::build_render_pass_inner(Vulkan::CommandBuffer &cmd, unsigned layer) const
{
	auto *suite = setup_data.suite;

	FlushParameters flush_params = {};
	unsigned bucket_index = 0;

	if (render_pass_is_separate_layered())
	{
		flush_params.layered = true;
		flush_params.layer = layer;
		bucket_index = layer;
	}

	if (setup_data.flags & (SCENE_RENDERER_FORWARD_OPAQUE_BIT | SCENE_RENDERER_Z_PREPASS_BIT))
	{
		if (setup_data.flags & SCENE_RENDERER_Z_PREPASS_BIT)
		{
			suite->get_renderer(RendererSuite::Type::PrepassDepth).flush(
				cmd, queue_per_task_depth[bucket_index], setup_data.context[bucket_index],
				Renderer::NO_COLOR_BIT | Renderer::SKIP_SORTING_BIT |
				Renderer::MESH_ASSET_OPAQUE_BIT |
				flush_flags, &flush_params);
		}

		if (setup_data.flags & SCENE_RENDERER_FORWARD_OPAQUE_BIT)
		{
			Renderer::RendererOptionFlags opt = Renderer::SKIP_SORTING_BIT | Renderer::MESH_ASSET_OPAQUE_BIT | flush_flags;
			if (setup_data.flags & (SCENE_RENDERER_Z_PREPASS_BIT | SCENE_RENDERER_Z_EXISTING_PREPASS_BIT))
				opt |= Renderer::DEPTH_STENCIL_READ_ONLY_BIT | Renderer::DEPTH_TEST_EQUAL_BIT;
			suite->get_renderer(RendererSuite::Type::ForwardOpaque).flush(
					cmd, queue_per_task_opaque[bucket_index], setup_data.context[bucket_index], opt, &flush_params);

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
		                                    Renderer::MESH_ASSET_MOTION_VECTOR_BIT |
		                                    Renderer::MESH_ASSET_IGNORE_ALPHA_TEST_BIT |
		                                    flush_flags;
		suite->get_renderer(RendererSuite::Type::MotionVector).flush(cmd, queue_per_task_opaque[bucket_index],
		                                                             setup_data.context[bucket_index], opt,
		                                                             &flush_params);
	}

	if (setup_data.flags & SCENE_RENDERER_DEFERRED_GBUFFER_BIT)
	{
		Renderer::RendererOptionFlags opt =
				Renderer::SKIP_SORTING_BIT | Renderer::MESH_ASSET_OPAQUE_BIT;
		if (setup_data.flags & SCENE_RENDERER_Z_EXISTING_PREPASS_BIT)
			opt |= Renderer::DEPTH_STENCIL_READ_ONLY_BIT | Renderer::DEPTH_TEST_EQUAL_BIT;

		suite->get_renderer(RendererSuite::Type::Deferred).flush(cmd, queue_per_task_opaque[bucket_index],
		                                                         setup_data.context[bucket_index],
		                                                         opt | flush_flags,
		                                                         &flush_params);

		if (setup_data.flags & SCENE_RENDERER_DEBUG_PROBES_BIT)
		{
			render_debug_probes(suite->get_renderer(RendererSuite::Type::Deferred), cmd,
			                    queue_non_tasked,
			                    *setup_data.context);
		}
	}

	if (setup_data.flags & SCENE_RENDERER_DEFERRED_LIGHTING_BIT)
		DeferredLightRenderer::render_light(cmd, *setup_data.context, convert_pcf_flags(setup_data.flags) | flush_flags);

	if (setup_data.flags & SCENE_RENDERER_FORWARD_TRANSPARENT_BIT)
	{
		suite->get_renderer(RendererSuite::Type::ForwardTransparent).flush(
			cmd, queue_per_task_transparent[bucket_index], setup_data.context[bucket_index],
			Renderer::DEPTH_STENCIL_READ_ONLY_BIT | Renderer::SKIP_SORTING_BIT |
			Renderer::MESH_ASSET_TRANSPARENT_BIT |
			flush_flags, &flush_params);
	}

	if (setup_data.flags & SCENE_RENDERER_DEPTH_BIT)
	{
		auto type = get_depth_renderer_type(setup_data.flags);
		suite->get_renderer(type).flush(cmd, queue_per_task_depth[bucket_index], setup_data.context[bucket_index],
		                                Renderer::DEPTH_BIAS_BIT | Renderer::SKIP_SORTING_BIT |
		                                Renderer::MESH_ASSET_OPAQUE_BIT | flush_flags,
		                                &flush_params);
	}
}

void RenderPassSceneRenderer::build_render_pass(Vulkan::CommandBuffer &cmd, unsigned layer) const
{
	build_render_pass_inner(cmd, layer);
}

void RenderPassSceneRenderer::build_render_pass(Vulkan::CommandBuffer &cmd)
{
	build_render_pass_inner(cmd, 0);
}

void RenderPassSceneRenderer::build_render_pass_separate_layer(Vulkan::CommandBuffer &cmd, unsigned layer)
{
	build_render_pass_inner(cmd, layer);
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

bool RenderPassSceneRenderer::render_pass_is_separate_layered() const
{
	return (setup_data.flags & SCENE_RENDERER_SEPARATE_PER_LAYER_BIT) != 0;
}

SceneTransformManager::SceneTransformManager()
{
	EVENT_MANAGER_REGISTER_LATCH(SceneTransformManager, on_device_created, on_device_destroyed, Vulkan::DeviceCreatedEvent);
}

void SceneTransformManager::on_device_created(const Vulkan::DeviceCreatedEvent &e)
{
	device = &e.get_device();
}

void SceneTransformManager::on_device_destroyed(const Vulkan::DeviceCreatedEvent &)
{
	device = nullptr;
	transforms.reset();
	prev_transforms.reset();
	aabbs.reset();
	task_buffer.reset();

	for (auto &ctx : per_context_data)
		ctx.occlusions.reset();
}

void SceneTransformManager::init(Scene &scene_)
{
	auto *entity = scene_.create_entity();
	auto *rpass = entity->allocate_component<RenderPassComponent>();
	rpass->creator = this;
	auto *refresh = entity->allocate_component<PerFrameUpdateComponent>();
	refresh->refresh = this;
	refresh->dependency_order = std::numeric_limits<int>::min() + 1;

	meshlets = &scene_.get_entity_pool().get_component_group<
		RenderableComponent, MeshletComponent, RenderInfoComponent, CachedSpatialTransformTimestampComponent>();
}

void SceneTransformManager::register_persistent_render_context(RenderContext *context)
{
	context->set_scene_transform_parameters(this, per_context_data.size());
	per_context_data.emplace_back();
}

void SceneTransformManager::register_one_shot_render_context(RenderContext *context)
{
	context->set_scene_transform_parameters(this, UINT32_MAX);
}

const char *SceneTransformManager::get_ident() const
{
	return "scene-transforms";
}

Vulkan::CommandBuffer::Type SceneTransformManager::owning_queue_type() const
{
	// The transfers are very small, and we don't want to incur cross-queue penalties.
	return Vulkan::CommandBuffer::Type::Generic;
}

void SceneTransformManager::add_render_passes(RenderGraph &graph)
{
	graph.add_external_lock_interface(get_ident(), this);
}

void SceneTransformManager::refresh(const RenderContext &, TaskComposer &composer)
{
	// Do actual work.
	auto &stage = composer.begin_pipeline_stage();
	stage.enqueue_task([&]()
	{
		update_scene_buffers();
	});
}

template <typename T>
static void flush_update(Vulkan::CommandBuffer &cmd, Vulkan::Buffer &buffer, VkDeviceSize offset, VkDeviceSize count,
                         const T *data)
{
	memcpy(cmd.update_buffer(buffer, offset * sizeof(*data), count * sizeof(*data)), &data[offset],
	       count * sizeof(*data));
}

static void flush_update(Vulkan::CommandBuffer &cmd, Vulkan::Buffer &buffer, VkDeviceSize offset, VkDeviceSize count,
                         const uint32_t *)
{
	cmd.fill_buffer(buffer, 0, offset * sizeof(uint32_t), count * sizeof(uint32_t));
}

template <typename T>
static void update_span(Vulkan::CommandBuffer &cmd, Vulkan::Buffer &buffer, const T *data,
                        const Scene::UpdateSpan &span)
{
	uint32_t base_i = 0;
	uint32_t base = 0;
	size_t count = 0;

	const auto flush = [&]()
	{
		if (count)
		{
			flush_update(cmd, buffer, base, count, data);
			count = 0;
		}
	};

	assert(span.count != 0);
	base = span.offsets[0];

	for (size_t i = 0; i < span.count; i++)
	{
		if (base + (i - base_i) != span.offsets[i])
		{
			flush();
			base = span.offsets[i];
			base_i = i;
		}

		count++;
	}

	flush();
}

static void copy_span(Vulkan::CommandBuffer &cmd, Vulkan::Buffer &dst, Vulkan::Buffer &src,
                      const Scene::UpdateSpan &span, VkDeviceSize element_size)
{
	uint32_t base_i = 0;
	uint32_t base = 0;
	size_t count = 0;

	const auto flush = [&]()
	{
		if (count)
		{
			cmd.copy_buffer(dst, base * element_size, src, base * element_size, count * element_size);
			count = 0;
		}
	};

	assert(span.count != 0);
	base = span.offsets[0];

	for (size_t i = 0; i < span.count; i++)
	{
		if (base + (i - base_i) != span.offsets[i])
		{
			flush();
			base = span.offsets[i];
			base_i = i;
		}

		count++;
	}

	flush();
}

void SceneTransformManager::update_task_buffer(Vulkan::CommandBuffer &cmd)
{
	unsigned num_task_instances_per_kind[2 * (int(DrawPipeline::Count) + 1)] = {};
	unsigned num_task_instances = 0;
	auto &manager = device->get_resource_manager();

	for (auto &elem : *meshlets)
	{
		auto &mesh = static_cast<MeshAssetRenderable &>(*get_component<RenderableComponent>(elem)->renderable);
		auto &transform = *get_component<RenderInfoComponent>(elem);
		auto range = manager.get_mesh_draw_range(mesh.get_asset_id());
		if (range.meshlet.count == 0)
			continue;
		bool skinned = (mesh.flags & RENDERABLE_MESH_ASSET_SKINNED_BIT) != 0;
		num_task_instances_per_kind[2 * int(mesh.get_mesh_draw_pipeline()) + skinned] += (range.meshlet.count + 31) / 32;

		if (transform.requires_motion_vectors && mesh.get_mesh_draw_pipeline() != DrawPipeline::AlphaBlend)
			num_task_instances_per_kind[2 * int(DrawPipeline::Count) + skinned] += (range.meshlet.count + 31) / 32;
	}

	for (auto count : num_task_instances_per_kind)
		num_task_instances += count;

	task_offset_counts[0] = {};

	for (int i = 1; i < 2 * (int(DrawPipeline::Count) + 1); i++)
	{
		task_offset_counts[i] = { num_task_instances_per_kind[i - 1], 0 };
		num_task_instances_per_kind[i] += num_task_instances_per_kind[i - 1];
	}

	auto required = VkDeviceSize(sizeof(MeshAssetDrawTaskInfo)) * num_task_instances;

	if (!required)
	{
		task_buffer.reset();
		return;
	}

	Vulkan::BufferCreateInfo bufinfo = {};
	bufinfo.size = required;
	bufinfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	bufinfo.domain = Vulkan::BufferDomain::UMACachedCoherentPreferDevice;
	task_buffer = device->create_buffer(bufinfo);
	device->set_name(*task_buffer, "task-buffer");

	// Ideally just derp the data into a mapped buffer on iGPU, but fallback to transfer queue on dGPU.
	auto *task_infos = static_cast<MeshAssetDrawTaskInfo *>(device->map_host_buffer(*task_buffer, Vulkan::MEMORY_ACCESS_WRITE_BIT));
	if (!task_infos)
		task_infos = static_cast<MeshAssetDrawTaskInfo *>(cmd.update_buffer(*task_buffer, 0, required));

	for (auto &elem : *meshlets)
	{
		auto &mesh = static_cast<MeshAssetRenderable &>(*get_component<RenderableComponent>(elem)->renderable);

		auto range = manager.get_mesh_draw_range(mesh.get_asset_id());
		if (range.meshlet.count == 0)
			continue;

		auto &transform = *get_component<RenderInfoComponent>(elem);
		bool skinned = (mesh.flags & RENDERABLE_MESH_ASSET_SKINNED_BIT) != 0;

		MeshAssetDrawTaskInfo draw = {};
		draw.aabb_instance = transform.aabb.offset;
		auto *node = transform.scene_node;
		auto *skin = node->get_skin();
		draw.node_instance = skin ? skin->transform.offset : node->transform.offset;
		draw.material_flags = mesh.get_material_flags();
		VK_ASSERT((range.meshlet.offset & 31) == 0);

		for (int i = 0; i <= transform.requires_motion_vectors; i++)
		{
			draw.occluder_state_offset = transform.occluder_state.offset;
			auto &offset_count = task_offset_counts[2 * int(i ? DrawPipeline::Count : mesh.get_mesh_draw_pipeline()) + skinned];

			for (uint32_t j = 0; j < range.meshlet.count; j += 32)
			{
				draw.mesh_index_count = range.meshlet.offset + j + (std::min(range.meshlet.count - j, 32u) - 1);
				task_infos[offset_count.first + offset_count.second++] = draw;
				draw.occluder_state_offset++;
			}
		}
	}

	// Even if it's device local, it's okay to call this.
	device->unmap_host_buffer(*task_buffer, Vulkan::MEMORY_ACCESS_WRITE_BIT);
}

void SceneTransformManager::update_scene_buffers()
{
	if (!scene)
		return;

	auto cmd = acquire_internal(*device, VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_CLEAR_BIT,
	                            VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT);

	update_task_buffer(*cmd);

	auto transform_span = scene->get_transform_update_span();
	auto aabb_span = scene->get_aabb_update_span();
	auto occlusion_span = scene->get_occluder_state_update_span();

	ensure_buffer(*cmd, transforms, VkDeviceSize(scene->get_transforms().get_count()) * sizeof(mat_affine), "transforms");
	ensure_buffer(*cmd, prev_transforms, VkDeviceSize(scene->get_transforms().get_count()) * sizeof(mat_affine), "prev-transforms");
	ensure_buffer(*cmd, aabbs, VkDeviceSize(scene->get_aabbs().get_count()) * sizeof(AABB), "aabbs");

	for (auto &ctx : per_context_data)
		ensure_buffer(*cmd, ctx.occlusions, VkDeviceSize(scene->get_occluder_states().get_count()) * sizeof(uint32_t), "occlusion-state");

	if (transform_span.count != 0)
	{
		// If there is motion this frame, copy over old transform.
		// We don't need to remember to keep copying over prev transforms when there is no motion since we only
		// need to render motion vectors for objects that moved *this* frame,
		// and we consider prev_transforms only valid for nodes which require special motion vectors.
		copy_span(*cmd, *prev_transforms, *transforms, transform_span, sizeof(mat_affine));
		// Add a pure execution barrier to ensure we don't clobber transforms before we have copied over to prev_transforms.
		cmd->barrier(VK_PIPELINE_STAGE_2_COPY_BIT, 0, VK_PIPELINE_STAGE_2_COPY_BIT, 0);
		update_span(*cmd, *transforms, scene->get_transforms().get_cached_transforms(), transform_span);
	}

	if (aabb_span.count != 0)
		update_span(*cmd, *aabbs, scene->get_aabbs().get_aabbs(), aabb_span);

	if (occlusion_span.count != 0)
		for (auto &ctx : per_context_data)
			update_span(*cmd, *ctx.occlusions, static_cast<const uint32_t *>(nullptr), occlusion_span);

	release_internal(cmd, VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_CLEAR_BIT,
	                 VK_ACCESS_TRANSFER_WRITE_BIT);

	scene->clear_updates();
}

void SceneTransformManager::ensure_buffer(Vulkan::CommandBuffer &cmd, Vulkan::BufferHandle &buffer,
                                          VkDeviceSize size, const char *name)
{
	if (buffer && buffer->get_create_info().size >= size)
		return;

	Vulkan::BufferCreateInfo bufinfo = {};
	bufinfo.size = std::max<VkDeviceSize>(64, size);
	if (buffer)
		bufinfo.size = std::max<VkDeviceSize>(size, buffer->get_create_info().size * 3 / 2);
	bufinfo.usage =
	    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	bufinfo.domain = Vulkan::BufferDomain::Device;
	auto new_buffer = device->create_buffer(bufinfo);
	device->set_name(*new_buffer, name);

	if (buffer)
	{
		cmd.copy_buffer(*new_buffer, 0, *buffer, 0, buffer->get_create_info().size);
		cmd.barrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
		            VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT);
	}

	buffer = std::move(new_buffer);
}

void SceneTransformManager::set_base_render_context(const RenderContext *)
{

}

void SceneTransformManager::set_base_renderer(const RendererSuite *)
{

}

void SceneTransformManager::set_scene(Scene *scene_)
{
	scene = scene_;
}

void SceneTransformManager::setup_render_pass_dependencies(RenderGraph &)
{
}

void SceneTransformManager::setup_render_pass_dependencies(RenderGraph &, RenderPass &target,
                                                           DependencyFlags dep_flags)
{
	if ((dep_flags & RenderPassCreator::GEOMETRY_BIT) != 0)
	{
		target.add_external_lock("scene-transforms", VK_PIPELINE_STAGE_2_PRE_RASTERIZATION_SHADERS_BIT,
		                         VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
	}
}

void SceneTransformManager::setup_render_pass_resources(RenderGraph &)
{
}
}
