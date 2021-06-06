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

#include "ecs.hpp"
#include "render_components.hpp"
#include "frustum.hpp"
#include "scene_formats.hpp"
#include "no_init_pod.hpp"
#include "thread_group.hpp"
#include "atomic_append_buffer.hpp"
#include <atomic>

namespace Granite
{

class RenderContext;
struct EnvironmentComponent;

class Scene
{
public:
	Scene();
	~Scene();

	// Non-copyable, movable.
	Scene(const Scene &) = delete;
	void operator=(const Scene &) = delete;

	void refresh_per_frame(const RenderContext &context, TaskComposer &composer);

	void update_all_transforms();
	void update_transform_tree();
	//void update_transform_tree(TaskComposer &composer);
	void update_transform_listener_components();
	void update_cached_transforms_subset(unsigned index, unsigned num_indices);
	size_t get_cached_transforms_count() const;

	void gather_visible_opaque_renderables(const Frustum &frustum, VisibilityList &list) const;
	void gather_visible_transparent_renderables(const Frustum &frustum, VisibilityList &list) const;
	void gather_visible_static_shadow_renderables(const Frustum &frustum, VisibilityList &list) const;
	void gather_visible_dynamic_shadow_renderables(const Frustum &frustum, VisibilityList &list) const;
	void gather_visible_positional_lights(const Frustum &frustum, VisibilityList &list) const;
	void gather_visible_positional_lights(const Frustum &frustum, PositionalLightList &list) const;
	void gather_irradiance_affecting_positional_lights(PositionalLightList &list) const;
	void gather_visible_volumetric_diffuse_lights(const Frustum &frustum, VolumetricDiffuseLightList &list) const;

	void gather_visible_opaque_renderables_subset(const Frustum &frustum, VisibilityList &list,
	                                              unsigned index, unsigned num_indices) const;
	void gather_visible_transparent_renderables_subset(const Frustum &frustum, VisibilityList &list,
	                                                   unsigned index, unsigned num_indices) const;
	void gather_visible_static_shadow_renderables_subset(const Frustum &frustum, VisibilityList &list,
	                                                     unsigned index, unsigned num_indices) const;
	void gather_visible_dynamic_shadow_renderables_subset(const Frustum &frustum, VisibilityList &list,
	                                                      unsigned index, unsigned num_indices) const;
	void gather_visible_positional_lights_subset(const Frustum &frustum, VisibilityList &list,
	                                             unsigned index, unsigned num_indices) const;
	void gather_visible_positional_lights_subset(const Frustum &frustum, PositionalLightList &list,
	                                             unsigned index, unsigned num_indices) const;

	size_t get_opaque_renderables_count() const;
	size_t get_transparent_renderables_count() const;
	size_t get_static_shadow_renderables_count() const;
	size_t get_dynamic_shadow_renderables_count() const;
	size_t get_positional_lights_count() const;

	void gather_visible_render_pass_sinks(const vec3 &camera_pos, VisibilityList &list) const;
	void gather_unbounded_renderables(VisibilityList &list) const;
	EnvironmentComponent *get_environment() const;
	EntityPool &get_entity_pool();

	void add_render_passes(RenderGraph &graph);
	void add_render_pass_dependencies(RenderGraph &graph, RenderPass &main_pass);
	void set_render_pass_data(const RendererSuite *suite, const RenderContext *context);
	void bind_render_graph_resources(RenderGraph &graph);

	class Node;
	struct NodeDeleter
	{
		void operator()(Node *node);
	};

	// TODO: Need to slim this down, and be more data oriented.
	// Should possibly maintain separate large buffers with transform matrices, and just point to those
	// in the node.
	class Node : public Util::IntrusivePtrEnabled<Node, NodeDeleter>
	{
	public:
		explicit Node(Scene *parent_)
			: parent_scene(parent_)
			, transform(*parent_scene->transform_pool.allocate())
			, cached_transform(*parent_scene->cached_transform_pool.allocate())
		{
			node_is_pending_update.store(false, std::memory_order_relaxed);
			invalidate_cached_transform();
			assert(node_is_pending_update.is_lock_free());
		}

