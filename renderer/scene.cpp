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

#include "scene.hpp"
#include "transforms.hpp"
#include "lights/lights.hpp"
#include "simd.hpp"
#include "task_composer.hpp"
#include <float.h>

using namespace std;

namespace Granite
{

Scene::Scene()
	: spatials(pool.get_component_group<BoundedComponent, RenderInfoComponent, CachedSpatialTransformTimestampComponent>()),
	  opaque(pool.get_component_group<RenderInfoComponent, RenderableComponent, CachedSpatialTransformTimestampComponent, OpaqueComponent>()),
	  transparent(pool.get_component_group<RenderInfoComponent, RenderableComponent, CachedSpatialTransformTimestampComponent, TransparentComponent>()),
	  positional_lights(pool.get_component_group<RenderInfoComponent, RenderableComponent, CachedSpatialTransformTimestampComponent, PositionalLightComponent>()),
	  irradiance_affecting_positional_lights(pool.get_component_group<RenderInfoComponent, PositionalLightComponent, CachedSpatialTransformTimestampComponent, IrradianceAffectingComponent>()),
	  static_shadowing(pool.get_component_group<RenderInfoComponent, RenderableComponent, CachedSpatialTransformTimestampComponent, CastsStaticShadowComponent>()),
	  dynamic_shadowing(pool.get_component_group<RenderInfoComponent, RenderableComponent, CachedSpatialTransformTimestampComponent, CastsDynamicShadowComponent>()),
	  render_pass_shadowing(pool.get_component_group<RenderPassComponent, RenderableComponent, CachedSpatialTransformTimestampComponent, CastsDynamicShadowComponent>()),
	  backgrounds(pool.get_component_group<UnboundedComponent, RenderableComponent>()),
	  cameras(pool.get_component_group<CameraComponent, CachedTransformComponent>()),
	  directional_lights(pool.get_component_group<DirectionalLightComponent, CachedTransformComponent>()),
	  volumetric_diffuse_lights(pool.get_component_group<VolumetricDiffuseLightComponent, CachedSpatialTransformTimestampComponent, RenderInfoComponent>()),
	  volumetric_fog_regions(pool.get_component_group<VolumetricFogRegionComponent, CachedSpatialTransformTimestampComponent, RenderInfoComponent>()),
	  per_frame_updates(pool.get_component_group<PerFrameUpdateComponent>()),
	  per_frame_update_transforms(pool.get_component_group<PerFrameUpdateTransformComponent, RenderInfoComponent>()),
	  environments(pool.get_component_group<EnvironmentComponent>()),
	  render_pass_sinks(pool.get_component_group<RenderPassSinkComponent, RenderableComponent, CullPlaneComponent>()),
	  render_pass_creators(pool.get_component_group<RenderPassComponent>())
{
	pending_hierarchy_level_mask.store(0, std::memory_order_relaxed);
}

Scene::~Scene()
{
	// Makes shutdown way faster :)
	// We know ahead of time we're going to delete everything,
	// so reduce a lot of overhead by deleting right away.
	pool.reset_groups();

	destroy_entities(entities);
	destroy_entities(queued_entities);
}

template <typename T, typename Func>
static void gather_visible_renderables(const Frustum &frustum, VisibilityList &list, const T &objects,
                                       size_t begin_index, size_t end_index, const Func &filter_func)
{
	for (size_t i = begin_index; i < end_index; i++)
	{
		auto &o = objects[i];
		auto *transform = get_component<RenderInfoComponent>(o);

		auto *renderable = get_component<RenderableComponent>(o);
		auto flags = renderable->renderable->flags;
		if (!filter_func(transform, flags))
			continue;

		auto *timestamp = get_component<CachedSpatialTransformTimestampComponent>(o);

		Util::Hasher h;
		h.u64(timestamp->cookie);
		h.u32(timestamp->last_timestamp);

		if (transform->transform)
		{
			if ((flags & RENDERABLE_FORCE_VISIBLE_BIT) != 0 ||
			    SIMD::frustum_cull(transform->world_aabb, frustum.get_planes()))
			{
				list.push_back({ renderable->renderable.get(), transform, h.get() });
			}
		}
		else
			list.push_back({ renderable->renderable.get(), nullptr, h.get() });
	}
}

void Scene::add_render_passes(RenderGraph &graph)
{
	for (auto &pass : render_pass_creators)
	{
		auto *rpass = get_component<RenderPassComponent>(pass)->creator;
		rpass->add_render_passes(graph);
	}
}

void Scene::add_render_pass_dependencies(RenderGraph &graph, RenderPass &main_pass)
{
	for (auto &pass : render_pass_creators)
	{
		auto *rpass = get_component<RenderPassComponent>(pass)->creator;
		rpass->setup_render_pass_dependencies(graph, main_pass);
	}
}

void Scene::set_render_pass_data(const RendererSuite *suite, const RenderContext *context)
{
	for (auto &pass : render_pass_creators)
	{
		auto *rpass = get_component<RenderPassComponent>(pass)->creator;
		rpass->set_base_renderer(suite);
		rpass->set_base_render_context(context);
		rpass->set_scene(this);
	}
}

void Scene::bind_render_graph_resources(RenderGraph &graph)
{
	for (auto &pass : render_pass_creators)
	{
		auto *rpass = get_component<RenderPassComponent>(pass)->creator;
		rpass->setup_render_pass_resources(graph);
	}
}

void Scene::refresh_per_frame(const RenderContext &context, TaskComposer &composer)
{
	per_frame_update_transforms_sorted = per_frame_update_transforms;
	per_frame_updates_sorted = per_frame_updates;

	stable_sort(per_frame_update_transforms_sorted.begin(), per_frame_update_transforms_sorted.end(),
	            [](auto &a, auto &b) -> bool {
		            int order_a = get_component<PerFrameUpdateTransformComponent>(a)->dependency_order;
		            int order_b = get_component<PerFrameUpdateTransformComponent>(b)->dependency_order;
		            return order_a < order_b;
	            });

	stable_sort(per_frame_updates_sorted.begin(), per_frame_updates_sorted.end(),
	            [](auto &a, auto &b) -> bool {
		            int order_a = get_component<PerFrameUpdateComponent>(a)->dependency_order;
		            int order_b = get_component<PerFrameUpdateComponent>(b)->dependency_order;
		            return order_a < order_b;
	            });

	int dep = std::numeric_limits<int>::min();

	for (auto &update : per_frame_update_transforms_sorted)
	{
		auto *comp = get_component<PerFrameUpdateTransformComponent>(update);
		assert(comp->dependency_order != std::numeric_limits<int>::min());
		if (comp->dependency_order != dep)
		{
			composer.begin_pipeline_stage();
			dep = comp->dependency_order;
		}

		auto *refresh = comp->refresh;
		auto *transform = get_component<RenderInfoComponent>(update);
		if (refresh)
			refresh->refresh(context, transform, composer);
	}

	dep = std::numeric_limits<int>::min();

	for (auto &update : per_frame_updates_sorted)
	{
		auto *comp = get_component<PerFrameUpdateComponent>(update);
		assert(comp->dependency_order != std::numeric_limits<int>::min());
		if (comp->dependency_order != dep)
		{
			composer.begin_pipeline_stage();
			dep = comp->dependency_order;
		}

		auto *refresh = comp->refresh;
		if (refresh)
			refresh->refresh(context, composer);
	}

	composer.begin_pipeline_stage();
}

EnvironmentComponent *Scene::get_environment() const
{
	if (environments.empty())
		return nullptr;
	else
		return get_component<EnvironmentComponent>(environments.front());
}

EntityPool &Scene::get_entity_pool()
{
	return pool;
}

void Scene::gather_unbounded_renderables(VisibilityList &list) const
{
	for (auto &background : backgrounds)
		list.push_back({ get_component<RenderableComponent>(background)->renderable.get(), nullptr });
}

void Scene::gather_visible_render_pass_sinks(const vec3 &camera_pos, VisibilityList &list) const
{
	for (auto &sink : render_pass_sinks)
	{
		auto &plane = get_component<CullPlaneComponent>(sink)->plane;
		if (dot(vec4(camera_pos, 1.0f), plane) > 0.0f)
			list.push_back({get_component<RenderableComponent>(sink)->renderable.get(), nullptr});
	}
}

static bool filter_true(const RenderInfoComponent *, RenderableFlags)
{
	return true;
}

void Scene::gather_visible_opaque_renderables(const Frustum &frustum, VisibilityList &list) const
{
	gather_visible_renderables(frustum, list, opaque, 0, opaque.size(), filter_true);
}

void Scene::gather_visible_motion_vector_renderables(const Frustum &frustum, VisibilityList &list) const
{
	gather_visible_renderables(frustum, list, opaque, 0, opaque.size(),
	                           [](const RenderInfoComponent *info, RenderableFlags flags) {
		                           return (flags & RENDERABLE_IMPLICIT_MOTION_BIT) == 0 &&
		                                  info->requires_motion_vectors;
	                           });
}

void Scene::gather_visible_opaque_renderables_subset(const Frustum &frustum, VisibilityList &list,
                                                     unsigned index, unsigned num_indices) const
{
	size_t start_index = (index * opaque.size()) / num_indices;
	size_t end_index = ((index + 1) * opaque.size()) / num_indices;
	gather_visible_renderables(frustum, list, opaque, start_index, end_index, filter_true);
}

void Scene::gather_visible_motion_vector_renderables_subset(const Frustum &frustum, VisibilityList &list,
                                                            unsigned index, unsigned num_indices) const
{
	size_t start_index = (index * opaque.size()) / num_indices;
	size_t end_index = ((index + 1) * opaque.size()) / num_indices;
	gather_visible_renderables(frustum, list, opaque, start_index, end_index,
	                           [](const RenderInfoComponent *info, RenderableFlags flags) {
		                           return (flags & RENDERABLE_IMPLICIT_MOTION_BIT) == 0 &&
		                                  info->requires_motion_vectors;
	                           });
}

void Scene::gather_visible_transparent_renderables(const Frustum &frustum, VisibilityList &list) const
{
	gather_visible_renderables(frustum, list, transparent, 0, transparent.size(), filter_true);
}

void Scene::gather_visible_static_shadow_renderables(const Frustum &frustum, VisibilityList &list) const
{
	gather_visible_renderables(frustum, list, static_shadowing, 0, static_shadowing.size(), filter_true);
}

void Scene::gather_visible_transparent_renderables_subset(const Frustum &frustum, VisibilityList &list,
                                                          unsigned index, unsigned num_indices) const
{
	size_t start_index = (index * transparent.size()) / num_indices;
	size_t end_index = ((index + 1) * transparent.size()) / num_indices;
	gather_visible_renderables(frustum, list, transparent, start_index, end_index, filter_true);
}

void Scene::gather_visible_static_shadow_renderables_subset(const Frustum &frustum, VisibilityList &list,
                                                            unsigned index, unsigned num_indices) const
{
	size_t start_index = (index * static_shadowing.size()) / num_indices;
	size_t end_index = ((index + 1) * static_shadowing.size()) / num_indices;
	gather_visible_renderables(frustum, list, static_shadowing, start_index, end_index, filter_true);
}

void Scene::gather_visible_dynamic_shadow_renderables(const Frustum &frustum, VisibilityList &list) const
{
	gather_visible_renderables(frustum, list, dynamic_shadowing, 0, dynamic_shadowing.size(), filter_true);
	for (auto &object : render_pass_shadowing)
		list.push_back({ get_component<RenderableComponent>(object)->renderable.get(), nullptr });
}

void Scene::gather_visible_dynamic_shadow_renderables_subset(const Frustum &frustum, VisibilityList &list,
                                                             unsigned index, unsigned num_indices) const
{
	size_t start_index = (index * dynamic_shadowing.size()) / num_indices;
	size_t end_index = ((index + 1) * dynamic_shadowing.size()) / num_indices;
	gather_visible_renderables(frustum, list, dynamic_shadowing, start_index, end_index, filter_true);

	if (index == 0)
		for (auto &object : render_pass_shadowing)
			list.push_back({ get_component<RenderableComponent>(object)->renderable.get(), nullptr });
}

static void gather_positional_lights(const Frustum &frustum, VisibilityList &list,
                                     const ComponentGroupVector<
		                                     RenderInfoComponent,
		                                     RenderableComponent,
		                                     CachedSpatialTransformTimestampComponent,
		                                     PositionalLightComponent> &positional,
                                     size_t start_index, size_t end_index)
{
	for (size_t i = start_index; i < end_index; i++)
	{
		auto &o = positional[i];
		auto *transform = get_component<RenderInfoComponent>(o);
		auto *renderable = get_component<RenderableComponent>(o);
		auto *timestamp = get_component<CachedSpatialTransformTimestampComponent>(o);

		Util::Hasher h;
		h.u64(timestamp->cookie);
		h.u32(timestamp->last_timestamp);

		if (transform->transform)
		{
			if (SIMD::frustum_cull(transform->world_aabb, frustum.get_planes()))
				list.push_back({ renderable->renderable.get(), transform, h.get() });
		}
		else
			list.push_back({ renderable->renderable.get(), nullptr, h.get() });
	}
}

static void gather_positional_lights(const Frustum &frustum, PositionalLightList &list,
                                     const ComponentGroupVector<RenderInfoComponent,
		                                     RenderableComponent,
		                                     CachedSpatialTransformTimestampComponent,
		                                     PositionalLightComponent> &positional,
                                     size_t start_index, size_t end_index)
{
	for (size_t i = start_index; i < end_index; i++)
	{
		auto &o = positional[i];
		auto *transform = get_component<RenderInfoComponent>(o);
		auto *light = get_component<PositionalLightComponent>(o)->light;
		auto *timestamp = get_component<CachedSpatialTransformTimestampComponent>(o);

		Util::Hasher h;
		h.u64(timestamp->cookie);
		h.u32(timestamp->last_timestamp);

		if (transform->transform)
		{
			if (SIMD::frustum_cull(transform->world_aabb, frustum.get_planes()))
				list.push_back({ light, transform, h.get() });
		}
		else
			list.push_back({ light, transform, h.get() });
	}
}

void Scene::gather_visible_positional_lights(const Frustum &frustum, VisibilityList &list) const
{
	gather_positional_lights(frustum, list, positional_lights, 0, positional_lights.size());
}

void Scene::gather_irradiance_affecting_positional_lights(PositionalLightList &list) const
{
	for (auto &light_tup : irradiance_affecting_positional_lights)
	{
		auto *transform = get_component<RenderInfoComponent>(light_tup);
		auto *light = get_component<PositionalLightComponent>(light_tup)->light;
		auto *timestamp = get_component<CachedSpatialTransformTimestampComponent>(light_tup);

		Util::Hasher h;
		h.u64(timestamp->cookie);
		h.u32(timestamp->last_timestamp);
		list.push_back({ light, transform, h.get() });
	}
}

void Scene::gather_visible_positional_lights(const Frustum &frustum, PositionalLightList &list) const
{
	gather_positional_lights(frustum, list, positional_lights, 0, positional_lights.size());
}

void Scene::gather_visible_volumetric_diffuse_lights(const Frustum &frustum, VolumetricDiffuseLightList &list) const
{
	for (auto &o : volumetric_diffuse_lights)
	{
		auto *transform = get_component<RenderInfoComponent>(o);
		auto *light = get_component<VolumetricDiffuseLightComponent>(o);

		if (light->light.get_volume_view())
		{
			if (transform->transform)
			{
				if (SIMD::frustum_cull(transform->world_aabb, frustum.get_planes()))
					list.push_back({ light, transform });
			}
			else
				list.push_back({ light, transform });
		}
	}
}

void Scene::gather_visible_volumetric_fog_regions(const Frustum &frustum, VolumetricFogRegionList &list) const
{
	for (auto &o : volumetric_fog_regions)
	{
		auto *transform = get_component<RenderInfoComponent>(o);
		auto *region = get_component<VolumetricFogRegionComponent>(o);

		if (region->region.get_volume_view())
		{
			if (transform->transform)
			{
				if (SIMD::frustum_cull(transform->world_aabb, frustum.get_planes()))
					list.push_back({ region, transform });
			}
			else
				list.push_back({ region, transform });
		}
	}
}

void Scene::gather_visible_positional_lights_subset(const Frustum &frustum, VisibilityList &list,
                                                    unsigned index, unsigned num_indices) const
{
	size_t start_index = (index * positional_lights.size()) / num_indices;
	size_t end_index = ((index + 1) * positional_lights.size()) / num_indices;
	gather_positional_lights(frustum, list, positional_lights, start_index, end_index);
}

void Scene::gather_visible_positional_lights_subset(const Frustum &frustum, PositionalLightList &list,
                                                    unsigned index, unsigned num_indices) const
{
	size_t start_index = (index * positional_lights.size()) / num_indices;
	size_t end_index = ((index + 1) * positional_lights.size()) / num_indices;
	gather_positional_lights(frustum, list, positional_lights, start_index, end_index);
}

size_t Scene::get_opaque_renderables_count() const
{
	return opaque.size();
}

size_t Scene::get_motion_vector_renderables_count() const
{
	// We might want separate MotionVector components, but for now we only select
	// objects which actually had motion applied to them anyways.
	return opaque.size();
}

size_t Scene::get_transparent_renderables_count() const
{
	return transparent.size();
}

size_t Scene::get_static_shadow_renderables_count() const
{
	return static_shadowing.size();
}

size_t Scene::get_dynamic_shadow_renderables_count() const
{
	return dynamic_shadowing.size();
}

size_t Scene::get_positional_lights_count() const
{
	return positional_lights.size();
}

#if 0
static void log_node_transforms(const Scene::Node &node)
{
	for (unsigned i = 0; i < node.cached_skin_transform.bone_world_transforms.size(); i++)
	{
		LOGI("Joint #%u:\n", i);

		const auto &ibp = node.cached_skin_transform.bone_world_transforms[i];
		LOGI(" Transform:\n"
				     "      [%f, %f, %f, %f]\n"
				     "      [%f, %f, %f, %f]\n"
				     "      [%f, %f, %f, %f]\n"
				     "      [%f, %f, %f, %f]\n",
		     ibp[0][0], ibp[1][0], ibp[2][0], ibp[3][0],
		     ibp[0][1], ibp[1][1], ibp[2][1], ibp[3][1],
		     ibp[0][2], ibp[1][2], ibp[2][2], ibp[3][2],
		     ibp[0][3], ibp[1][3], ibp[2][3], ibp[3][3]);
	}
}
#endif

static void update_skinning(Scene::Node &node)
{
	auto &skin = *node.get_skin();
	if (!skin.cached_skin_transform.bone_world_transforms.empty())
	{
		skin.prev_cached_skin_transform = skin.cached_skin_transform;

		auto len = skin.skin.size();
		assert(skin.skin.size() == skin.cached_skin_transform.bone_world_transforms.size());
		//assert(node.get_skin().cached_skin.size() == node.cached_skin_transform.bone_normal_transforms.size());
		for (size_t i = 0; i < len; i++)
		{
			SIMD::mul(skin.cached_skin_transform.bone_world_transforms[i],
			          skin.cached_skin[i]->world_transform,
			          skin.inverse_bind_poses[i]);
			//node.cached_skin_transform.bone_normal_transforms[i] = node.get_skin().cached_skin[i]->normal_transform;
		}
		//log_node_transforms(node);
	}
}

static const mat4 identity_transform(1.0f);

size_t Scene::get_cached_transforms_count() const
{
	return spatials.size();
}

void Scene::update_cached_transforms_subset(unsigned index, unsigned num_indices)
{
	size_t begin_index = (spatials.size() * index) / num_indices;
	size_t end_index = (spatials.size() * (index + 1)) / num_indices;
	update_cached_transforms_range(begin_index, end_index);
}

void Scene::update_all_transforms()
{
	update_transform_tree();
	update_transform_listener_components();
	update_cached_transforms_range(0, spatials.size());
}

static void perform_update_skinning(Scene::Node * const *updates, size_t count)
{
	for (size_t i = 0; i < count; i++)
	{
		auto *update = updates[i];
		update_skinning(*update);
	}
}

void Scene::update_transform_tree(TaskComposer *composer)
{
	if (composer)
	{
		auto &group = composer->begin_pipeline_stage();
		group.set_desc("distribute-per-level-updates");
		group.enqueue_task([this, h = composer->get_deferred_enqueue_handle()]() mutable {
			distribute_per_level_updates(h.get());
		});
	}
	else
		distribute_per_level_updates(nullptr);

	if (composer)
	{
		auto &thread_group = composer->get_thread_group();
		auto &group = composer->begin_pipeline_stage();
		group.set_desc("dispatch-per-level-updates");
		group.enqueue_task([&, h = composer->get_deferred_enqueue_handle()]() mutable {
			uint32_t mask = pending_hierarchy_level_mask.load(std::memory_order_relaxed);
			if (!mask)
				return;
			uint32_t num_pending_levels = 32 - leading_zeroes(mask);

			TaskComposer stage_composer(thread_group);
			for (unsigned level = 0, count = num_pending_levels; level < count; level++)
			{
				auto &level_group = stage_composer.begin_pipeline_stage();
				level_group.set_desc("perform-per-level-update");
				perform_per_level_updates(level, &level_group);
			}
			stage_composer.add_outgoing_dependency(*h);
			h->flush();
		});
	}
	else
	{
		uint32_t mask = pending_hierarchy_level_mask.load(std::memory_order_relaxed);
		if (mask)
		{
			uint32_t num_pending_levels = 32 - leading_zeroes(mask);
			for (unsigned level = 0, count = num_pending_levels; level < count; level++)
				perform_per_level_updates(level, nullptr);
		}
	}

	if (composer)
	{
		auto &group = composer->begin_pipeline_stage();
		group.set_desc("perform-update-skinning");

		group.enqueue_task([this, h = composer->get_deferred_enqueue_handle()]() mutable {
			pending_node_updates_skin.for_each_ranged([&](Node *const *updates, size_t count) {
				h->enqueue_task([=]() {
					perform_update_skinning(updates, count);
				});
			});
		});
	}
	else
	{
		pending_node_updates_skin.for_each_ranged([this](Node *const *updates, size_t count) {
			perform_update_skinning(updates, count);
		});
	}

	if (composer)
	{
		auto &clear_task = composer->begin_pipeline_stage();
		clear_task.enqueue_task([this]() {
			pending_node_updates.clear();
			for (auto &l : pending_node_update_per_level)
				l.clear();
			pending_node_updates_skin.clear();
			pending_hierarchy_level_mask.store(0, std::memory_order_relaxed);
		});
	}
	else
	{
		pending_node_updates.clear();
		for (auto &l : pending_node_update_per_level)
			l.clear();
		pending_node_updates_skin.clear();
		pending_hierarchy_level_mask.store(0, std::memory_order_relaxed);
	}
}

void Scene::update_transform_tree()
{
	update_transform_tree(nullptr);
}

void Scene::update_transform_tree(TaskComposer &composer)
{
	update_transform_tree(&composer);
}

void Scene::update_transform_listener_components()
{
	// Update camera transforms.
	for (auto &c : cameras)
	{
		CameraComponent *cam;
		CachedTransformComponent *transform;
		tie(cam, transform) = c;
		cam->camera.set_transform(transform->transform->world_transform);
	}

	// Update directional light transforms.
	for (auto &light : directional_lights)
	{
		DirectionalLightComponent *l;
		CachedTransformComponent *transform;
		tie(l, transform) = light;

		// v = [0, 0, 1, 0].
		l->direction = normalize(transform->transform->world_transform[2].xyz());
	}

	for (auto &light : volumetric_diffuse_lights)
	{
		VolumetricDiffuseLightComponent *l;
		CachedSpatialTransformTimestampComponent *timestamp;
		RenderInfoComponent *transform;
		tie(l, timestamp, transform) = light;

		if (timestamp->last_timestamp != l->timestamp)
		{
			// This is a somewhat expensive operation, so timestamp it.
			// We only expect this to run once since diffuse volumes really
			// cannot freely move around the scene due to the semi-baked nature of it.
			auto texture_to_world = transform->transform->world_transform * translate(vec3(-0.5f));
			auto world_to_texture = inverse(texture_to_world);

			world_to_texture = transpose(world_to_texture);
			texture_to_world = transpose(texture_to_world);

			for (int i = 0; i < 3; i++)
			{
				l->world_to_texture[i] = world_to_texture[i];
				l->texture_to_world[i] = texture_to_world[i];
			}
			l->world_lo = transform->world_aabb.get_minimum4();
			l->world_hi = transform->world_aabb.get_maximum4();
			l->timestamp = timestamp->last_timestamp;
		}
	}

	for (auto &region : volumetric_fog_regions)
	{
		VolumetricFogRegionComponent *r;
		CachedSpatialTransformTimestampComponent *timestamp;
		RenderInfoComponent *transform;
		tie(r, timestamp, transform) = region;

		if (timestamp->last_timestamp != r->timestamp)
		{
			// This is a somewhat expensive operation, so timestamp it.
			auto texture_to_world = transform->transform->world_transform * translate(vec3(-0.5f));
			auto world_to_texture = inverse(texture_to_world);

			world_to_texture = transpose(world_to_texture);

			for (int i = 0; i < 3; i++)
				r->world_to_texture[i] = world_to_texture[i];
			r->world_lo = transform->world_aabb.get_minimum4();
			r->world_hi = transform->world_aabb.get_maximum4();
			r->timestamp = timestamp->last_timestamp;
		}
	}
}

void Scene::update_cached_transforms_range(size_t begin_range, size_t end_range)
{
	for (size_t i = begin_range; i < end_range; i++)
	{
		auto &s = spatials[i];

		BoundedComponent *aabb;
		RenderInfoComponent *cached_transform;
		CachedSpatialTransformTimestampComponent *timestamp;
		tie(aabb, cached_transform, timestamp) = s;

		uint64_t new_timestamp = *timestamp->current_timestamp;
		bool modified_timestamp = timestamp->last_timestamp != new_timestamp;

		if (modified_timestamp)
		{
			if (cached_transform->transform)
			{
				if (cached_transform->skin_transform)
				{
					// TODO: Isolate the AABB per bone.
					cached_transform->world_aabb = AABB(vec3(FLT_MAX), vec3(-FLT_MAX));
					for (auto &m : cached_transform->skin_transform->bone_world_transforms)
						SIMD::transform_and_expand_aabb(cached_transform->world_aabb, *aabb->aabb, m);
				}
				else
				{
					SIMD::transform_aabb(cached_transform->world_aabb,
					                     *aabb->aabb,
					                     cached_transform->transform->world_transform);
				}
			}

			timestamp->last_timestamp = new_timestamp;
		}

		// The first update won't have valid prev transforms.
		cached_transform->requires_motion_vectors = modified_timestamp && new_timestamp >= 2;
	}
}

void Scene::push_pending_node_update(Node *node)
{
	pending_node_updates.push(node);
}

unsigned Scene::Node::get_dirty_transform_depth() const
{
	unsigned level_candidate = 0;
	unsigned level = 0;

	auto *node = this;
	while (node->parent)
	{
		level++;
		if (node->parent->node_is_pending_update.load(std::memory_order_relaxed))
			level_candidate = level;
		node = node->parent;
	}

	return level_candidate;
}

void Scene::distribute_update_to_level(Node *update, unsigned level)
{
	if (level >= MaxNodeHierarchyLevels)
	{
		LOGE("distribute_update_to_level: Level %u is out of range.\n", level);
		return;
	}

	pending_node_update_per_level[level].push(update);
	pending_hierarchy_level_mask.fetch_or(1u << level, std::memory_order_relaxed);
	if (update->get_skin())
		pending_node_updates_skin.push(update);

	level++;
	auto &children = update->get_children();
	for (auto &child : children)
		if (!child->test_and_set_pending_update_no_atomic())
			distribute_update_to_level(child.get(), level);
}

void Scene::distribute_per_level_updates(TaskGroup *group)
{
	if (group)
	{
		pending_node_updates.for_each_ranged([this, group](Node *const *updates, size_t count) {
			group->enqueue_task([=]() {
				for (size_t i = 0; i < count; i++)
				{
					auto *update = updates[i];
					unsigned level = update->get_dirty_transform_depth();
					distribute_update_to_level(update, level);
				}
			});
		});
	}
	else
	{
		pending_node_updates.for_each_ranged([this](Node *const *updates, size_t count) {
			for (size_t i = 0; i < count; i++)
			{
				auto *update = updates[i];
				unsigned level = update->get_dirty_transform_depth();
				distribute_update_to_level(update, level);
			}
		});
	}
}

static void update_transform_tree_node(Scene::Node &node, const mat4 &transform)
{
	node.prev_cached_transform = node.cached_transform;
	compute_model_transform(node.cached_transform.world_transform,
	                        node.transform.scale, node.transform.rotation, node.transform.translation,
	                        transform);

	//compute_normal_transform(node.cached_transform.normal_transform, node.cached_transform.world_transform);
	node.update_timestamp();
	node.clear_pending_update_no_atomic();
}

static void perform_updates(Scene::Node * const *updates, size_t count)
{
	for (size_t i = 0; i < count; i++)
	{
		auto *update = updates[i];
		auto *parent = update->get_parent();
		auto &transform = parent ? parent->cached_transform.world_transform : identity_transform;
		update_transform_tree_node(*update, transform);
	}
}

void Scene::perform_per_level_updates(unsigned level, TaskGroup *group)
{
	if (group)
	{
		pending_node_update_per_level[level].for_each_ranged([group](Node *const *updates, size_t count) {
			group->enqueue_task([=]() {
				perform_updates(updates, count);
			});
		});
	}
	else
	{
		pending_node_update_per_level[level].for_each_ranged([](Node *const *updates, size_t count) {
			perform_updates(updates, count);
		});
	}
}

Scene::NodeHandle Scene::create_node()
{
	return Scene::NodeHandle(node_pool.allocate(this));
}

void Scene::NodeDeleter::operator()(Node *node)
{
	node->parent_scene->get_node_pool().free(node);
}

static void add_bone(Scene::NodeHandle *bones, uint32_t parent, const SceneFormats::Skin::Bone &bone)
{
	bones[parent]->add_child(bones[bone.index]);
	for (auto &child : bone.children)
		add_bone(bones, bone.index, child);
}

Scene::NodeHandle Scene::create_skinned_node(const SceneFormats::Skin &skin)
{
	auto node = create_node();

	vector<NodeHandle> bones;
	bones.reserve(skin.joint_transforms.size());

	for (size_t i = 0; i < skin.joint_transforms.size(); i++)
	{
		bones.push_back(create_node());
		bones[i]->transform.translation = skin.joint_transforms[i].translation;
		bones[i]->transform.scale = skin.joint_transforms[i].scale;
		bones[i]->transform.rotation = skin.joint_transforms[i].rotation;
	}

	node->set_skin(skinning_pool.allocate());
	auto &node_skin = *node->get_skin();
	node_skin.cached_skin_transform.bone_world_transforms.resize(skin.joint_transforms.size());
	node_skin.prev_cached_skin_transform.bone_world_transforms.resize(skin.joint_transforms.size());
	//node->cached_skin_transform.bone_normal_transforms.resize(skin.joint_transforms.size());

	node_skin.skin.reserve(skin.joint_transforms.size());
	node_skin.cached_skin.reserve(skin.joint_transforms.size());
	node_skin.inverse_bind_poses.reserve(skin.joint_transforms.size());
	for (size_t i = 0; i < skin.joint_transforms.size(); i++)
	{
		node_skin.cached_skin.push_back(&bones[i]->cached_transform);
		node_skin.skin.push_back(&bones[i]->transform);
		node_skin.inverse_bind_poses.push_back(skin.inverse_bind_pose[i]);
	}

	for (auto &skeleton : skin.skeletons)
	{
		node->add_child(bones[skeleton.index]);
		for (auto &child : skeleton.children)
			add_bone(bones.data(), skeleton.index, child);
	}

	node_skin.skin_compat = skin.skin_compat;
	return node;
}

void Scene::Node::add_child(NodeHandle node)
{
	assert(this != node.get());
	assert(node->parent == nullptr);
	node->parent = this;
	node->invalidate_cached_transform();
	children.push_back(node);
}

Scene::NodeHandle Scene::Node::remove_child(Node *node)
{
	assert(node->parent == this);
	node->parent = nullptr;
	auto handle = node->reference_from_this();
	node->invalidate_cached_transform();

	auto itr = remove_if(begin(children), end(children), [&](const NodeHandle &h) {
		return node == h.get();
	});
	assert(itr != end(children));
	children.erase(itr, end(children));
	return handle;
}

Scene::NodeHandle Scene::Node::remove_node_from_hierarchy(Node *node)
{
	if (node->parent)
		return node->parent->remove_child(node);
	else
		return Scene::NodeHandle(nullptr);
}

void Scene::Node::invalidate_cached_transform()
{
	// Order does not matter. We will synchronize where we actually read from this.
	if (!node_is_pending_update.exchange(true, std::memory_order_relaxed))
		parent_scene->push_pending_node_update(this);
}

Entity *Scene::create_entity()
{
	Entity *entity = pool.create_entity();
	entities.insert_front(entity);
	return entity;
}

static std::atomic<uint64_t> transform_cookies;

Entity *Scene::create_volumetric_diffuse_light(uvec3 resolution, Node *node)
{
	Entity *entity = pool.create_entity();
	entities.insert_front(entity);

	auto *light = entity->allocate_component<VolumetricDiffuseLightComponent>();
	light->light.set_resolution(resolution);
	auto *transform = entity->allocate_component<RenderInfoComponent>();
	auto *timestamp = entity->allocate_component<CachedSpatialTransformTimestampComponent>();

	auto *bounded = entity->allocate_component<BoundedComponent>();
	bounded->aabb = &VolumetricDiffuseLight::get_static_aabb();

	if (node)
	{
		transform->transform = &node->cached_transform;
		timestamp->current_timestamp = node->get_timestamp_pointer();
	}
	timestamp->cookie = transform_cookies.fetch_add(std::memory_order_relaxed);

	return entity;
}

Entity *Scene::create_volumetric_fog_region(Node *node)
{
	Entity *entity = pool.create_entity();
	entities.insert_front(entity);

	entity->allocate_component<VolumetricFogRegionComponent>();
	auto *transform = entity->allocate_component<RenderInfoComponent>();
	auto *timestamp = entity->allocate_component<CachedSpatialTransformTimestampComponent>();

	auto *bounded = entity->allocate_component<BoundedComponent>();
	bounded->aabb = &VolumetricFogRegion::get_static_aabb();

	if (node)
	{
		transform->transform = &node->cached_transform;
		timestamp->current_timestamp = node->get_timestamp_pointer();
	}
	timestamp->cookie = transform_cookies.fetch_add(std::memory_order_relaxed);

	return entity;
}

Entity *Scene::create_light(const SceneFormats::LightInfo &light, Node *node)
{
	Entity *entity = pool.create_entity();
	entities.insert_front(entity);

	switch (light.type)
	{
	case SceneFormats::LightInfo::Type::Directional:
	{
		auto *dir = entity->allocate_component<DirectionalLightComponent>();
		auto *transform = entity->allocate_component<CachedTransformComponent>();
		transform->transform = &node->cached_transform;
		dir->color = light.color;
		break;
	}

	case SceneFormats::LightInfo::Type::Point:
	case SceneFormats::LightInfo::Type::Spot:
	{
		AbstractRenderableHandle renderable;
		if (light.type == SceneFormats::LightInfo::Type::Point)
			renderable = Util::make_handle<PointLight>();
		else
		{
			renderable = Util::make_handle<SpotLight>();
			auto &spot = static_cast<SpotLight &>(*renderable);
			spot.set_spot_parameters(light.inner_cone, light.outer_cone);
		}

		auto &positional = static_cast<PositionalLight &>(*renderable);
		positional.set_color(light.color);
		if (light.range > 0.0f)
			positional.set_maximum_range(light.range);

		entity->allocate_component<PositionalLightComponent>()->light = &positional;
		entity->allocate_component<RenderableComponent>()->renderable = renderable;

		auto *transform = entity->allocate_component<RenderInfoComponent>();
		auto *timestamp = entity->allocate_component<CachedSpatialTransformTimestampComponent>();
		timestamp->cookie = transform_cookies.fetch_add(1, std::memory_order_relaxed);

		if (node)
		{
			transform->transform = &node->cached_transform;
			timestamp->current_timestamp = node->get_timestamp_pointer();
		}

		auto *bounded = entity->allocate_component<BoundedComponent>();
		bounded->aabb = renderable->get_static_aabb();
		break;
	}
	}
	return entity;
}

Entity *Scene::create_renderable(AbstractRenderableHandle renderable, Node *node)
{
	Entity *entity = pool.create_entity();
	entities.insert_front(entity);

	if (renderable->has_static_aabb())
	{
		auto *transform = entity->allocate_component<RenderInfoComponent>();
		auto *timestamp = entity->allocate_component<CachedSpatialTransformTimestampComponent>();
		timestamp->cookie = transform_cookies.fetch_add(1, std::memory_order_relaxed);

		if (node)
		{
			transform->transform = &node->cached_transform;
			transform->prev_transform = &node->prev_cached_transform;
			timestamp->current_timestamp = node->get_timestamp_pointer();

			if (node->get_skin() && !node->get_skin()->cached_skin.empty())
			{
				transform->skin_transform = &node->get_skin()->cached_skin_transform;
				transform->prev_skin_transform = &node->get_skin()->prev_cached_skin_transform;
			}
		}
		auto *bounded = entity->allocate_component<BoundedComponent>();
		bounded->aabb = renderable->get_static_aabb();
	}
	else
		entity->allocate_component<UnboundedComponent>();

	auto *render = entity->allocate_component<RenderableComponent>();

	switch (renderable->get_mesh_draw_pipeline())
	{
	case DrawPipeline::AlphaBlend:
		entity->allocate_component<TransparentComponent>();
		break;

	default:
		entity->allocate_component<OpaqueComponent>();
		if (renderable->has_static_aabb())
		{
			// TODO: Find a way to make this smarter.
			entity->allocate_component<CastsStaticShadowComponent>();
			entity->allocate_component<CastsDynamicShadowComponent>();
		}
		break;
	}

	render->renderable = renderable;
	return entity;
}

void Scene::destroy_entities(Util::IntrusiveList<Entity> &entity_list)
{
	auto itr = entity_list.begin();
	while (itr != entity_list.end())
	{
		auto *to_free = itr.get();
		itr = entity_list.erase(itr);
		to_free->get_pool()->delete_entity(to_free);
	}
}

void Scene::remove_entities_with_component(ComponentType id)
{
	// We know ahead of time we're going to delete everything,
	// so reduce a lot of overhead by deleting right away.
	pool.reset_groups_for_component_type(id);

	auto itr = entities.begin();
	while (itr != entities.end())
	{
		if (itr->has_component(id))
		{
			auto *to_free = itr.get();
			itr = entities.erase(itr);
			to_free->get_pool()->delete_entity(to_free);
		}
		else
			++itr;
	}
}

void Scene::destroy_queued_entities()
{
	destroy_entities(queued_entities);
}

void Scene::destroy_entity(Entity *entity)
{
	if (entity)
	{
		entities.erase(entity);
		entity->get_pool()->delete_entity(entity);
	}
}

void Scene::queue_destroy_entity(Entity *entity)
{
	if (entity->mark_for_destruction())
	{
		entities.erase(entity);
		queued_entities.insert_front(entity);
	}
}

}
