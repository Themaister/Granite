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

#include "physics_system.hpp"
#include <btBulletDynamicsCommon.h>
#include <btBulletCollisionCommon.h>

using namespace std;

namespace Granite
{
struct PhysicsHandle
{
	Scene::Node *node = nullptr;
	btCollisionObject *bt_object = nullptr;
	btCollisionShape *bt_shape = nullptr;
	Entity *entity = nullptr;

	~PhysicsHandle()
	{
		if (bt_object)
		{
			btRigidBody *body = btRigidBody::upcast(bt_object);
			if (body && body->getMotionState())
				delete body->getMotionState();
		}

		delete bt_object;
		delete bt_shape;
	}
};

static void tick_callback_wrapper(btDynamicsWorld *world, btScalar time_step)
{
	static_cast<PhysicsSystem *>(world->getWorldUserInfo())->tick_callback(time_step);
}

void PhysicsSystem::tick_callback(float)
{
	auto *dispatcher = world->getDispatcher();
	int num_manifolds = dispatcher->getNumManifolds();
	for (int i = 0; i < num_manifolds; i++)
	{
		btPersistentManifold *contact = dispatcher->getManifoldByIndexInternal(i);
		auto *handle0 = static_cast<PhysicsHandle *>(contact->getBody0()->getUserPointer());
		auto *handle1 = static_cast<PhysicsHandle *>(contact->getBody1()->getUserPointer());
		int num_contacts = contact->getNumContacts();
		for (int j = 0; j < num_contacts; j++)
		{
			auto &pt = contact->getContactPoint(j);
			if (pt.m_lifeTime == 1)
			{
				new_collision_buffer.emplace_back(handle0->entity, handle1->entity,
				                                  handle0, handle1,
				                                  vec3(pt.getPositionWorldOnB().x(),
				                                       pt.getPositionWorldOnB().y(),
				                                       pt.getPositionWorldOnB().z()),
				                                  vec3(pt.m_normalWorldOnB.x(),
				                                       pt.m_normalWorldOnB.y(),
				                                       pt.m_normalWorldOnB.z()));
			}
		}
	}

	auto *em = Global::event_manager();
	if (em)
		for (auto &collision : new_collision_buffer)
			em->dispatch_inline(collision);
	new_collision_buffer.clear();
}

RaycastResult PhysicsSystem::query_closest_hit_ray(const vec3 &from, const vec3 &dir, float t)
{
	vec3 to = from + dir * t;
	btVector3 ray_from_world(from.x, from.y, from.z);
	btVector3 ray_to_world(to.x, to.y, to.z);
	btCollisionWorld::ClosestRayResultCallback cb(ray_from_world, ray_to_world);
	world->rayTest(ray_from_world, ray_to_world, cb);

	RaycastResult result = {};
	if (cb.hasHit())
	{
		auto *object = cb.m_collisionObject;
		if (object)
		{
			result.handle = static_cast<PhysicsHandle *>(object->getUserPointer());
			result.entity = result.handle->entity;
		}
		result.world_pos.x = cb.m_hitPointWorld.x();
		result.world_pos.y = cb.m_hitPointWorld.y();
		result.world_pos.z = cb.m_hitPointWorld.z();
		result.world_normal.x = cb.m_hitNormalWorld.x();
		result.world_normal.y = cb.m_hitNormalWorld.y();
		result.world_normal.z = cb.m_hitNormalWorld.z();
		result.t = cb.m_closestHitFraction * t;
	}
	return result;
}

PhysicsSystem::PhysicsSystem()
{
	collision_config = make_unique<btDefaultCollisionConfiguration>();
	dispatcher = make_unique<btCollisionDispatcher>(collision_config.get());
	broadphase = make_unique<btDbvtBroadphase>();
	solver = make_unique<btSequentialImpulseConstraintSolver>();
	world = make_unique<btDiscreteDynamicsWorld>(dispatcher.get(), broadphase.get(),
	                                             solver.get(), collision_config.get());

	world->setGravity(btVector3(0.0f, -9.81f, 0.0f));
	world->setInternalTickCallback(tick_callback_wrapper, this);
}

PhysicsSystem::~PhysicsSystem()
{
	for (int i = world->getNumCollisionObjects() - 1; i >= 0; i--)
	{
		auto *obj = world->getCollisionObjectArray()[i];
		world->removeCollisionObject(obj);
	}

	for (auto *handle : handles)
		handle_pool.free(handle);
}

void PhysicsSystem::iterate(double frame_time)
{
	world->stepSimulation(btScalar(frame_time), 20, 1.0f / 300.0f);

	for (auto *handle : handles)
	{
		if (!handle->node)
			continue;

		auto *obj = handle->bt_object;
		auto *body = btRigidBody::upcast(obj);
		btTransform t;
		if (body && body->getMotionState())
			body->getMotionState()->getWorldTransform(t);
		else
			t = obj->getWorldTransform();

		auto rot = t.getRotation();
		auto &transform = handle->node->transform;
		transform.rotation.x = rot.x();
		transform.rotation.y = rot.y();
		transform.rotation.z = rot.z();
		transform.rotation.w = rot.w();

		auto orig = t.getOrigin();
		transform.translation.x = orig.x();
		transform.translation.y = orig.y();
		transform.translation.z = orig.z();

		handle->node->invalidate_cached_transform();
	}
}

Entity *PhysicsSystem::get_handle_parent(PhysicsHandle *handle)
{
	return handle->entity;
}

Scene::Node *PhysicsSystem::get_scene_node(PhysicsHandle *handle)
{
	return handle->node;
}

void PhysicsSystem::set_handle_parent(PhysicsHandle *handle, Entity *entity)
{
	handle->entity = entity;
}

void PhysicsSystem::remove_body(PhysicsHandle *handle)
{
	auto *obj = handle->bt_object;
	btRigidBody *body = btRigidBody::upcast(obj);

	if (body)
	{
		for (int i = body->getNumConstraintRefs() - 1; i >= 0; i--)
		{
			btTypedConstraint *constraint = body->getConstraintRef(i);
			world->removeConstraint(constraint);
		}
	}

	world->removeCollisionObject(obj);
	handle_pool.free(handle);

	// TODO: Avoid O(n).
	auto itr = find(begin(handles), end(handles), handle);
	if (itr != end(handles))
		handles.erase(itr);
}

unsigned PhysicsSystem::register_collision_mesh(const CollisionMesh &mesh)
{
	static_assert(sizeof(int) == sizeof(uint32_t), "You're on a really weird platform.");
	auto *index_vertex_array = new btTriangleIndexVertexArray(mesh.num_triangles,
	                                                          const_cast<int *>(reinterpret_cast<const int *>(mesh.indices)),
	                                                          mesh.index_stride_triangle,
	                                                          mesh.num_vertices,
	                                                          const_cast<btScalar *>(mesh.positions), mesh.position_stride);

	const vec3 &lo = mesh.aabb.get_minimum();
	const vec3 &hi = mesh.aabb.get_maximum();
	index_vertex_array->setPremadeAabb(btVector3(lo.x, lo.y, lo.z), btVector3(hi.x, hi.y, hi.z));
	const bool quantized_aabb_compression = false;
	auto *shape = new btBvhTriangleMeshShape(index_vertex_array, quantized_aabb_compression);
	shape->setMargin(mesh.margin);

	auto index = unsigned(mesh_collision_shapes.size());
	mesh_collision_shapes.emplace_back(shape);
	index_vertex_arrays.emplace_back(index_vertex_array);
	return index;
}

PhysicsHandle *PhysicsSystem::add_shape(Scene::Node *node, const MaterialInfo &info, btCollisionShape *shape)
{
	btTransform t;
	t.setIdentity();
	btVector3 local_inertia(0, 0, 0);
	if (info.mass != 0.0f)
		shape->calculateLocalInertia(info.mass, local_inertia);

	if (node)
	{
		t.setOrigin(btVector3(
				node->transform.translation.x,
				node->transform.translation.y,
				node->transform.translation.z));
		t.setRotation(btQuaternion(
				node->transform.rotation.x,
				node->transform.rotation.y,
				node->transform.rotation.z,
				node->transform.rotation.w));
	}

	auto *motion = new btDefaultMotionState(t);
	btRigidBody::btRigidBodyConstructionInfo rb_info(info.mass, motion, shape, local_inertia);
	if (info.mass != 0.0f)
	{
		rb_info.m_restitution = info.restitution;
		rb_info.m_linearDamping = info.linear_damping;
		rb_info.m_angularDamping = info.angular_damping;
	}
	else
		rb_info.m_restitution = 1.0f;

	rb_info.m_friction = info.friction;
	rb_info.m_rollingFriction = info.rolling_friction;

	auto *body = new btRigidBody(rb_info);
	world->addRigidBody(body);

	auto *handle = handle_pool.allocate();
	body->setUserPointer(handle);
	handle->node = node;
	handle->bt_object = body;
	handle->bt_shape = shape;
	handles.push_back(handle);
	return handle;
}

PhysicsHandle *PhysicsSystem::add_mesh(Scene::Node *node, unsigned index, const MaterialInfo &info)
{
	assert(index < mesh_collision_shapes.size());
	auto *shape = new btScaledBvhTriangleMeshShape(mesh_collision_shapes[index].get(),
	                                               btVector3(node->transform.scale.x,
	                                                         node->transform.scale.y,
	                                                         node->transform.scale.z));

	// Mesh objects cannot be dynamic.
	MaterialInfo tmp = info;
	tmp.mass = 0.0f;
	tmp.restitution = 1.0f;

	auto *handle = add_shape(node, tmp, shape);
	return handle;
}

PhysicsHandle *PhysicsSystem::add_cube(Scene::Node *node, const MaterialInfo &info)
{
	auto *shape = new btBoxShape(btVector3(node->transform.scale.x,
	                                       node->transform.scale.y,
	                                       node->transform.scale.z));
	auto *handle = add_shape(node, info, shape);
	return handle;
}

PhysicsHandle *PhysicsSystem::add_cone(Scene::Node *node, float height, float radius, const MaterialInfo &info)
{
	auto *shape = new btConeShape(radius * node->transform.scale.x, height * node->transform.scale.y);
	auto *handle = add_shape(node, info, shape);
	return handle;
}

PhysicsHandle *PhysicsSystem::add_cylinder(Scene::Node *node, float height, float radius, const MaterialInfo &info)
{
	auto *shape = new btCylinderShape(btVector3(radius * node->transform.scale.x, height * node->transform.scale.y, radius * node->transform.scale.z));
	auto *handle = add_shape(node, info, shape);
	return handle;
}

void PhysicsSystem::set_linear_velocity(PhysicsHandle *handle, const vec3 &v)
{
	auto *body = btRigidBody::upcast(handle->bt_object);
	body->setLinearVelocity(btVector3(v.x, v.y, v.z));
}

void PhysicsSystem::set_angular_velocity(PhysicsHandle *handle, const vec3 &v)
{
	auto *body = btRigidBody::upcast(handle->bt_object);
	body->setAngularVelocity(btVector3(v.x, v.y, v.z));
}

PhysicsHandle *PhysicsSystem::add_sphere(Scene::Node *node, const MaterialInfo &info)
{
	auto *shape = new btSphereShape(btScalar(node->transform.scale.x));
	auto *handle = add_shape(node, info, shape);
	return handle;
}

PhysicsHandle *PhysicsSystem::add_infinite_plane(const vec4 &plane, const MaterialInfo &info)
{
	auto *shape = new btStaticPlaneShape(btVector3(plane.x, plane.y, plane.z), plane.w);

	MaterialInfo tmp = info;
	tmp.mass = 0.0f;
	tmp.restitution = 1.0f;

	auto *handle = add_shape(nullptr, tmp, shape);
	return handle;
}

void PhysicsSystem::apply_impulse(PhysicsHandle *handle, const vec3 &impulse, const vec3 &relative)
{
	auto *body = btRigidBody::upcast(handle->bt_object);
	body->activate();
	body->applyImpulse(
			btVector3(impulse.x, impulse.y, impulse.z),
			btVector3(relative.x, relative.y, relative.z));
}

void PhysicsSystem::add_point_constraint(PhysicsHandle *handle, const vec3 &local_pivot)
{
	auto *body = btRigidBody::upcast(handle->bt_object);
	if (!body)
		return;

	auto *constraint = new btPoint2PointConstraint(
			*body,
			btVector3(local_pivot.x, local_pivot.y, local_pivot.z));

	world->addConstraint(constraint, false);
	body->addConstraintRef(constraint);
}

void PhysicsSystem::add_point_constraint(PhysicsHandle *handle0, PhysicsHandle *handle1,
                                         const vec3 &local_pivot0, const vec3 &local_pivot1,
                                         bool skip_collision)
{
	auto *body0 = btRigidBody::upcast(handle0->bt_object);
	auto *body1 = btRigidBody::upcast(handle1->bt_object);
	if (!body0 || !body1)
		return;

	auto *constraint = new btPoint2PointConstraint(
			*body0,
			*body1,
			btVector3(local_pivot0.x, local_pivot0.y, local_pivot0.z),
			btVector3(local_pivot1.x, local_pivot1.y, local_pivot1.z));

	world->addConstraint(constraint, skip_collision);
	body0->addConstraintRef(constraint);
	body1->addConstraintRef(constraint);
}

PhysicsComponent::~PhysicsComponent()
{
	if (handle)
		Global::physics()->remove_body(handle);
}
}