		~Node()
		{
			if (skinning)
				parent_scene->skinning_pool.free(skinning);
			parent_scene->transform_pool.free(&transform);
			parent_scene->cached_transform_pool.free(&cached_transform);
		}

		Scene *parent_scene;
		Transform &transform;
		CachedTransform &cached_transform;

		void invalidate_cached_transform();
		void add_child(Util::IntrusivePtr<Node> node);
		Util::IntrusivePtr<Node> remove_child(Node *node);
		static Util::IntrusivePtr<Node> remove_node_from_hierarchy(Node *node);

		inline const std::vector<Util::IntrusivePtr<Node>> &get_children() const
		{
			return children;
		}

		inline std::vector<Util::IntrusivePtr<Node>> &get_children()
		{
			return children;
		}

		inline Node *get_parent() const
		{
			return parent;
		}

		struct Skinning
		{
			CachedSkinTransform cached_skin_transform;
			std::vector<const CachedTransform *> cached_skin;
			std::vector<Transform *> skin;
			std::vector<mat4> inverse_bind_poses;
			Util::Hash skin_compat = 0;
		};

		inline void set_skin(Skinning *skinning_)
		{
			if (skinning)
				parent_scene->skinning_pool.free(skinning);
			skinning = skinning_;
		}

		inline Skinning *get_skin()
		{
			return skinning;
		}

		inline void update_timestamp()
		{
			timestamp++;
		}

		inline const uint32_t *get_timestamp_pointer() const
		{
			return &timestamp;
		}

		unsigned get_dirty_transform_depth() const;

		inline bool test_and_set_pending_update_no_atomic()
		{
			bool value = node_is_pending_update.load(std::memory_order_relaxed);
			if (!value)
				node_is_pending_update.store(true, std::memory_order_relaxed);
			return value;
		}

		inline void clear_pending_update_no_atomic()
		{
			node_is_pending_update.store(false, std::memory_order_relaxed);
		}

	private:
		std::vector<Util::IntrusivePtr<Node>> children;
		Skinning *skinning = nullptr;
		Node *parent = nullptr;
		uint32_t timestamp = 0;
		std::atomic<bool> node_is_pending_update;
	};
	using NodeHandle = Util::IntrusivePtr<Node>;
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

private:
	EntityPool pool;
	Util::ObjectPool<Node::Skinning> skinning_pool;
	Util::ObjectPool<Transform> transform_pool;
	Util::ObjectPool<CachedTransform> cached_transform_pool;
	Util::ObjectPool<Node> node_pool;
	NodeHandle root_node;

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
			RenderableComponent,
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
			CameraComponent,
			CachedTransformComponent> &cameras;
	const ComponentGroupVector<
			DirectionalLightComponent,
			CachedTransformComponent> &directional_lights;
	const ComponentGroupVector<
			VolumetricDiffuseLightComponent,
			CachedSpatialTransformTimestampComponent,
			RenderInfoComponent> &volumetric_diffuse_lights;

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

	static void update_transform_tree_node(Node &node, const mat4 &transform);

	void update_cached_transforms_range(size_t start_index, size_t end_index);

	// New transform update system:
	enum { MaxNodeHierarchyLevels = 32 };
	void push_pending_node_update(Node *node);
	void distribute_per_level_updates();
	void distribute_update_to_level(Node *update, unsigned level);
	void perform_per_level_updates(unsigned level);
	Util::AtomicAppendBuffer<Node *, 8> pending_node_updates;
	Util::AtomicAppendBuffer<Node *, 8> pending_node_updates_skin;
	Util::AtomicAppendBuffer<Node *, 8> pending_node_update_per_level[MaxNodeHierarchyLevels];
};
}