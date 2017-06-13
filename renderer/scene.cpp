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
	  backgrounds(pool.get_component_group<UnboundedComponent, RenderableComponent>()),
	  per_frame_updates(pool.get_component_group<PerFrameUpdateComponent>())
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
				list.push_back({ renderable->renderable.get(), transform });
		}
		else
			list.push_back({ renderable->renderable.get(), nullptr});
	}
}

void Scene::refresh_per_frame(RenderContext &context)
{
	for (auto &update : per_frame_updates)
	{
		auto *refresh = get<0>(update)->refresh;
		if (refresh)
			refresh->refresh(context);
	}
}

void Scene::gather_background_renderables(VisibilityList &list)
{
	for (auto &background : backgrounds)
		list.push_back({ get<1>(background)->renderable.get(), nullptr });
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

void Scene::update_skinning(Node &node)
{
	if (!node.cached_skin_transform.bone_world_transforms.empty())
	{
		unsigned len = unsigned(node.get_skin().cached_skin.size());
		assert(node.get_skin().cached_skin.size() == node.cached_skin_transform.bone_world_transforms.size());
		assert(node.get_skin().cached_skin.size() == node.cached_skin_transform.bone_normal_transforms.size());
		for (unsigned i = 0; i < len; i++)
		{
			node.cached_skin_transform.bone_world_transforms[i] = node.get_skin().cached_skin[i]->world_transform;
			node.cached_skin_transform.bone_normal_transforms[i] = node.get_skin().cached_skin[i]->normal_transform;
		}
	}
}

void Scene::update_transform_tree(Node &node, const mat4 &transform)
{
	compute_model_transform(node.cached_transform.world_transform,
                            node.transform.scale, node.transform.rotation, node.transform.translation, transform);

	for (auto &child : node.get_children())
		update_transform_tree(*child, node.cached_transform.world_transform);
	for (auto &child : node.get_skeletons())
		update_transform_tree(*child, node.cached_transform.world_transform);

	// Apply the first transformation in the sequence, this is used for skinning.
	node.cached_transform.world_transform = node.cached_transform.world_transform * node.initial_transform;

	compute_normal_transform(node.cached_transform.normal_transform, node.cached_transform.world_transform);
	update_skinning(node);
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
			if (cached_transform->skin_transform)
			{
				// TODO: Isolate the AABB per bone.
				cached_transform->world_aabb = AABB(vec3(FLT_MAX), vec3(-FLT_MAX));
				for (auto &m : cached_transform->skin_transform->bone_world_transforms)
					cached_transform->world_aabb.expand(aabb->aabb.transform(m));
			}
			else
			{
				cached_transform->world_aabb = aabb->aabb.transform(
					cached_transform->transform->world_transform);
			}
		}
	}
}

Scene::NodeHandle Scene::create_node()
{
	return Util::make_handle<Node>();
}

static void add_bone(Scene::NodeHandle *bones, uint32_t parent, const Importer::Skin::Bone &bone)
{
	bones[parent]->get_skeletons().push_back(bones[bone.index]);
	for (auto &child : bone.children)
		add_bone(bones, bone.index, child);
}

Scene::NodeHandle Scene::create_skinned_node(const Importer::Skin &skin)
{
	auto node = create_node();

	vector<NodeHandle> bones;
	bones.reserve(skin.joint_transforms.size());

	for (size_t i = 0; i < skin.joint_transforms.size(); i++)
		bones.push_back(create_node());

	for (size_t i = 0; i < skin.joint_transforms.size(); i++)
	{
		bones[i]->transform.translation = skin.joint_transforms[i].translation;
		bones[i]->transform.scale = skin.joint_transforms[i].scale;
		bones[i]->transform.rotation = skin.joint_transforms[i].rotation;
		bones[i]->initial_transform = skin.inverse_bind_pose[i];
	}

	node->cached_skin_transform.bone_world_transforms.resize(skin.joint_transforms.size());
	node->cached_skin_transform.bone_normal_transforms.resize(skin.joint_transforms.size());

	auto &node_skin = node->get_skin();
	node_skin.cached_skin.reserve(skin.joint_transforms.size());
	node_skin.skin.reserve(skin.joint_transforms.size());
	for (size_t i = 0; i < skin.joint_transforms.size(); i++)
	{
		node_skin.skin.push_back(&bones[i]->transform);
		node_skin.cached_skin.push_back(&bones[i]->cached_transform);
	}

	for (auto &skeleton : skin.skeletons)
	{
		node->get_skeletons().push_back(bones[skeleton.index]);
		for (auto &child : skeleton.children)
			add_bone(bones.data(), skeleton.index, child);
	}

	node_skin.skin_compat = skin.skin_compat;
	return node;
}

void Scene::Node::add_child(NodeHandle node)
{
	assert(this != node.get());
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
		{
			transform->transform = &node->cached_transform;
			if (!node->get_skin().cached_skin.empty())
				transform->skin_transform = &node->cached_skin_transform;
		}
		auto *bounded = entity->allocate_component<BoundedComponent>();
		bounded->aabb = renderable->get_static_aabb();
	}
	else
		entity->allocate_component<UnboundedComponent>();

	auto *render = entity->allocate_component<RenderableComponent>();

	switch (renderable->get_mesh_draw_pipeline())
	{
	case DrawPipeline::AlphaBlend:
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
