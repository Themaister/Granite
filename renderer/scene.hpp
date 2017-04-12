#pragma once

#include "ecs.hpp"
#include "render_components.hpp"
#include <tuple>

namespace Granite
{
class Scene
{
public:
	Scene();
	EntityHandle create_spatial_node();
	EntityHandle create_renderable(AbstractRenderableHandle renderable);

private:
	EntityPool pool;
	std::vector<std::tuple<BoundedComponent*, SpatialComponent*, SpatialTransformComponent*, CachedSpatialTransformComponent*>> &spatials;
	std::vector<std::tuple<BoundedComponent*, CachedSpatialTransformComponent*, RenderableComponent*, OpaqueComponent*>> &opaque;
	std::vector<std::tuple<BoundedComponent*, CachedSpatialTransformComponent*, RenderableComponent*, TransparentComponent*>> &transparent;
	std::vector<std::tuple<BoundedComponent*, CachedSpatialTransformComponent*, RenderableComponent*, CastsShadowComponent*>> &shadowing;
	std::vector<EntityHandle> nodes;
};
}