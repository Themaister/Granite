#include "scene.hpp"
#include "transforms.hpp"

using namespace std;

namespace Granite
{

Scene::Scene()
	: spatials(pool.get_component_group<BoundedComponent, SpatialTransformComponent, CachedSpatialTransformComponent>()),
      opaque(pool.get_component_group<CachedSpatialTransformComponent, RenderableComponent, OpaqueComponent>()),
      transparent(pool.get_component_group<CachedSpatialTransformComponent, RenderableComponent, TransparentComponent>()),
      shadowing(pool.get_component_group<CachedSpatialTransformComponent, RenderableComponent, CastsShadowComponent>()),
      backgrounds(pool.get_component_group<UnboundedComponent, RenderableComponent>())
{

}

template <typename T>
static void gather_visible_renderables(const Frustum &frustum, VisibilityList &list, const T &objects)
{
	for (auto &o : objects)
	{
		auto *transform = get<0>(o);
		auto *renderable = get<1>(o);

		if (frustum.intersects(transform->world_aabb))
			list.push_back({ renderable->renderable, transform });
	}
}

void Scene::gather_background_renderables(VisibilityList &list)
{
	for (auto &background : backgrounds)
		list.push_back({ get<1>(background)->renderable, nullptr });
}

void Scene::gather_visible_opaque_renderables(const Frustum &frustum, VisibilityList &list)
{
	gather_visible_renderables(frustum, list, opaque);
}

void Scene::gather_visible_transparent_renderables(const Frustum &frustum, VisibilityList &list)
{
	gather_visible_renderables(frustum, list, transparent);
}

void Scene::gather_visible_shadow_renderables(const Frustum &frustum, VisibilityList &list)
{
	gather_visible_renderables(frustum, list, shadowing);
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