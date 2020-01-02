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

#pragma once

#include "scene.hpp"
#include "object_pool.hpp"
#include "math.hpp"
#include "ecs.hpp"
#include <memory>

class btDefaultCollisionConfiguration;
class btCollisionDispatcher;
struct btDbvtBroadphase;
class btSequentialImpulseConstraintSolver;
class btDiscreteDynamicsWorld;
class btCollisionShape;
class btBvhTriangleMeshShape;
class btTriangleIndexVertexArray;
class btGhostPairCallback;
class btDynamicsWorld;

namespace Granite
{
struct PhysicsHandle;

struct PhysicsComponent : ComponentBase
{
	GRANITE_COMPONENT_TYPE_DECL(PhysicsComponent)
	PhysicsHandle *handle = nullptr;
	~PhysicsComponent();
};

struct CollisionMeshComponent : ComponentBase
{
	GRANITE_COMPONENT_TYPE_DECL(CollisionMeshComponent)
	SceneFormats::CollisionMesh mesh;
};

struct ForceComponent : ComponentBase
{
	GRANITE_COMPONENT_TYPE_DECL(ForceComponent)
	vec3 linear_force = vec3(0.0f);
	vec3 torque = vec3(0.0f);
};

class KinematicCharacter
{
public:
	KinematicCharacter();
	KinematicCharacter(btDynamicsWorld *world, Scene::NodeHandle node);
	~KinematicCharacter();

	KinematicCharacter(KinematicCharacter &&other) noexcept;
	KinematicCharacter &operator=(KinematicCharacter &&other) noexcept;

	void set_move_velocity(const vec3 &v);
	bool is_grounded();
	void jump(const vec3 &v);

private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};

class CollisionEvent : public Event
{
public:
	GRANITE_EVENT_TYPE_DECL(CollisionEvent)
	CollisionEvent(Entity *entity0_, Entity *entity1_,
	               PhysicsHandle *object0_, PhysicsHandle *object1_,
	               const vec3 &world_point_, const vec3 &normal_)
		: entity0(entity0_), entity1(entity1_),
		  object0(object0_), object1(object1_),
		  world_point(world_point_), normal(normal_)
	{
	}

	Entity *get_first_entity() const
	{
		return entity0;
	}

	Entity *get_second_entity() const
	{
		return entity1;
	}

	PhysicsHandle *get_first_handle() const
	{
		return object0;
	}

	PhysicsHandle *get_second_handle() const
	{
		return object1;
	}

	vec3 get_world_contact() const
	{
		return world_point;
	}

	vec3 get_world_normal() const
	{
		return normal;
	}

private:
	Entity *entity0;
	Entity *entity1;
	PhysicsHandle *object0;
	PhysicsHandle *object1;
	vec3 world_point;
	vec3 normal;
};

struct RaycastResult
{
	Entity *entity;
	PhysicsHandle *handle;
	vec3 world_pos;
	vec3 world_normal;
	float t;
};

class PhysicsSystem
{
public:
	PhysicsSystem();
	~PhysicsSystem();
	void set_scene(Scene *scene);

	enum class InteractionType
	{
		Ghost,
		Area,
		Static,
		Dynamic,
		Kinematic
	};

	struct MaterialInfo
	{
		InteractionType type = InteractionType::Dynamic;
		float mass = 1.0f;
		float restitution = 0.5f;
		float linear_damping = 0.1f;
		float angular_damping = 0.1f;
		float friction = 0.2f;
		float rolling_friction = 0.2f;
		float margin = 0.01f;
	};

	struct CollisionMesh
	{
		unsigned num_triangles = 0;
		unsigned num_vertices = 0;
		const uint32_t *indices = nullptr;
		size_t index_stride_triangle = 0;
		const float *positions = nullptr;
		size_t position_stride = 0;
		AABB aabb = {};

		float margin = 0.1f;
	};

	unsigned register_collision_mesh(const CollisionMesh &mesh);

	enum class MeshType
	{
		None,
		ConvexHull,
		Cube,
		Sphere,
		Cone,
		Capsule,
		Cylinder
	};

	struct ConvexMeshPart
	{
		MeshType type = MeshType::None;
		const Scene::Node *child_node = nullptr;
		unsigned index = 0;
		float height = 1.0f;
		float radius = 1.0f;
	};

