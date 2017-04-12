#include "scene.hpp"
#include "transforms.hpp"

using namespace std;

namespace Granite
{

Scene::Scene()
	: spatials(pool.get_component_group<BoundedComponent, SpatialTransformComponent, CachedSpatialTransformComponent>()),
      opaque(pool.get_component_group<BoundedComponent, CachedSpatialTransformComponent, RenderableComponent, OpaqueComponent>()),
      transparent(pool.get_component_group<BoundedComponent, CachedSpatialTransformComponent, RenderableComponent, TransparentComponent>()),
      shadowing(pool.get_component_group<BoundedComponent, CachedSpatialTransformComponent, RenderableComponent, CastsShadowComponent>()),
      backgrounds(pool.get_component_group<UnboundedComponent, RenderableComponent>())
{

}

void Scene::update_cached_transforms()
{
	for (auto &s : spatials)
	{
		BoundedComponent *aabb;
		SpatialTransformComponent *transform;
		CachedSpatialTransformComponent *cached_transform;
		tie(aabb, transform, cached_transform) = s;

		compute_model_transform(cached_transform->world_transform, cached_transform->normal_transform,
		                        transform->scale, transform->rotation, transform->translation);
		cached_transform->world_aabb = aabb->aabb.transform(cached_transform->world_transform);
	}
}

EntityHandle Scene::create_renderable(AbstractRenderableHandle renderable)
{
	EntityHandle entity = pool.create_entity();
	nodes.push_back(entity);

	if (renderable->has_static_aabb())
	{
		entity->allocate_component<SpatialTransformComponent>();
		entity->allocate_component<CachedSpatialTransformComponent>();
		auto *bounded = entity->allocate_component<BoundedComponent>();
		bounded->aabb = renderable->get_static_aabb();
	}
	else
		entity->allocate_component<UnboundedComponent>();

	auto *render = entity->allocate_component<RenderableComponent>();

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