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

#pragma once

#include "ecs.hpp"
#include "render_components.hpp"
#include "frustum.hpp"
#include "scene_formats.hpp"
#include "no_init_pod.hpp"
#include "thread_group.hpp"
#include "atomic_append_buffer.hpp"
#include "arena_allocator.hpp"
#include <atomic>

namespace Granite
{
class RenderContext;
struct EnvironmentComponent;
class Node;
class Scene;

static constexpr unsigned MaxNumNodesLog2 = 20;
static constexpr unsigned MaxOcclusionStatesLog2 = 20;

struct TransformBackingAllocator final : Util::SliceBackingAllocator
{
	uint32_t allocate(uint32_t count) override;
	void free(uint32_t index) override;
	void prime(uint32_t count, const void *opaque_meta) override;

	Util::DynamicArray<Transform> transforms;
	Util::DynamicArray<mat_affine> cached_transforms;
	Util::DynamicArray<mat_affine> cached_prev_transforms;
	bool allocated_global = false;
};

struct TransformBackingAllocatorAABB final : Util::SliceBackingAllocator
{
	uint32_t allocate(uint32_t count) override;
	void free(uint32_t index) override;
	void prime(uint32_t count, const void *opaque_meta) override;

	Util::DynamicArray<AABB> aabb;
	bool allocated_global = false;
};

class TransformAllocator : public Util::SliceAllocator
{
public:
	TransformAllocator();
	inline Transform *get_transforms() { return allocator.transforms.data(); }
	inline mat_affine *get_cached_transforms() { return allocator.cached_transforms.data(); }
	inline mat_affine *get_cached_prev_transforms() { return allocator.cached_prev_transforms.data(); }
	inline const Transform *get_transforms() const { return allocator.transforms.data(); }
	inline const mat_affine *get_cached_transforms() const { return allocator.cached_transforms.data(); }
	inline const mat_affine *get_cached_prev_transforms() const { return allocator.cached_prev_transforms.data(); }

	uint32_t get_count() const { return high_water_mark; }
	bool allocate(uint32_t count, Util::AllocatedSlice *slice);

private:
	TransformBackingAllocator allocator;
	uint32_t high_water_mark = 0;
};

class TransformAllocatorAABB : public Util::SliceAllocator
{
public:
	TransformAllocatorAABB();
	inline AABB *get_aabbs() { return allocator.aabb.data(); }
	inline const AABB *get_aabbs() const { return allocator.aabb.data(); }

	uint32_t get_count() const { return high_water_mark; }
	bool allocate(uint32_t count, Util::AllocatedSlice *slice);

private:
	TransformBackingAllocatorAABB allocator;
	uint32_t high_water_mark = 0;
};

class OccluderStateAllocator : public Util::SliceAllocator
{
public:
	OccluderStateAllocator();
	uint32_t get_count() const { return high_water_mark; }
	bool allocate(uint32_t count, Util::AllocatedSlice *slice);

private:
	Util::SliceBackingAllocatorVA allocator;
	uint32_t high_water_mark = 0;
};

class Scene
{
public:
	Scene();
	~Scene();
	friend class Node;

	// Non-copyable, movable.
	Scene(const Scene &) = delete;
	void operator=(const Scene &) = delete;

	void refresh_per_frame(const RenderContext &context, TaskComposer &composer);

	void update_all_transforms();
	void update_transform_tree();
	void update_transform_tree(TaskComposer &composer);
	void update_transform_listener_components();
	void update_cached_transforms_subset(unsigned index, unsigned num_indices);
	size_t get_cached_transforms_count() const;

	void gather_visible_opaque_renderables(const Frustum &frustum, VisibilityList &list) const;
	void gather_visible_motion_vector_renderables(const Frustum &frustum, VisibilityList &list) const;
	void gather_visible_transparent_renderables(const Frustum &frustum, VisibilityList &list) const;
	void gather_visible_static_shadow_renderables(const Frustum &frustum, VisibilityList &list) const;
	void gather_visible_dynamic_shadow_renderables(const Frustum &frustum, VisibilityList &list) const;
	void gather_visible_positional_lights(const Frustum &frustum, PositionalLightList &list) const;
	void gather_irradiance_affecting_positional_lights(PositionalLightList &list) const;
	void gather_visible_volumetric_diffuse_lights(const Frustum &frustum, VolumetricDiffuseLightList &list) const;
	void gather_visible_volumetric_decals(const Frustum &frustum, VolumetricDecalList &list) const;
	void gather_visible_volumetric_fog_regions(const Frustum &frustum, VolumetricFogRegionList &list) const;

