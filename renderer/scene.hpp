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
	EntityHandle create_renderable(AbstractRenderableHandle renderable);

	void update_cached_transforms();
	void gather_visible_opaque_renderables(const Frustum &frustum, VisibilityList &list);
	void gather_visible_transparent_renderables(const Frustum &frustum, VisibilityList &list);
	void gather_visible_shadow_renderables(const Frustum &frustum, VisibilityList &list);

private:
	EntityPool pool;
	std::vector<std::tuple<BoundedComponent*, SpatialTransformComponent*, CachedSpatialTransformComponent*>> &spatials;
	std::vector<std::tuple<CachedSpatialTransformComponent*, RenderableComponent*, OpaqueComponent*>> &opaque;
	std::vector<std::tuple<CachedSpatialTransformComponent*, RenderableComponent*, TransparentComponent*>> &transparent;
	std::vector<std::tuple<CachedSpatialTransformComponent*, RenderableComponent*, CastsShadowComponent*>> &shadowing;
	std::vector<std::tuple<UnboundedComponent*, RenderableComponent*>> &backgrounds;
	std::vector<EntityHandle> nodes;
};
}