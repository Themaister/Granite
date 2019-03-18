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
		btRigidBody *body = btRigidBody::upcast(obj);
		if (body && body->getMotionState())
			delete body->getMotionState();
		world->removeCollisionObject(obj);
	}

	for (auto *handle : handles)
		delete handle->bt_shape;
}

void PhysicsSystem::iterate(double frame_time)
{
	world->stepSimulation(btScalar(frame_time), 50, 1.0f / 300.0f);

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

void PhysicsSystem::set_handle_parent(PhysicsHandle *handle, Entity *entity)
{
	handle->entity = entity;
}

void PhysicsSystem::remove_body(PhysicsHandle *handle)
{
	auto *obj = handle->bt_object;
	btRigidBody *body = btRigidBody::upcast(obj);
	if (body && body->getMotionState())
		delete body->getMotionState();
	world->removeCollisionObject(obj);
	delete handle->bt_shape;
	handle_pool.free(handle);

	// TODO: Avoid O(n).
	auto itr = find(begin(handles), end(handles), handle);
	if (itr != end(handles))
		handles.erase(itr);
}

PhysicsHandle *PhysicsSystem::add_shape(Scene::Node *node, float mass, btCollisionShape *shape)
{
	btTransform t;
	t.setIdentity();
	btVector3 local_inertia(0, 0, 0);
	if (mass != 0.0f)
		shape->calculateLocalInertia(mass, local_inertia);

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
	btRigidBody::btRigidBodyConstructionInfo rb_info(mass, motion, shape, local_inertia);

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

PhysicsHandle *PhysicsSystem::add_cube(Scene::Node *node, float mass)
{
	auto *shape = new btBoxShape(btVector3(node->transform.scale.x,
	                                       node->transform.scale.y,
	                                       node->transform.scale.z));
	return add_shape(node, mass, shape);
}

PhysicsHandle *PhysicsSystem::add_sphere(Scene::Node *node, float mass)
{
	auto *shape = new btSphereShape(btScalar(node->transform.scale.x));
	return add_shape(node, mass, shape);
}

PhysicsHandle *PhysicsSystem::add_infinite_plane(const vec4 &plane)
{
	auto *shape = new btStaticPlaneShape(btVector3(plane.x, plane.y, plane.z), plane.w);
	return add_shape(nullptr, 0.0f, shape);
}

void PhysicsSystem::apply_impulse(PhysicsHandle *handle, const vec3 &impulse, const vec3 &relative)
{
	auto *body = btRigidBody::upcast(handle->bt_object);
	body->activate();
	body->applyImpulse(
			btVector3(impulse.x, impulse.y, impulse.z),
			btVector3(relative.x, relative.y, relative.z));
}

PhysicsComponent::~PhysicsComponent()
{
	if (handle)
		Global::physics()->remove_body(handle);
}

}