	PhysicsHandle *add_compound_object(Scene::Node *node,
	                                   const ConvexMeshPart *parts, unsigned num_parts,
	                                   const MaterialInfo &info);
	PhysicsHandle *add_object(Scene::Node *node,
	                          const ConvexMeshPart &part,
	                          const MaterialInfo &info);

	PhysicsHandle *add_mesh(Scene::Node *node, unsigned index, const MaterialInfo &info);
	PhysicsHandle *add_convex_hull(Scene::Node *node, unsigned index, const MaterialInfo &info);
	PhysicsHandle *add_cube(Scene::Node *node, const MaterialInfo &info);
	PhysicsHandle *add_sphere(Scene::Node *node, const MaterialInfo &info);
	PhysicsHandle *add_cone(Scene::Node *node, float height, float radius, const MaterialInfo &info);
	PhysicsHandle *add_capsule(Scene::Node *node, float height, float radius, const MaterialInfo &info);
	PhysicsHandle *add_cylinder(Scene::Node *node, float height, float radius, const MaterialInfo &info);
	PhysicsHandle *add_infinite_plane(const vec4 &plane, const MaterialInfo &info);

	KinematicCharacter add_kinematic_character(Scene::NodeHandle node);

	void set_linear_velocity(PhysicsHandle *handle, const vec3 &v);
	void set_angular_velocity(PhysicsHandle *handle, const vec3 &v);

	// Prefer ForceComponent.
	void apply_force(PhysicsHandle *handle, const vec3 &v);
	void apply_force(PhysicsHandle *handle, const vec3 &v, const vec3 &world_pos);

	void remove_body(PhysicsHandle *handle);
	static void set_handle_parent(PhysicsHandle *handle, Entity *entity);
	static Entity *get_handle_parent(PhysicsHandle *handle);
	static Scene::Node *get_scene_node(PhysicsHandle *handle);
	static InteractionType get_interaction_type(PhysicsHandle *handle);

	void apply_impulse(PhysicsHandle *handle, const vec3 &impulse, const vec3 &world_position);
	void iterate(double frame_time);
	void tick_callback(float tick_time);

	enum InteractionTypeFlagBits
	{
		INTERACTION_TYPE_STATIC_BIT = 1 << 0,
		INTERACTION_TYPE_DYNAMIC_BIT = 1 << 1,
		INTERACTION_TYPE_INVISIBLE_BIT = 1 << 2,
		INTERACTION_TYPE_KINEMATIC_BIT = 1 << 3,
		INTERACTION_TYPE_ALL_BITS = 0x7fffffff
	};
	using InteractionTypeFlags = uint32_t;

	RaycastResult query_closest_hit_ray(const vec3 &from, const vec3 &dir, float length,
	                                    InteractionTypeFlags mask = INTERACTION_TYPE_ALL_BITS);

	void add_point_constraint(PhysicsHandle *handle, const vec3 &local_pivot);
	void add_point_constraint(PhysicsHandle *handle0, PhysicsHandle *handle1,
	                          const vec3 &local_pivot0, const vec3 &local_pivot1,
	                          bool skip_collision = false);

	enum class OverlapMethod
	{
		Broadphase,
		Nearphase
	};

	bool get_overlapping_objects(PhysicsHandle *handle, std::vector<PhysicsHandle *> &other,
	                             OverlapMethod method = OverlapMethod::Nearphase);

private:
	std::unique_ptr<btDefaultCollisionConfiguration> collision_config;
	std::unique_ptr<btCollisionDispatcher> dispatcher;
	std::unique_ptr<btDbvtBroadphase> broadphase;
	std::unique_ptr<btSequentialImpulseConstraintSolver> solver;
	std::unique_ptr<btDiscreteDynamicsWorld> world;

	Util::ObjectPool<PhysicsHandle> handle_pool;
	std::vector<PhysicsHandle *> handles;

	PhysicsHandle *add_shape(Scene::Node *node, const MaterialInfo &info, btCollisionShape *shape);
	std::vector<CollisionEvent> new_collision_buffer;
	std::vector<std::unique_ptr<btBvhTriangleMeshShape>> mesh_collision_shapes;
	std::vector<std::unique_ptr<btTriangleIndexVertexArray>> index_vertex_arrays;
	std::unique_ptr<btGhostPairCallback> ghost_callback;

	btCollisionShape *create_shape(const ConvexMeshPart &part);
	Scene *scene = nullptr;
	ComponentGroupVector<PhysicsComponent, ForceComponent> *forces = nullptr;
};
}