	void gather_visible_opaque_renderables_subset(const Frustum &frustum, VisibilityList &list,
	                                              unsigned index, unsigned num_indices) const;
	void gather_visible_motion_vector_renderables_subset(const Frustum &frustum, VisibilityList &list,
	                                                     unsigned index, unsigned num_indices) const;
	void gather_visible_transparent_renderables_subset(const Frustum &frustum, VisibilityList &list,
	                                                   unsigned index, unsigned num_indices) const;
	void gather_visible_static_shadow_renderables_subset(const Frustum &frustum, VisibilityList &list,
	                                                     unsigned index, unsigned num_indices) const;
	void gather_visible_dynamic_shadow_renderables_subset(const Frustum &frustum, VisibilityList &list,
	                                                      unsigned index, unsigned num_indices) const;
	void gather_visible_positional_lights_subset(const Frustum &frustum, PositionalLightList &list,
	                                             unsigned index, unsigned num_indices) const;

	size_t get_opaque_renderables_count() const;
	size_t get_motion_vector_renderables_count() const;
	size_t get_transparent_renderables_count() const;
	size_t get_static_shadow_renderables_count() const;
	size_t get_dynamic_shadow_renderables_count() const;
	size_t get_positional_lights_count() const;

	void gather_visible_render_pass_sinks(const vec3 &camera_pos, VisibilityList &list) const;
	void gather_unbounded_renderables(VisibilityList &list) const;
	void gather_opaque_floating_renderables(VisibilityList &list) const;
	EnvironmentComponent *get_environment() const;
	EntityPool &get_entity_pool();

	void add_render_passes(RenderGraph &graph);
	void add_render_pass_dependencies(RenderGraph &graph, RenderPass &main_pass,
	                                  RenderPassCreator::DependencyFlags dep_flags);
	void add_render_pass_dependencies(RenderGraph &graph);
	void set_render_pass_data(const RendererSuite *suite, const RenderContext *context);
	void bind_render_graph_resources(RenderGraph &graph);

	NodeHandle create_node();
	NodeHandle create_skinned_node(const SceneFormats::Skin &skin);

	Util::ObjectPool<Node> &get_node_pool()
	{
		return node_pool;
	}

	void set_root_node(NodeHandle node)
	{
		root_node = std::move(node);
	}

	NodeHandle get_root_node() const
	{
		return root_node;
	}

	Entity *create_renderable(AbstractRenderableHandle renderable, Node *node);
	Entity *create_light(const SceneFormats::LightInfo &light, Node *node);
	Entity *create_volumetric_diffuse_light(uvec3 resolution, Node *node);
	Entity *create_volumetric_fog_region(Node *node);
	Entity *create_volumetric_decal(Node *node);
	Entity *create_entity();
	void destroy_entity(Entity *entity);
	void queue_destroy_entity(Entity *entity);
	void destroy_queued_entities();

	template <typename T>
	void remove_entities_with_component()
	{
		remove_entities_with_component(ComponentIDMapping::get_id<T>());
	}

	void remove_entities_with_component(ComponentType id);

	TransformAllocator &get_transforms() { return transform_allocator; }
	const TransformAllocator &get_transforms() const { return transform_allocator; }
	TransformAllocatorAABB &get_aabbs() { return transform_allocator_aabb; }
	const TransformAllocatorAABB &get_aabbs() const { return transform_allocator_aabb; }
	OccluderStateAllocator &get_occluder_states() { return occluder_state_allocator; }
	const OccluderStateAllocator &get_occluder_states() const { return occluder_state_allocator; }

	struct UpdateSpan
	{
		const uint32_t *offsets;
		size_t count;
	};

	// These are not thread-safe.
	UpdateSpan get_transform_update_span() const
	{
		return { updated_transforms.data(), updated_transforms_count.load(std::memory_order_relaxed) };
	}

	UpdateSpan get_aabb_update_span() const
	{
		return { updated_aabbs.data(), updated_aabb_count.load(std::memory_order_relaxed) };
	}

	UpdateSpan get_occluder_state_update_span() const
	{
		return { cleared_occlusion_states.data(), cleared_occlusion_states_count.load(std::memory_order_relaxed) };
	}

	void clear_updates();

private:
	TransformAllocator transform_allocator;
	TransformAllocatorAABB transform_allocator_aabb;
	OccluderStateAllocator occluder_state_allocator;
	EntityPool pool;
	Util::ObjectPool<Node::Skinning> skinning_pool;
	Util::ObjectPool<Node> node_pool;
	NodeHandle root_node;

	Util::DynamicArray<uint32_t> updated_transforms;
	Util::DynamicArray<uint32_t> updated_aabbs;
	Util::DynamicArray<uint32_t> cleared_occlusion_states;
	std::atomic_size_t updated_transforms_count;
	std::atomic_size_t updated_aabb_count;
	std::atomic_size_t cleared_occlusion_states_count;

