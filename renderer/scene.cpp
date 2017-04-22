#include "scene.hpp"
#include "transforms.hpp"

using namespace std;

namespace Granite
{

Scene::Scene()
	: spatials(pool.get_component_group<BoundedComponent, CachedSpatialTransformComponent>()),
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

		if (transform->transform)
		{
			if (frustum.intersects(transform->world_aabb))
				list.push_back({renderable->renderable, transform });
		}
		else
			list.push_back({renderable->renderable, nullptr});
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

void Scene::update_transform_tree(Node &node, const mat4 &transform)
{
	compute_model_transform(node.cached_transform.world_transform, node.cached_transform.normal_transform,
                            node.transform.scale, node.transform.rotation, node.transform.translation, transform);

	for (auto &child : node.get_children())
		update_transform_tree(*child, node.cached_transform.world_transform);
}

void Scene::update_cached_transforms()
{
	if (root_node)
		update_transform_tree(*root_node, mat4(1.0f));

	for (auto &s : spatials)
	{
		BoundedComponent *aabb;
		CachedSpatialTransformComponent *cached_transform;
		tie(aabb, cached_transform) = s;

		if (cached_transform->transform)
		{
			cached_transform->world_aabb = aabb->aabb.transform(
				cached_transform->transform->world_transform);
		}
	}
}

Scene::NodeHandle Scene::create_node()
{
	return Util::make_handle<Node>();
}

void Scene::Node::add_child(NodeHandle node)
{
	assert(node->parent == nullptr);
	node->parent = this;
	children.push_back(node);
}

void Scene::Node::remove_child(Node &node)
{
	assert(node.parent == this);
	node.parent = nullptr;

	auto itr = remove_if(begin(children), end(children), [&](const NodeHandle &h) {
		return &node == h.get();
	});
	assert(itr != end(children));
	children.erase(itr, end(children));
}

EntityHandle Scene::create_renderable(AbstractRenderableHandle renderable, Node *node)
{
	EntityHandle entity = pool.create_entity();
	nodes.push_back(entity);

	if (renderable->has_static_aabb())
	{
		auto *transform = entity->allocate_component<CachedSpatialTransformComponent>();
		if (node)
			transform->transform = &node->cached_transform;
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

void Scene::animate(double t)
{
	for (auto &animation : animations)
	{
		double wrapped_time = fmod(t - animation->start_time, animation->animation.get_length());

		unsigned index;
		float phase;
		animation->animation.get_index_phase(float(wrapped_time), index, phase);

		for (auto &channel : animation->animation.channels)
		{
			auto &node = *node_id[channel.node_index];
			switch (channel.type)
			{
			case Importer::AnimationChannel::Type::Translation:
				node.transform.translation = channel.linear.sample(index, phase);
				break;
			case Importer::AnimationChannel::Type::Scale:
				node.transform.scale = channel.linear.sample(index, phase);
				break;
			case Importer::AnimationChannel::Type::Rotation:
				node.transform.rotation = channel.spherical.sample(index, phase);
				break;
			}
		}
	}
}

void Scene::start_animation(const Importer::Animation &animation, double start_time)
{
	animations.emplace_back(new AnimationState(animation, start_time, true));
}

}