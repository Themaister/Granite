/* Copyright (c) 2017-2020 Hans-Kristian Arntzen
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
#include "lights/lights.hpp"
#include "simd.hpp"
#include "task_composer.hpp"
#include <float.h>

using namespace std;

namespace Granite
{

Scene::Scene()
	: spatials(pool.get_component_group<BoundedComponent, RenderInfoComponent, CachedSpatialTransformTimestampComponent>()),
	  opaque(pool.get_component_group<RenderInfoComponent, RenderableComponent, CachedSpatialTransformTimestampComponent, OpaqueComponent>()),
	  transparent(pool.get_component_group<RenderInfoComponent, RenderableComponent, CachedSpatialTransformTimestampComponent, TransparentComponent>()),
	  positional_lights(pool.get_component_group<RenderInfoComponent, RenderableComponent, CachedSpatialTransformTimestampComponent, PositionalLightComponent>()),
	  static_shadowing(pool.get_component_group<RenderInfoComponent, RenderableComponent, CachedSpatialTransformTimestampComponent, CastsStaticShadowComponent>()),
	  dynamic_shadowing(pool.get_component_group<RenderInfoComponent, RenderableComponent, CachedSpatialTransformTimestampComponent, CastsDynamicShadowComponent>()),
	  render_pass_shadowing(pool.get_component_group<RenderPassComponent, RenderableComponent, CachedSpatialTransformTimestampComponent, CastsDynamicShadowComponent>()),
	  backgrounds(pool.get_component_group<UnboundedComponent, RenderableComponent>()),
	  cameras(pool.get_component_group<CameraComponent, CachedTransformComponent>()),
	  directional_lights(pool.get_component_group<DirectionalLightComponent, CachedTransformComponent>()),
	  ambient_lights(pool.get_component_group<AmbientLightComponent>()),
	  per_frame_updates(pool.get_component_group<PerFrameUpdateComponent>()),
	  per_frame_update_transforms(pool.get_component_group<PerFrameUpdateTransformComponent, RenderInfoComponent>()),
	  environments(pool.get_component_group<EnvironmentComponent>()),
	  render_pass_sinks(pool.get_component_group<RenderPassSinkComponent, RenderableComponent, CullPlaneComponent>()),
	  render_pass_creators(pool.get_component_group<RenderPassComponent>())
{

}

Scene::~Scene()
{
	// Makes shutdown way faster :)
	// We know ahead of time we're going to delete everything,
	// so reduce a lot of overhead by deleting right away.
	pool.reset_groups();

	destroy_entities(entities);
	destroy_entities(queued_entities);
}

template <typename T>
static void gather_visible_renderables(const Frustum &frustum, VisibilityList &list, const T &objects,
                                       size_t begin_index, size_t end_index)
{
	for (size_t i = begin_index; i < end_index; i++)
	{
		auto &o = objects[i];
		auto *transform = get_component<RenderInfoComponent>(o);
		auto *renderable = get_component<RenderableComponent>(o);
		auto *timestamp = get_component<CachedSpatialTransformTimestampComponent>(o);

		Util::Hasher h;
		h.u64(timestamp->cookie);
		h.u32(timestamp->last_timestamp);

		if (transform->transform)
		{
			if ((renderable->renderable->flags & RENDERABLE_FORCE_VISIBLE_BIT) != 0 ||
			    SIMD::frustum_cull(transform->world_aabb, frustum.get_planes()))
			{
				list.push_back({ renderable->renderable.get(), transform, h.get() });
			}
		}
		else
			list.push_back({ renderable->renderable.get(), nullptr, h.get() });
	}
}

void Scene::add_render_passes(RenderGraph &graph)
{
	for (auto &pass : render_pass_creators)
	{
		auto *rpass = get_component<RenderPassComponent>(pass)->creator;
		rpass->add_render_passes(graph);
	}
}

void Scene::add_render_pass_dependencies(RenderGraph &graph, RenderPass &main_pass)
{
	for (auto &pass : render_pass_creators)
	{
		auto *rpass = get_component<RenderPassComponent>(pass)->creator;
		rpass->setup_render_pass_dependencies(graph, main_pass);
	}
}

void Scene::set_render_pass_data(const RendererSuite *suite, const RenderContext *context)
{
	for (auto &pass : render_pass_creators)
	{
		auto *rpass = get_component<RenderPassComponent>(pass)->creator;
		rpass->set_base_renderer(suite);
		rpass->set_base_render_context(context);
		rpass->set_scene(this);
	}
}

void Scene::bind_render_graph_resources(RenderGraph &graph)
{
	for (auto &pass : render_pass_creators)
	{
		auto *rpass = get_component<RenderPassComponent>(pass)->creator;
		rpass->setup_render_pass_resources(graph);
	}
}

void Scene::refresh_per_frame(const RenderContext &context, TaskComposer &composer)
{
	composer.begin_pipeline_stage();

	for (auto &update : per_frame_update_transforms)
	{
		auto *refresh = get_component<PerFrameUpdateTransformComponent>(update)->refresh;
		auto *transform = get_component<RenderInfoComponent>(update);
		if (refresh)
			refresh->refresh(context, transform, composer);
	}

	composer.begin_pipeline_stage();

	for (auto &update : per_frame_updates)
	{
		auto *refresh = get_component<PerFrameUpdateComponent>(update)->refresh;
		if (refresh)
			refresh->refresh(context, composer);
	}

	composer.begin_pipeline_stage();
}

EnvironmentComponent *Scene::get_environment() const
{
	if (environments.empty())
		return nullptr;
	else
		return get_component<EnvironmentComponent>(environments.front());
}

EntityPool &Scene::get_entity_pool()
{
	return pool;
}

void Scene::gather_unbounded_renderables(VisibilityList &list) const
{
	for (auto &background : backgrounds)
		list.push_back({ get_component<RenderableComponent>(background)->renderable.get(), nullptr });
}

void Scene::gather_visible_render_pass_sinks(const vec3 &camera_pos, VisibilityList &list) const
{
	for (auto &sink : render_pass_sinks)
	{
		auto &plane = get_component<CullPlaneComponent>(sink)->plane;
		if (dot(vec4(camera_pos, 1.0f), plane) > 0.0f)
			list.push_back({get_component<RenderableComponent>(sink)->renderable.get(), nullptr});
	}
}

void Scene::gather_visible_opaque_renderables(const Frustum &frustum, VisibilityList &list) const
{
	gather_visible_renderables(frustum, list, opaque, 0, opaque.size());
}

void Scene::gather_visible_opaque_renderables_subset(const Frustum &frustum, VisibilityList &list,
                                                     unsigned index, unsigned num_indices) const
{
	size_t start_index = (index * opaque.size()) / num_indices;
	size_t end_index = ((index + 1) * opaque.size()) / num_indices;
	gather_visible_renderables(frustum, list, opaque, start_index, end_index);
}

void Scene::gather_visible_transparent_renderables(const Frustum &frustum, VisibilityList &list) const
{
	gather_visible_renderables(frustum, list, transparent, 0, transparent.size());
}

void Scene::gather_visible_static_shadow_renderables(const Frustum &frustum, VisibilityList &list) const
{
	gather_visible_renderables(frustum, list, static_shadowing, 0, static_shadowing.size());
}

void Scene::gather_visible_transparent_renderables_subset(const Frustum &frustum, VisibilityList &list,
                                                          unsigned index, unsigned num_indices) const
{
	size_t start_index = (index * transparent.size()) / num_indices;
	size_t end_index = ((index + 1) * transparent.size()) / num_indices;
	gather_visible_renderables(frustum, list, transparent, start_index, end_index);
}

void Scene::gather_visible_static_shadow_renderables_subset(const Frustum &frustum, VisibilityList &list,
                                                            unsigned index, unsigned num_indices) const
{
	size_t start_index = (index * static_shadowing.size()) / num_indices;
	size_t end_index = ((index + 1) * static_shadowing.size()) / num_indices;
	gather_visible_renderables(frustum, list, static_shadowing, start_index, end_index);
}

void Scene::gather_visible_dynamic_shadow_renderables(const Frustum &frustum, VisibilityList &list) const
{
	gather_visible_renderables(frustum, list, dynamic_shadowing, 0, dynamic_shadowing.size());
	for (auto &object : render_pass_shadowing)
		list.push_back({ get_component<RenderableComponent>(object)->renderable.get(), nullptr });
}

void Scene::gather_visible_dynamic_shadow_renderables_subset(const Frustum &frustum, VisibilityList &list,
                                                             unsigned index, unsigned num_indices) const
{
	size_t start_index = (index * dynamic_shadowing.size()) / num_indices;
	size_t end_index = ((index + 1) * dynamic_shadowing.size()) / num_indices;
	gather_visible_renderables(frustum, list, dynamic_shadowing, start_index, end_index);

	if (index == 0)
		for (auto &object : render_pass_shadowing)
			list.push_back({ get_component<RenderableComponent>(object)->renderable.get(), nullptr });
}

static void gather_positional_lights(const Frustum &frustum, VisibilityList &list,
                                     const std::vector<std::tuple<RenderInfoComponent *,
	                                     RenderableComponent *,
	                                     CachedSpatialTransformTimestampComponent *,
	                                     PositionalLightComponent *>> &positional,
                                     size_t start_index, size_t end_index)
{
	for (size_t i = start_index; i < end_index; i++)
	{
		auto &o = positional[i];
		auto *transform = get_component<RenderInfoComponent>(o);
		auto *renderable = get_component<RenderableComponent>(o);
		auto *timestamp = get_component<CachedSpatialTransformTimestampComponent>(o);

		Util::Hasher h;
		h.u64(timestamp->cookie);
		h.u32(timestamp->last_timestamp);

		if (transform->transform)
		{
			if (SIMD::frustum_cull(transform->world_aabb, frustum.get_planes()))
				list.push_back({ renderable->renderable.get(), transform, h.get() });
		}
		else
			list.push_back({ renderable->renderable.get(), nullptr, h.get() });
	}
}

static void gather_positional_lights(const Frustum &frustum, PositionalLightList &list,
                                     const std::vector<std::tuple<RenderInfoComponent *,
	                                     RenderableComponent *,
	                                     CachedSpatialTransformTimestampComponent *,
	                                     PositionalLightComponent *>> &positional,
                                     size_t start_index, size_t end_index)
{
	for (size_t i = start_index; i < end_index; i++)
	{
		auto &o = positional[i];
		auto *transform = get_component<RenderInfoComponent>(o);
		auto *light = get_component<PositionalLightComponent>(o)->light;
		auto *timestamp = get_component<CachedSpatialTransformTimestampComponent>(o);

		Util::Hasher h;
		h.u64(timestamp->cookie);
		h.u32(timestamp->last_timestamp);

		if (transform->transform)
		{
			if (SIMD::frustum_cull(transform->world_aabb, frustum.get_planes()))
				list.push_back({ light, transform, h.get() });
		}
		else
			list.push_back({ light, transform, h.get() });
	}
}

void Scene::gather_visible_positional_lights(const Frustum &frustum, VisibilityList &list) const
{
	gather_positional_lights(frustum, list, positional_lights, 0, positional_lights.size());
}

void Scene::gather_visible_positional_lights(const Frustum &frustum, PositionalLightList &list) const
{
	gather_positional_lights(frustum, list, positional_lights, 0, positional_lights.size());
}

void Scene::gather_visible_positional_lights_subset(const Frustum &frustum, VisibilityList &list,
                                                    unsigned index, unsigned num_indices) const
{
	size_t start_index = (index * positional_lights.size()) / num_indices;
	size_t end_index = ((index + 1) * positional_lights.size()) / num_indices;
	gather_positional_lights(frustum, list, positional_lights, start_index, end_index);
}

void Scene::gather_visible_positional_lights_subset(const Frustum &frustum, PositionalLightList &list,
                                                    unsigned index, unsigned num_indices) const
{
	size_t start_index = (index * positional_lights.size()) / num_indices;
	size_t end_index = ((index + 1) * positional_lights.size()) / num_indices;
	gather_positional_lights(frustum, list, positional_lights, start_index, end_index);
}

size_t Scene::get_opaque_renderables_count() const
{
	return opaque.size();
}

size_t Scene::get_transparent_renderables_count() const
{
	return transparent.size();
}

size_t Scene::get_static_shadow_renderables_count() const
{
	return static_shadowing.size();
}

size_t Scene::get_dynamic_shadow_renderables_count() const
{
	return dynamic_shadowing.size();
}

size_t Scene::get_positional_lights_count() const
{
	return positional_lights.size();
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
	if (node.get_skin() && !node.get_skin()->cached_skin_transform.bone_world_transforms.empty())
	{
		auto &skin = *node.get_skin();
		auto len = node.get_skin()->skin.size();
		assert(skin.skin.size() == skin.cached_skin_transform.bone_world_transforms.size());
		//assert(node.get_skin().cached_skin.size() == node.cached_skin_transform.bone_normal_transforms.size());
		for (size_t i = 0; i < len; i++)
		{
			SIMD::mul(skin.cached_skin_transform.bone_world_transforms[i],
			          skin.cached_skin[i]->world_transform,
			          skin.inverse_bind_poses[i]);
			//node.cached_skin_transform.bone_normal_transforms[i] = node.get_skin().cached_skin[i]->normal_transform;
		}
		//log_node_transforms(node);
	}
}

void Scene::dispatch_collect_children(TraversalState *state)
{
	size_t count = state->pending_count;

	// If we have a lot of child nodes, they will be farmed out to separate traversal states.
	// The spill-over will be collected, and sub-batches will be dispatched from that, etc.

	Node *children[TraversalState::BatchSize];
	size_t unbatched_child_count = 0;

	for (size_t i = 0; i < count; i++)
	{
		NodeUpdateState update_state;
		Node *pending;

		if (state->single_parent)
		{
			pending = state->pending_list[i].get();
			update_state = update_node_state(*pending, state->single_parent_is_dirty);
			if (update_state.self)
				update_transform_tree_node(*pending, *state->single_parent_transform);
		}
		else
		{
			pending = state->pending[i];
			update_state = update_node_state(*pending, state->parent_is_dirty[i]);
			if (update_state.self)
				update_transform_tree_node(*pending, *state->parent_transforms[i]);
		}

		if (!update_state.children)
			continue;

		bool parent_is_dirty = update_state.self;

		auto *input_childs = pending->get_children().data();
		size_t child_count = pending->get_children().size();
		const mat4 *transform = &pending->cached_transform.world_transform;

		size_t derived_batch_count = child_count / TraversalState::BatchSize;

		for (size_t batch = 0; batch < derived_batch_count; batch++)
		{
			auto *child_state = traversal_state_pool.allocate();
			child_state->traversal_done_dependency = state->traversal_done_dependency;
			child_state->traversal_done_dependency->add_flush_dependency();
			child_state->group = state->group;
			child_state->pending_count = TraversalState::BatchSize;

			child_state->pending_list = &input_childs[batch * TraversalState::BatchSize];
			child_state->single_parent = true;
			child_state->single_parent_transform = transform;
			child_state->single_parent_is_dirty = parent_is_dirty;

			dispatch_per_node_work(child_state);
		}

		for (size_t j = derived_batch_count * TraversalState::BatchSize; j < child_count; j++)
		{
			children[unbatched_child_count] = input_childs[j].get();
			state->parent_is_dirty[unbatched_child_count] = parent_is_dirty;
			state->parent_transforms[unbatched_child_count] = transform;
			unbatched_child_count++;

			if (unbatched_child_count == TraversalState::BatchSize)
			{
				auto *child_state = traversal_state_pool.allocate();
				child_state->traversal_done_dependency = state->traversal_done_dependency;
				child_state->traversal_done_dependency->add_flush_dependency();
				child_state->group = state->group;
				child_state->pending_count = TraversalState::BatchSize;

				memcpy(child_state->pending, children, sizeof(children));
				memcpy(child_state->parent_is_dirty, state->parent_is_dirty, sizeof(state->parent_is_dirty));
				memcpy(child_state->parent_transforms, state->parent_transforms, sizeof(state->parent_transforms));

				dispatch_per_node_work(child_state);
				unbatched_child_count = 0;
			}
		}
	}

	state->pending_count = unbatched_child_count;
	memcpy(state->pending, children, unbatched_child_count * sizeof(*children));
}

TaskGroupHandle Scene::dispatch_per_node_work(TraversalState *state)
{
	auto dispatcher_task = state->group->create_task([this, state]() {
		while (state->pending_count != 0)
			dispatch_collect_children(state);
		state->traversal_done_dependency->release_flush_dependency();
		traversal_state_pool.free(state);
	});
	dispatcher_task->set_desc("parallel-node-transform-update");
	return dispatcher_task;
}

static const mat4 identity_transform(1.0f);

void Scene::update_transform_tree(TaskComposer &composer)
{
	if (!root_node)
		return;

	auto &group = composer.begin_pipeline_stage();

	auto traversal = traversal_state_pool.allocate();
	traversal->traversal_done_dependency = composer.get_thread_group().create_task();
	traversal->pending_count = 1;
	traversal->pending[0] = root_node.get();
	traversal->parent_is_dirty[0] = false;
	traversal->parent_transforms[0] = &identity_transform;
	traversal->group = &composer.get_thread_group();
	traversal->traversal_done_dependency->add_flush_dependency();
	auto dispatch = dispatch_per_node_work(traversal);

	if (auto dep = composer.get_pipeline_stage_dependency())
		composer.get_thread_group().add_dependency(*dispatch, *dep);
	composer.get_thread_group().add_dependency(group, *traversal->traversal_done_dependency);
}

Scene::NodeUpdateState Scene::update_node_state(Node &node, bool parent_is_dirty)
{
	bool transform_dirty = node.get_and_clear_transform_dirty() || parent_is_dirty;
	bool child_transforms_dirty = node.get_and_clear_child_transform_dirty() || transform_dirty;
	return { transform_dirty, child_transforms_dirty };
}

void Scene::update_transform_tree_node(Node &node, const mat4 &transform)
{
	compute_model_transform(node.cached_transform.world_transform,
	                        node.transform.scale, node.transform.rotation, node.transform.translation,
	                        transform);

	if (auto *skinning = node.get_skin())
		for (auto &child : skinning->skeletons)
			update_transform_tree(*child, node.cached_transform.world_transform, true);

	//compute_normal_transform(node.cached_transform.normal_transform, node.cached_transform.world_transform);
	update_skinning(node);
	node.update_timestamp();
}

void Scene::update_transform_tree(Node &node, const mat4 &transform, bool parent_is_dirty)
{
	auto state = update_node_state(node, parent_is_dirty);

	if (state.self)
		update_transform_tree_node(node, transform);

	if (state.children)
		for (auto &child : node.get_children())
			update_transform_tree(*child, node.cached_transform.world_transform, state.self);
}

size_t Scene::get_cached_transforms_count() const
{
	return spatials.size();
}

void Scene::update_cached_transforms_subset(unsigned index, unsigned num_indices)
{
	size_t begin_index = (spatials.size() * index) / num_indices;
	size_t end_index = (spatials.size() * (index + 1)) / num_indices;
	update_cached_transforms_range(begin_index, end_index);
}

void Scene::update_all_transforms()
{
	update_transform_tree();
	update_transform_listener_components();
	update_cached_transforms_range(0, spatials.size());
}

void Scene::update_transform_tree()
{
	if (root_node)
		update_transform_tree(*root_node, mat4(1.0f), false);
}

void Scene::update_transform_listener_components()
{
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

void Scene::update_cached_transforms_range(size_t begin_range, size_t end_range)
{
	for (size_t i = begin_range; i < end_range; i++)
	{
		auto &s = spatials[i];

		BoundedComponent *aabb;
		RenderInfoComponent *cached_transform;
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
						SIMD::transform_and_expand_aabb(cached_transform->world_aabb, *aabb->aabb, m);
				}
				else
				{
					SIMD::transform_aabb(cached_transform->world_aabb,
					                     *aabb->aabb,
					                     cached_transform->transform->world_transform);
				}
			}
			timestamp->last_timestamp = *timestamp->current_timestamp;
		}
	}
}

Scene::NodeHandle Scene::create_node()
{
	return Scene::NodeHandle(node_pool.allocate(this));
}

void Scene::NodeDeleter::operator()(Node *node)
{
	node->parent_scene->get_node_pool().free(node);
}

static void add_bone(Scene::NodeHandle *bones, uint32_t parent, const SceneFormats::Skin::Bone &bone)
{
	bones[parent]->get_skin()->skeletons.push_back(bones[bone.index]);
	for (auto &child : bone.children)
		add_bone(bones, bone.index, child);
}

Scene::NodeHandle Scene::create_skinned_node(const SceneFormats::Skin &skin)
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
	}

	node->set_skin(skinning_pool.allocate());
	auto &node_skin = *node->get_skin();
	node_skin.cached_skin_transform.bone_world_transforms.resize(skin.joint_transforms.size());
	//node->cached_skin_transform.bone_normal_transforms.resize(skin.joint_transforms.size());

	node_skin.skin.reserve(skin.joint_transforms.size());
	node_skin.cached_skin.reserve(skin.joint_transforms.size());
	node_skin.inverse_bind_poses.reserve(skin.joint_transforms.size());
	for (size_t i = 0; i < skin.joint_transforms.size(); i++)
	{
		node_skin.cached_skin.push_back(&bones[i]->cached_transform);
		node_skin.skin.push_back(&bones[i]->transform);
		node_skin.inverse_bind_poses.push_back(skin.inverse_bind_pose[i]);
	}

	for (auto &skeleton : skin.skeletons)
	{
		node->get_skin()->skeletons.push_back(bones[skeleton.index]);
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

	// Force parents to be notified.
	node->cached_transform_dirty = false;
	node->invalidate_cached_transform();
	children.push_back(node);
}

Scene::NodeHandle Scene::Node::remove_child(Node *node)
{
	assert(node->parent == this);
	node->parent = nullptr;
	auto handle = node->reference_from_this();

	// Force parents to be notified.
	node->cached_transform_dirty = false;
	node->invalidate_cached_transform();

	auto itr = remove_if(begin(children), end(children), [&](const NodeHandle &h) {
		return node == h.get();
	});
	assert(itr != end(children));
	children.erase(itr, end(children));
	return handle;
}

Scene::NodeHandle Scene::Node::remove_node_from_hierarchy(Node *node)
{
	if (node->parent)
		return node->parent->remove_child(node);
	else
		return Scene::NodeHandle(nullptr);
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

Entity *Scene::create_entity()
{
	Entity *entity = pool.create_entity();
	entities.insert_front(entity);
	return entity;
}

static std::atomic<uint64_t> transform_cookies;

Entity *Scene::create_light(const SceneFormats::LightInfo &light, Node *node)
{
	Entity *entity = pool.create_entity();
	entities.insert_front(entity);

	switch (light.type)
	{
	case SceneFormats::LightInfo::Type::Directional:
	{
		auto *dir = entity->allocate_component<DirectionalLightComponent>();
		auto *transform = entity->allocate_component<CachedTransformComponent>();
		transform->transform = &node->cached_transform;
		dir->color = light.color;
		break;
	}

	case SceneFormats::LightInfo::Type::Ambient:
	{
		auto *ambient = entity->allocate_component<AmbientLightComponent>();
		ambient->color = light.color;
		break;
	}

	case SceneFormats::LightInfo::Type::Point:
	case SceneFormats::LightInfo::Type::Spot:
	{
		AbstractRenderableHandle renderable;
		if (light.type == SceneFormats::LightInfo::Type::Point)
			renderable = Util::make_handle<PointLight>();
		else
		{
			renderable = Util::make_handle<SpotLight>();
			auto &spot = static_cast<SpotLight &>(*renderable);
			spot.set_spot_parameters(light.inner_cone, light.outer_cone);
		}

		auto &positional = static_cast<PositionalLight &>(*renderable);
		positional.set_color(light.color);
		if (light.range > 0.0f)
			positional.set_maximum_range(light.range);

		entity->allocate_component<PositionalLightComponent>()->light = &positional;
		entity->allocate_component<RenderableComponent>()->renderable = renderable;

		auto *transform = entity->allocate_component<RenderInfoComponent>();
		auto *timestamp = entity->allocate_component<CachedSpatialTransformTimestampComponent>();
		timestamp->cookie = transform_cookies.fetch_add(1, std::memory_order_relaxed);

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

Entity *Scene::create_renderable(AbstractRenderableHandle renderable, Node *node)
{
	Entity *entity = pool.create_entity();
	entities.insert_front(entity);

	if (renderable->has_static_aabb())
	{
		auto *transform = entity->allocate_component<RenderInfoComponent>();
		auto *timestamp = entity->allocate_component<CachedSpatialTransformTimestampComponent>();
		timestamp->cookie = transform_cookies.fetch_add(1, std::memory_order_relaxed);

		if (node)
		{
			transform->transform = &node->cached_transform;
			timestamp->current_timestamp = node->get_timestamp_pointer();

			if (node->get_skin() && !node->get_skin()->cached_skin.empty())
				transform->skin_transform = &node->get_skin()->cached_skin_transform;
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

void Scene::destroy_entities(Util::IntrusiveList<Entity> &entity_list)
{
	auto itr = entity_list.begin();
	while (itr != entity_list.end())
	{
		auto *to_free = itr.get();
		itr = entity_list.erase(itr);
		to_free->get_pool()->delete_entity(to_free);
	}
}

void Scene::remove_entities_with_component(ComponentType id)
{
	// We know ahead of time we're going to delete everything,
	// so reduce a lot of overhead by deleting right away.
	pool.reset_groups_for_component_type(id);

	auto itr = entities.begin();
	while (itr != entities.end())
	{
		if (itr->has_component(id))
		{
			auto *to_free = itr.get();
			itr = entities.erase(itr);
			to_free->get_pool()->delete_entity(to_free);
		}
		else
			++itr;
	}
}

void Scene::destroy_queued_entities()
{
	destroy_entities(queued_entities);
}

void Scene::destroy_entity(Entity *entity)
{
	if (entity)
	{
		entities.erase(entity);
		entity->get_pool()->delete_entity(entity);
	}
}

void Scene::queue_destroy_entity(Entity *entity)
{
	if (entity->mark_for_destruction())
	{
		entities.erase(entity);
		queued_entities.insert_front(entity);
	}
}

}
