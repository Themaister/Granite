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
	EntityHandle create_renderable(AbstractRenderableHandle renderable);

	void update_cached_transforms();

private:
	EntityPool pool;
	std::vector<std::tuple<BoundedComponent*, SpatialTransformComponent*, CachedSpatialTransformComponent*>> &spatials;
	std::vector<std::tuple<BoundedComponent*, CachedSpatialTransformComponent*, RenderableComponent*, OpaqueComponent*>> &opaque;
	std::vector<std::tuple<BoundedComponent*, CachedSpatialTransformComponent*, RenderableComponent*, TransparentComponent*>> &transparent;
	std::vector<std::tuple<BoundedComponent*, CachedSpatialTransformComponent*, RenderableComponent*, CastsShadowComponent*>> &shadowing;
	std::vector<std::tuple<UnboundedComponent*, RenderableComponent*>> &backgrounds;
	std::vector<EntityHandle> nodes;
};
}