#include "scene.hpp"

using namespace std;

namespace Granite
{

Scene::Scene()
	: spatials(pool.get_component_group<BoundedComponent, SpatialComponent, SpatialTransformComponent, CachedSpatialTransformComponent>()),
      opaque(pool.get_component_group<BoundedComponent, CachedSpatialTransformComponent, RenderableComponent, OpaqueComponent>()),
      transparent(pool.get_component_group<BoundedComponent, CachedSpatialTransformComponent, RenderableComponent, TransparentComponent>()),
      shadowing(pool.get_component_group<BoundedComponent, CachedSpatialTransformComponent, RenderableComponent, CastsShadowComponent>())
{

}

EntityHandle Scene::create_spatial_node()
{
	auto entity = pool.create_entity();
	entity->allocate_component<SpatialComponent>();
	entity->allocate_component<SpatialTransformComponent>();
	entity->allocate_component<CachedSpatialTransformComponent>();
	return entity;
}

EntityHandle Scene::create_renderable(AbstractRenderableHandle renderable)
{
	auto entity = create_spatial_node();
	auto *render = entity->allocate_component<RenderableComponent>();

	if (renderable->has_static_aabb())
	{
		auto *bounded = entity->allocate_component<BoundedComponent>();
		bounded->aabb = renderable->get_static_aabb();
	}
	else
		entity->allocate_component<UnboundedComponent>();

	switch (renderable->get_mesh_draw_pipeline())
	{
	case MeshDrawPipeline::AlphaBlend:
		entity->allocate_component<TransparentComponent>();
		break;

	default:
		entity->allocate_component<OpaqueComponent>();
		entity->allocate_component<CastsShadowComponent>();
		break;
	}

	render->renderable = renderable;
	return entity;
}

}