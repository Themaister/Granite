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

#include <btBulletDynamicsCommon.h>
#include <btBulletCollisionCommon.h>
#include <memory>
#include <vector>
#include "util.hpp"
using namespace std;

bool callback(btManifoldPoint &cp,
              const btCollisionObjectWrapper *obj1, int id1, int index1,
              const btCollisionObjectWrapper *obj2, int id2, int index2)
{
	auto *body1 = btRigidBody::upcast(obj1->getCollisionObject());
	auto *body2 = btRigidBody::upcast(obj2->getCollisionObject());
	void *ptr1 = body1->getUserPointer();
	void *ptr2 = body2->getUserPointer();
	LOGI("Collision!\n");

	cp.m_appliedImpulse = 100.0f;
	return true;
}

int main()
{
	auto collision_config = make_unique<btDefaultCollisionConfiguration>();
	auto dispatcher = make_unique<btCollisionDispatcher>(collision_config.get());
	auto overlapping = make_unique<btDbvtBroadphase>();
	auto solver = make_unique<btSequentialImpulseConstraintSolver>();
	auto world = make_unique<btDiscreteDynamicsWorld>(dispatcher.get(), overlapping.get(), solver.get(), collision_config.get());

	world->setGravity(btVector3(0.0f, -9.81f, 0.0f));
	vector<unique_ptr<btCollisionShape>> shapes;

	gContactAddedCallback = callback;

	{
		auto ground_shape = make_unique<btBoxShape>(btVector3(10.0f, 10.0f, 10.0f));

		btTransform t;
		t.setIdentity();
		btScalar mass(0);
		btVector3 local_inertia(0, 0, 0);

		auto motion = make_unique<btDefaultMotionState>(t);
		btRigidBody::btRigidBodyConstructionInfo rb_info(mass, motion.release(), ground_shape.get(), local_inertia);

		auto body = make_unique<btRigidBody>(rb_info);
		world->addRigidBody(body.release());
		shapes.push_back(move(ground_shape));
	}

	{
		auto sphere_shape = make_unique<btSphereShape>(btScalar(1));

		btTransform t;
		t.setIdentity();
		btScalar mass(1);
		btVector3 local_inertia(0, 0, 0);
		sphere_shape->calculateLocalInertia(mass, local_inertia);
		t.setOrigin(btVector3(2, 20, 0));

		auto motion = make_unique<btDefaultMotionState>(t);
		btRigidBody::btRigidBodyConstructionInfo rb_info(mass, motion.release(), sphere_shape.get(), local_inertia);

		auto body = make_unique<btRigidBody>(rb_info);
		body->setCollisionFlags(body->getCollisionFlags() |
		                        btCollisionObject::CF_CUSTOM_MATERIAL_CALLBACK);
		body->setUserPointer(new int(42));
		world->addRigidBody(body.release());
		shapes.push_back(move(sphere_shape));
	}

	for (int i = 0; i < 150; i++)
	{
		world->stepSimulation(1.0f / 60.0f, 10);
		for (int j = 0; j < world->getNumCollisionObjects(); j++)
		{
			auto *obj = world->getCollisionObjectArray()[j];
			auto *body = btRigidBody::upcast(obj);
			btTransform t;
			if (body && body->getMotionState())
				body->getMotionState()->getWorldTransform(t);
			else
				t = obj->getWorldTransform();

			LOGI("World pos: %d = %f,%f,%f\n", j, t.getOrigin().x(), t.getOrigin().y(), t.getOrigin().z());
		}
	}

	for (int i = world->getNumCollisionObjects() - 1; i >= 0; i--)
	{
		auto *obj = world->getCollisionObjectArray()[i];
		btRigidBody *body = btRigidBody::upcast(obj);
		if (body && body->getMotionState())
			delete body->getMotionState();
		world->removeCollisionObject(obj);
	}
}