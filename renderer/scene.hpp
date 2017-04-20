#pragma once

#include "ecs.hpp"
#include "render_components.hpp"
#include "frustum.hpp"
#include <tuple>

namespace Granite
{
struct RenderableInfo
{
	AbstractRenderableHandle renderable;
	const CachedSpatialTransformComponent *transform;
};
using VisibilityList = std::vector<RenderableInfo>;

class Scene
{
public:
	Scene();

	void update_cached_transforms();
	void gather_visible_opaque_renderables(const Frustum &frustum, VisibilityList &list);
	void gather_visible_transparent_renderables(const Frustum &frustum, VisibilityList &list);
	void gather_visible_shadow_renderables(const Frustum &frustum, VisibilityList &list);
	void gather_background_renderables(VisibilityList &list);

	class Node : public Util::IntrusivePtrEnabled<Node>
	{
	public:
		Transform transform;
		CachedTransform cached_transform;

		void add_child(Util::IntrusivePtr<Node> node);
		void remove_child(Node &node);

		const std::vector<Util::IntrusivePtr<Node>> &get_children() const
		{
			return children;
		}

		std::vector<Util::IntrusivePtr<Node>> &get_children()
		{
			return children;
		}

		Node *get_parent() const
		{
			return parent;
		}

	private:
		std::vector<Util::IntrusivePtr<Node>> children;
		Node *parent = nullptr;
	};
	using NodeHandle = Util::IntrusivePtr<Node>;
	NodeHandle create_node();

	void set_root_node(NodeHandle node)
	{
		root_node = node;
	}

	EntityHandle create_renderable(AbstractRenderableHandle renderable, Node *node);

private:
	EntityPool pool;
	NodeHandle root_node;
	std::vector<std::tuple<BoundedComponent*, CachedSpatialTransformComponent*>> &spatials;
	std::vector<std::tuple<CachedSpatialTransformComponent*, RenderableComponent*, OpaqueComponent*>> &opaque;
	std::vector<std::tuple<CachedSpatialTransformComponent*, RenderableComponent*, TransparentComponent*>> &transparent;
	std::vector<std::tuple<CachedSpatialTransformComponent*, RenderableComponent*, CastsShadowComponent*>> &shadowing;
	std::vector<std::tuple<UnboundedComponent*, RenderableComponent*>> &backgrounds;
	std::vector<EntityHandle> nodes;
	void update_transform_tree(Node &node, const mat4 &transform);
};
}