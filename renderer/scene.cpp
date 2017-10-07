/* Copyright (c) 2017 Hans-Kristian Arntzen
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

#include "scene.hpp"
#include "transforms.hpp"
#include "lights.hpp"

using namespace std;

namespace Granite
{

Scene::Scene()
	: spatials(pool.get_component_group<BoundedComponent, CachedSpatialTransformComponent, CachedSpatialTransformTimestampComponent>()),
	  opaque(pool.get_component_group<CachedSpatialTransformComponent, RenderableComponent, OpaqueComponent>()),
	  transparent(pool.get_component_group<CachedSpatialTransformComponent, RenderableComponent, TransparentComponent>()),
	  positional_lights(pool.get_component_group<CachedSpatialTransformComponent, RenderableComponent, PositionalLightComponent>()),
	  static_shadowing(pool.get_component_group<CachedSpatialTransformComponent, RenderableComponent, CastsStaticShadowComponent>()),
	  dynamic_shadowing(pool.get_component_group<CachedSpatialTransformComponent, RenderableComponent, CastsDynamicShadowComponent>()),
	  render_pass_shadowing(pool.get_component_group<RenderPassComponent, RenderableComponent, CastsDynamicShadowComponent>()),
	  backgrounds(pool.get_component_group<UnboundedComponent, RenderableComponent>()),
	  cameras(pool.get_component_group<CameraComponent, CachedTransformComponent>()),
	  directional_lights(pool.get_component_group<DirectionalLightComponent, CachedTransformComponent>()),
	  ambient_lights(pool.get_component_group<AmbientLightComponent>()),
	  per_frame_updates(pool.get_component_group<PerFrameUpdateComponent>()),
	  per_frame_update_transforms(pool.get_component_group<PerFrameUpdateTransformComponent, CachedSpatialTransformComponent>()),
	  environments(pool.get_component_group<EnvironmentComponent>()),
	  render_pass_sinks(pool.get_component_group<RenderPassSinkComponent, RenderableComponent, CullPlaneComponent>()),
	  render_pass_creators(pool.get_component_group<RenderPassComponent>())
{

}

Scene::~Scene()
{
	// Makes shutdown way faster :)
	pool.reset_groups();
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
			if (frustum.intersects_fast(transform->world_aabb))
				list.push_back({ renderable->renderable.get(), transform });
		}
		else
			list.push_back({ renderable->renderable.get(), nullptr});
	}
}

void Scene::add_render_passes(RenderGraph &graph)
{
	for (auto &pass : render_pass_creators)
	{
		auto *rpass = get<0>(pass)->creator;
		rpass->add_render_passes(graph);
	}
}

void Scene::add_render_pass_dependencies(RenderGraph &graph, RenderPass &main_pass)
{
	for (auto &pass : render_pass_creators)
	{
		auto *rpass = get<0>(pass)->creator;
		rpass->setup_render_pass_dependencies(graph, main_pass);
	}
}

void Scene::set_render_pass_data(Renderer *renderer, const RenderContext *context)
{
	for (auto &pass : render_pass_creators)
	{
		auto *rpass = get<0>(pass)->creator;
		rpass->set_base_renderer(renderer);
		rpass->set_base_render_context(context);
		rpass->set_scene(this);
	}
}

void Scene::bind_render_graph_resources(RenderGraph &graph)
{
	for (auto &pass : render_pass_creators)
	{
		auto *rpass = get<0>(pass)->creator;
		rpass->setup_render_pass_resources(graph);
	}
}

void Scene::refresh_per_frame(RenderContext &context)
{
	for (auto &update : per_frame_update_transforms)
	{
		auto *refresh = get<0>(update)->refresh;
		auto *transform = get<1>(update);
		if (refresh)
			refresh->refresh(context, transform);
	}

	for (auto &update : per_frame_updates)
	{
		auto *refresh = get<0>(update)->refresh;
		if (refresh)
			refresh->refresh(context);
	}
}

EnvironmentComponent *Scene::get_environment() const
{
	if (environments.empty())
		return nullptr;
	else
		return get<0>(environments.front());
}

EntityPool &Scene::get_entity_pool()
{
	return pool;
}

void Scene::gather_background_renderables(VisibilityList &list)
{
	for (auto &background : backgrounds)
		list.push_back({ get<1>(background)->renderable.get(), nullptr });
}

void Scene::gather_visible_render_pass_sinks(const vec3 &camera_pos, VisibilityList &list)
{
	for (auto &sink : render_pass_sinks)
	{
		auto &plane = get<2>(sink)->plane;
		if (dot(vec4(camera_pos, 1.0f), plane) > 0.0f)
			list.push_back({get<1>(sink)->renderable.get(), nullptr});
	}
}

void Scene::gather_visible_opaque_renderables(const Frustum &frustum, VisibilityList &list)
{
	gather_visible_renderables(frustum, list, opaque);
}

void Scene::gather_visible_transparent_renderables(const Frustum &frustum, VisibilityList &list)
{
	gather_visible_renderables(frustum, list, transparent);
}

void Scene::gather_visible_static_shadow_renderables(const Frustum &frustum, VisibilityList &list)
{
	gather_visible_renderables(frustum, list, static_shadowing);
}

void Scene::gather_visible_positional_lights(const Frustum &frustum, VisibilityList &list)
{
	gather_visible_renderables(frustum, list, positional_lights);
}

void Scene::gather_visible_dynamic_shadow_renderables(const Frustum &frustum, VisibilityList &list)
{
	gather_visible_renderables(frustum, list, dynamic_shadowing);
	for (auto &object : render_pass_shadowing)
		list.push_back({ get<1>(object)->renderable.get(), nullptr });
}

#if 0
static void log_node_transforms(const Scene::Node &node)
{
	for (unsigned i = 0; i < node.cached_skin_transform.bone_world_transforms.size(); i++)
	{
		LOGI("Joint #%u:\n", i);

		const auto &ibp = node.cached_skin_transform.bone_world_transforms[i];
		LOGI(" Transform:\n"
				     "      [%f, %f, %f, %f]\n"
				     "      [%f, %f, %f, %f]\n"
				     "      [%f, %f, %f, %f]\n"
				     "      [%f, %f, %f, %f]\n",
		     ibp[0][0], ibp[1][0], ibp[2][0], ibp[3][0],
		     ibp[0][1], ibp[1][1], ibp[2][1], ibp[3][1],
		     ibp[0][2], ibp[1][2], ibp[2][2], ibp[3][2],
		     ibp[0][3], ibp[1][3], ibp[2][3], ibp[3][3]);
	}
}
#endif

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

		//log_node_transforms(node);
	}
}

void Scene::update_transform_tree(Node &node, const mat4 &transform, bool parent_is_dirty)
{
	bool transform_dirty = node.get_and_clear_transform_dirty() || parent_is_dirty;

	if (transform_dirty)
	{
		compute_model_transform(node.cached_transform.world_transform,
		                        node.transform.scale, node.transform.rotation, node.transform.translation, transform);
	}

	if (node.get_and_clear_child_transform_dirty() || transform_dirty)
	{
		for (auto &child : node.get_children())
			update_transform_tree(*child, node.cached_transform.world_transform, transform_dirty);
	}

	if (transform_dirty)
	{
		for (auto &child : node.get_skeletons())
			update_transform_tree(*child, node.cached_transform.world_transform, true);

		// Apply the first transformation in the sequence, this is used for skinning.
		node.cached_transform.world_transform = node.cached_transform.world_transform * node.initial_transform;

		compute_normal_transform(node.cached_transform.normal_transform, node.cached_transform.world_transform);
		update_skinning(node);
		node.update_timestamp();
	}
}

void Scene::update_cached_transforms()
{
	if (root_node)
		update_transform_tree(*root_node, mat4(1.0f), false);

	for (auto &s : spatials)
	{
		BoundedComponent *aabb;
		CachedSpatialTransformComponent *cached_transform;
		CachedSpatialTransformTimestampComponent *timestamp;
		tie(aabb, cached_transform, timestamp) = s;

		if (timestamp->last_timestamp != *timestamp->current_timestamp)
		{
			if (cached_transform->transform)
			{
				if (cached_transform->skin_transform)
				{
					// TODO: Isolate the AABB per bone.
					cached_transform->world_aabb = AABB(vec3(FLT_MAX), vec3(-FLT_MAX));
					for (auto &m : cached_transform->skin_transform->bone_world_transforms)
						cached_transform->world_aabb.expand(aabb->aabb->transform(m));
				}
				else
				{
					cached_transform->world_aabb = aabb->aabb->transform(
						cached_transform->transform->world_transform);
				}
			}
			timestamp->last_timestamp = *timestamp->current_timestamp;
		}
	}

	// Update camera transforms.
	for (auto &c : cameras)
	{
		CameraComponent *cam;
		CachedTransformComponent *transform;
		tie(cam, transform) = c;
		cam->camera.set_transform(transform->transform->world_transform);
	}

	// Update directional light transforms.
	for (auto &light : directional_lights)
	{
		DirectionalLightComponent *l;
		CachedTransformComponent *transform;
		tie(l, transform) = light;

		// v = [0, 0, 1, 0].
		l->direction = normalize(transform->transform->world_transform[2].xyz());
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
	{
		bones.push_back(create_node());
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
	node->invalidate_cached_transform();
	children.push_back(node);
}

void Scene::Node::remove_child(Node &node)
{
	assert(node.parent == this);
	node.parent = nullptr;
	node.invalidate_cached_transform();

	auto itr = remove_if(begin(children), end(children), [&](const NodeHandle &h) {
		return &node == h.get();
	});
	assert(itr != end(children));
	children.erase(itr, end(children));
}

void Scene::Node::invalidate_cached_transform()
{
	if (!cached_transform_dirty)
	{
		cached_transform_dirty = true;
		for (auto *p = parent; p && !p->any_child_transform_dirty; p = p->parent)
			p->any_child_transform_dirty = true;
	}
}

EntityHandle Scene::create_entity()
{
	EntityHandle entity = pool.create_entity();
	nodes.push_back(entity);
	return entity;
}

EntityHandle Scene::create_light(const Importer::LightInfo &light, Node *node)
{
	EntityHandle entity = pool.create_entity();
	nodes.push_back(entity);

	switch (light.type)
	{
	case Importer::LightInfo::Type::Directional:
	{
		auto *dir = entity->allocate_component<DirectionalLightComponent>();
		auto *transform = entity->allocate_component<CachedTransformComponent>();
		transform->transform = &node->cached_transform;
		dir->color = light.color;
		break;
	}

	case Importer::LightInfo::Type::Ambient:
	{
		auto *ambient = entity->allocate_component<AmbientLightComponent>();
		ambient->color = light.color;
		break;
	}

	case Importer::LightInfo::Type::Point:
	case Importer::LightInfo::Type::Spot:
	{
		AbstractRenderableHandle renderable;
		if (light.type == Importer::LightInfo::Type::Point)
			renderable = Util::make_abstract_handle<AbstractRenderable, PointLight>();
		else
		{
			renderable = Util::make_abstract_handle<AbstractRenderable, SpotLight>();
			auto &spot = static_cast<SpotLight &>(*renderable);
			spot.set_spot_parameters(light.inner_cone, light.outer_cone);
		}

		auto &positional = static_cast<PositionalLight &>(*renderable);
		positional.set_color(light.color);
		positional.set_falloff(light.constant_falloff, light.linear_falloff, light.quadratic_falloff);

		entity->allocate_component<PositionalLightComponent>()->light = &positional;
		entity->allocate_component<RenderableComponent>()->renderable = renderable;

		auto *transform = entity->allocate_component<CachedSpatialTransformComponent>();
		auto *timestamp = entity->allocate_component<CachedSpatialTransformTimestampComponent>();
		if (node)
		{
			transform->transform = &node->cached_transform;
			timestamp->current_timestamp = node->get_timestamp_pointer();
		}

		auto *bounded = entity->allocate_component<BoundedComponent>();
		bounded->aabb = renderable->get_static_aabb();
		break;
	}
	}
	return entity;
}

EntityHandle Scene::create_renderable(AbstractRenderableHandle renderable, Node *node)
{
	EntityHandle entity = pool.create_entity();
	nodes.push_back(entity);

	if (renderable->has_static_aabb())
	{
		auto *transform = entity->allocate_component<CachedSpatialTransformComponent>();
		auto *timestamp = entity->allocate_component<CachedSpatialTransformTimestampComponent>();
		if (node)
		{
			transform->transform = &node->cached_transform;
			timestamp->current_timestamp = node->get_timestamp_pointer();

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
		if (renderable->has_static_aabb())
		{
			// TODO: Find a way to make this smarter.
			entity->allocate_component<CastsStaticShadowComponent>();
			entity->allocate_component<CastsDynamicShadowComponent>();
		}
		break;
	}

	render->renderable = renderable;
	return entity;
}

}
