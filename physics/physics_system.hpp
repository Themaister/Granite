/* Copyright (c) 2017-2019 Hans-Kristian Arntzen
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

	enum class ObjectType
	{
		Ghost,
		Static,
		Dynamic,
		Kinematic
	};

	struct MaterialInfo
	{
		ObjectType type = ObjectType::Dynamic;
		float mass = 1.0f;
		float restitution = 0.5f;
		float linear_damping = 0.1f;
		float angular_damping = 0.1f;
		float friction = 0.2f;
		float rolling_friction = 0.2f;
	};

	struct CollisionMesh
	{
		unsigned num_triangles = 0;
		unsigned num_vertices = 0;
		const uint32_t *indices = nullptr;
		size_t index_stride_triangle = 0;
		const float *positions = nullptr;
		size_t position_stride = 0;
		AABB aabb;

		float margin = 0.1f;
	};

	unsigned register_collision_mesh(const CollisionMesh &mesh);
	PhysicsHandle *add_mesh(Scene::Node *node, unsigned index, const MaterialInfo &info);
	PhysicsHandle *add_cube(Scene::Node *node, const MaterialInfo &info);
	PhysicsHandle *add_sphere(Scene::Node *node, const MaterialInfo &info);
	PhysicsHandle *add_cone(Scene::Node *node, float height, float radius, const MaterialInfo &info);
	PhysicsHandle *add_cylinder(Scene::Node *node, float height, float radius, const MaterialInfo &info);
	PhysicsHandle *add_infinite_plane(const vec4 &plane, const MaterialInfo &info);
	void set_linear_velocity(PhysicsHandle *handle, const vec3 &v);
	void set_angular_velocity(PhysicsHandle *handle, const vec3 &v);
	void remove_body(PhysicsHandle *handle);
	static void set_handle_parent(PhysicsHandle *handle, Entity *entity);
	static Entity *get_handle_parent(PhysicsHandle *handle);
	static Scene::Node *get_scene_node(PhysicsHandle *handle);

	void apply_impulse(PhysicsHandle *handle, const vec3 &impulse, const vec3 &relative);
	void iterate(double frame_time);
	void tick_callback(float tick_time);

	RaycastResult query_closest_hit_ray(const vec3 &from, const vec3 &dir, float length);

	void add_point_constraint(PhysicsHandle *handle, const vec3 &local_pivot);
	void add_point_constraint(PhysicsHandle *handle0, PhysicsHandle *handle1,
	                          const vec3 &local_pivot0, const vec3 &local_pivot1,
	                          bool skip_collision = false);

	bool get_overlapping_objects(PhysicsHandle *handle, std::vector<PhysicsHandle *> &other);

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
};
}