	// Sets up the default useful component groups up front.
	const ComponentGroupVector<
			BoundedComponent,
			RenderInfoComponent,
			CachedSpatialTransformTimestampComponent> &spatials;
	const ComponentGroupVector<
			RenderInfoComponent,
			RenderableComponent,
			CachedSpatialTransformTimestampComponent,
			OpaqueComponent> &opaque;
	const ComponentGroupVector<
			RenderInfoComponent,
			RenderableComponent,
			CachedSpatialTransformTimestampComponent,
			TransparentComponent> &transparent;
	const ComponentGroupVector<
			RenderInfoComponent,
			CachedSpatialTransformTimestampComponent,
			PositionalLightComponent> &positional_lights;
	const ComponentGroupVector<
			RenderInfoComponent,
			PositionalLightComponent,
			CachedSpatialTransformTimestampComponent,
			IrradianceAffectingComponent> &irradiance_affecting_positional_lights;
	const ComponentGroupVector<
			RenderInfoComponent,
			RenderableComponent,
			CachedSpatialTransformTimestampComponent,
			CastsStaticShadowComponent> &static_shadowing;
	const ComponentGroupVector<
			RenderInfoComponent,
			RenderableComponent,
			CachedSpatialTransformTimestampComponent,
			CastsDynamicShadowComponent> &dynamic_shadowing;
	const ComponentGroupVector<
			RenderPassComponent,
			RenderableComponent,
			CachedSpatialTransformTimestampComponent,
			CastsDynamicShadowComponent> &render_pass_shadowing;
	const ComponentGroupVector<
			UnboundedComponent,
			RenderableComponent> &backgrounds;
	const ComponentGroupVector<
			OpaqueFloatingComponent,
			RenderableComponent> &opaque_floating;
	const ComponentGroupVector<
			CameraComponent,
			CachedTransformComponent> &cameras;
	const ComponentGroupVector<
			DirectionalLightComponent,
			CachedTransformComponent> &directional_lights;
	const ComponentGroupVector<
			VolumetricDiffuseLightComponent,
			CachedSpatialTransformTimestampComponent,
			RenderInfoComponent> &volumetric_diffuse_lights;
	const ComponentGroupVector<
			VolumetricFogRegionComponent,
			CachedSpatialTransformTimestampComponent,
			RenderInfoComponent> &volumetric_fog_regions;
	const ComponentGroupVector<
			VolumetricDecalComponent,
			CachedSpatialTransformTimestampComponent,
			RenderInfoComponent> &volumetric_decals;

	const ComponentGroupVector<PerFrameUpdateComponent> &per_frame_updates;
	const ComponentGroupVector<PerFrameUpdateTransformComponent,
			RenderInfoComponent> &per_frame_update_transforms;
	ComponentGroupVector<PerFrameUpdateComponent> per_frame_updates_sorted;
	ComponentGroupVector<PerFrameUpdateTransformComponent,
			RenderInfoComponent> per_frame_update_transforms_sorted;

	const ComponentGroupVector<EnvironmentComponent> &environments;
	const ComponentGroupVector<RenderPassSinkComponent,
			RenderableComponent,
			CullPlaneComponent> &render_pass_sinks;
	const ComponentGroupVector<RenderPassComponent> &render_pass_creators;
	Util::IntrusiveList<Entity> entities;
	Util::IntrusiveList<Entity> queued_entities;
	void destroy_entities(Util::IntrusiveList<Entity> &entity_list);

	void update_cached_transforms_range(size_t start_index, size_t end_index);

	// New transform update system:
	enum { MaxNodeHierarchyLevels = 32 };
	void push_pending_node_update(Node *node);
	void distribute_per_level_updates(TaskGroup *group);
	void distribute_update_to_level(Node *update, unsigned level);
	void perform_per_level_updates(unsigned level, TaskGroup *group);
	Util::AtomicAppendBuffer<Node *, 8> pending_node_updates;
	Util::AtomicAppendBuffer<Node *, 8> pending_node_updates_skin;
	Util::AtomicAppendBuffer<Node *, 8> pending_node_update_per_level[MaxNodeHierarchyLevels];
	std::atomic_uint32_t pending_hierarchy_level_mask;

	void update_transform_tree(TaskComposer *composer);

	void perform_updates(Node * const *updates, size_t count);
	void update_transform_tree_node(Node &node, const mat_affine &transform);
	void update_skinning(Node &node);
	void perform_update_skinning(Node * const *updates, size_t count);
	void notify_transform_updates(uint32_t offset, uint32_t count);
	void notify_aabb_updates(uint32_t offset, uint32_t count);
	void notify_allocated_occlusion_state(uint32_t offset, uint32_t count);
	void sort_updates();
};
}
