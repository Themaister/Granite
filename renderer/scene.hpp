/* Copyright (c) 2017-2019 Hans-Kristian Arntzen
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

namespace Granite
{
struct RenderableInfo
{
	AbstractRenderable *renderable;
	const RenderInfoComponent *transform;
};
using VisibilityList = std::vector<RenderableInfo>;

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

	void refresh_per_frame(RenderContext &context);
	void update_cached_transforms();
	void gather_visible_opaque_renderables(const Frustum &frustum, VisibilityList &list);
	void gather_visible_transparent_renderables(const Frustum &frustum, VisibilityList &list);
	void gather_visible_static_shadow_renderables(const Frustum &frustum, VisibilityList &list);
	void gather_visible_dynamic_shadow_renderables(const Frustum &frustum, VisibilityList &list);
	void gather_visible_positional_lights(const Frustum &frustum, VisibilityList &list,
	                                      unsigned max_spot_lights = std::numeric_limits<unsigned>::max(),
	                                      unsigned max_point_lights = std::numeric_limits<unsigned>::max());
	void gather_visible_render_pass_sinks(const vec3 &camera_pos, VisibilityList &list);
	void gather_unbounded_renderables(VisibilityList &list);
	EnvironmentComponent *get_environment() const;
	EntityPool &get_entity_pool();

	void add_render_passes(RenderGraph &graph);
	void add_render_pass_dependencies(RenderGraph &graph, RenderPass &main_pass);
	void set_render_pass_data(Renderer *forward_renderer, Renderer *deferred_renderer, Renderer *depth_renderer, const RenderContext *context);
	void bind_render_graph_resources(RenderGraph &graph);

	class Node;
	struct NodeDeleter
	{
		void operator()(Node *node);
	};

	class Node : public Util::IntrusivePtrEnabled<Node, NodeDeleter>
	{
	public:
		explicit Node(Scene *parent_)
			: parent_scene(parent_)
		{
		}

		Scene *parent_scene;
		Transform transform;
		CachedTransform cached_transform;
		CachedSkinTransform cached_skin_transform;

		void invalidate_cached_transform();
		void add_child(Util::IntrusivePtr<Node> node);
		Util::IntrusivePtr<Node> remove_child(Node *node);
		static Util::IntrusivePtr<Node> remove_node_from_hierarchy(Node *node);

		const std::vector<Util::IntrusivePtr<Node>> &get_children() const
		{
			return children;
		}

		const std::vector<Util::IntrusivePtr<Node>> &get_skeletons() const
		{
			return skeletons;
		}

		std::vector<Util::IntrusivePtr<Node>> &get_children()
		{
			return children;
		}

		std::vector<Util::IntrusivePtr<Node>> &get_skeletons()
		{
			return skeletons;
		}

		Node *get_parent() const
		{
			return parent;
		}

		struct Skinning
		{
			std::vector<Transform *> skin;
			std::vector<CachedTransform *> cached_skin;
			Util::Hash skin_compat = 0;
		};

		Skinning &get_skin()
		{
			return skinning;
		}

		inline bool get_and_clear_child_transform_dirty()
		{
			auto ret = any_child_transform_dirty;
			any_child_transform_dirty = false;
			return ret;
		}

		inline bool get_and_clear_transform_dirty()
		{
			auto ret = cached_transform_dirty;
			cached_transform_dirty = false;
			return ret;
		}

		mat4 initial_transform = mat4(1.0f);

		void update_timestamp()
		{
			timestamp++;
		}

		const uint32_t *get_timestamp_pointer() const
		{
			return &timestamp;
		}

	private:
		std::vector<Util::IntrusivePtr<Node>> children;
		std::vector<Util::IntrusivePtr<Node>> skeletons;
		Skinning skinning;
		Node *parent = nullptr;

		bool any_child_transform_dirty = true;
		bool cached_transform_dirty = true;
		uint32_t timestamp = 0;
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
	Util::ObjectPool<Node> node_pool;
	NodeHandle root_node;
	ComponentGroupVector<BoundedComponent, RenderInfoComponent, CachedSpatialTransformTimestampComponent> &spatials;
	ComponentGroupVector<RenderInfoComponent, RenderableComponent, OpaqueComponent> &opaque;
	ComponentGroupVector<RenderInfoComponent, RenderableComponent, TransparentComponent> &transparent;
	ComponentGroupVector<RenderInfoComponent, RenderableComponent, PositionalLightComponent> &positional_lights;
	ComponentGroupVector<RenderInfoComponent, RenderableComponent, CastsStaticShadowComponent> &static_shadowing;
	ComponentGroupVector<RenderInfoComponent, RenderableComponent, CastsDynamicShadowComponent> &dynamic_shadowing;
	ComponentGroupVector<RenderPassComponent, RenderableComponent, CastsDynamicShadowComponent> &render_pass_shadowing;
	ComponentGroupVector<UnboundedComponent, RenderableComponent> &backgrounds;
	ComponentGroupVector<CameraComponent, CachedTransformComponent> &cameras;
	ComponentGroupVector<DirectionalLightComponent, CachedTransformComponent> &directional_lights;
	ComponentGroupVector<AmbientLightComponent> &ambient_lights;
	ComponentGroupVector<PerFrameUpdateComponent> &per_frame_updates;
	ComponentGroupVector<PerFrameUpdateTransformComponent, RenderInfoComponent> &per_frame_update_transforms;
	ComponentGroupVector<EnvironmentComponent> &environments;
	ComponentGroupVector<RenderPassSinkComponent, RenderableComponent, CullPlaneComponent> &render_pass_sinks;
	ComponentGroupVector<RenderPassComponent> &render_pass_creators;
	Util::IntrusiveList<Entity> entities;
	Util::IntrusiveList<Entity> queued_entities;
	void destroy_entities(Util::IntrusiveList<Entity> &entity_list);
	void update_transform_tree(Node &node, const mat4 &transform, bool parent_is_dirty);

	void update_skinning(Node &node);
};
}