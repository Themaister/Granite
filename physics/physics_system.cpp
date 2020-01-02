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

#include "physics_system.hpp"
#include <btBulletDynamicsCommon.h>
#include <btBulletCollisionCommon.h>
#include <BulletCollision/CollisionDispatch/btGhostObject.h>
#include <BulletDynamics/Character/btKinematicCharacterController.h>

using namespace std;

namespace Granite
{
static const float PHYSICS_TICK = 1.0f / 300.0f;

static btVector3 convert(const vec3 &v)
{
	return { v.x, v.y, v.z };
}

static btQuaternion convert(const quat &q)
{
	return { q.x, q.y, q.z, q.w };
}

static vec3 convert(const btVector3 &v)
{
	return { v.x(), v.y(), v.z() };
}

static quat convert(const btQuaternion &q)
{
	return { q.w(), q.x(), q.y(), q.z() };
}

struct PhysicsHandle
{
	Scene::Node *node = nullptr;
	btCollisionObject *bt_object = nullptr;
	btCollisionShape *bt_shape = nullptr;
	Entity *entity = nullptr;
	PhysicsSystem::InteractionType type = PhysicsSystem::InteractionType::Ghost;
	bool copy_transform_from_node = false;

	~PhysicsHandle()
	{
		if (bt_object)
		{
			btRigidBody *body = btRigidBody::upcast(bt_object);
			if (body && body->getMotionState())
				delete body->getMotionState();


		}

		if (bt_shape && bt_shape->getShapeType() == COMPOUND_SHAPE_PROXYTYPE)
		{
			auto *shape = static_cast<btCompoundShape *>(bt_shape);
			for (int i = 0; i < shape->getNumChildShapes(); i++)
				delete shape->getChildShape(i);
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
	auto *collision_dispatcher = world->getDispatcher();
	int num_manifolds = collision_dispatcher->getNumManifolds();
	for (int i = 0; i < num_manifolds; i++)
	{
		btPersistentManifold *contact = collision_dispatcher->getManifoldByIndexInternal(i);
		auto *handle0 = static_cast<PhysicsHandle *>(contact->getBody0()->getUserPointer());
		auto *handle1 = static_cast<PhysicsHandle *>(contact->getBody1()->getUserPointer());
		int num_contacts = contact->getNumContacts();
		for (int j = 0; j < num_contacts; j++)
		{
			auto &pt = contact->getContactPoint(j);
			if (pt.m_lifeTime == 1)
			{
				new_collision_buffer.emplace_back(handle0 ? handle0->entity : nullptr,
				                                  handle1 ? handle1->entity : nullptr,
				                                  handle0, handle1,
				                                  convert(pt.getPositionWorldOnB()),
				                                  convert(pt.m_normalWorldOnB));
			}
		}
	}

	auto *em = Global::event_manager();
	if (em)
		for (auto &collision : new_collision_buffer)
			em->dispatch_inline(collision);
	new_collision_buffer.clear();
}

RaycastResult PhysicsSystem::query_closest_hit_ray(const vec3 &from, const vec3 &dir, float t,
                                                   InteractionTypeFlags flags)
{
	vec3 to = from + dir * t;
	btVector3 ray_from_world = convert(from);
	btVector3 ray_to_world = convert(to);
	btCollisionWorld::ClosestRayResultCallback cb(ray_from_world, ray_to_world);

	cb.m_collisionFilterMask = 0;
	if (flags == INTERACTION_TYPE_ALL_BITS)
		cb.m_collisionFilterMask = btBroadphaseProxy::AllFilter;
	else
	{
		if (flags & INTERACTION_TYPE_STATIC_BIT)
			cb.m_collisionFilterMask |= btBroadphaseProxy::StaticFilter;
		if (flags & INTERACTION_TYPE_DYNAMIC_BIT)
			cb.m_collisionFilterMask |= btBroadphaseProxy::DefaultFilter;
		if (flags & INTERACTION_TYPE_INVISIBLE_BIT)
			cb.m_collisionFilterMask |= btBroadphaseProxy::SensorTrigger;
		if (flags & INTERACTION_TYPE_KINEMATIC_BIT)
			cb.m_collisionFilterMask |= btBroadphaseProxy::CharacterFilter;
	}

	world->rayTest(ray_from_world, ray_to_world, cb);

	RaycastResult result = {};
	if (cb.hasHit())
	{
		auto *object = cb.m_collisionObject;
		if (object)
		{
			result.handle = static_cast<PhysicsHandle *>(object->getUserPointer());
			result.entity = result.handle ? result.handle->entity : nullptr;
		}
		result.world_pos.x = cb.m_hitPointWorld.x();
		result.world_pos.y = cb.m_hitPointWorld.y();
		result.world_pos.z = cb.m_hitPointWorld.z();
		result.world_normal.x = cb.m_hitNormalWorld.x();
		result.world_normal.y = cb.m_hitNormalWorld.y();
		result.world_normal.z = cb.m_hitNormalWorld.z();
		result.t = cb.m_closestHitFraction * t;

		//LOGI("Ray hit: %f, %f, %f\n", result.world_pos.x, result.world_pos.y, result.world_pos.z);
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

	ghost_callback.reset(new btGhostPairCallback);
	world->getPairCache()->setInternalGhostPairCallback(ghost_callback.get());
}

void PhysicsSystem::set_scene(Scene *scene_)
{
	scene = scene_;
	forces = &scene->get_entity_pool().get_component_group<PhysicsComponent, ForceComponent>();
}

struct KinematicCharacter::Impl : btKinematicCharacterController
{
	Impl(btPairCachingGhostObject *ghost_, btConvexShape *shape_, float step_height, const btVector3 &up)
		: btKinematicCharacterController(ghost_, shape_, step_height, up),
		  ghost(ghost_), shape(shape_)
	{
		setMaxSlope(0.4f);
		setMaxJumpHeight(3.0f);
	}

	void updateAction(btCollisionWorld *collision_world, btScalar delta_time) override
	{
		btKinematicCharacterController::updateAction(collision_world, delta_time);
		if (node)
		{
			node->transform.translation = convert(m_currentPosition);
			node->transform.rotation = convert(m_currentOrientation);
			node->invalidate_cached_transform();
		}
	}

	~Impl() override
	{
		if (world && ghost)
			world->removeCollisionObject(ghost);
		if (world)
			world->removeAction(this);
		delete shape;
		delete ghost;
	}

	btPairCachingGhostObject *ghost;
	btConvexShape *shape;
	btDynamicsWorld *world = nullptr;
	Scene::NodeHandle node;
	float tick = 0.0f;
};

KinematicCharacter::KinematicCharacter(btDynamicsWorld *world, Scene::NodeHandle node)
{
	auto *ghost = new btPairCachingGhostObject();
	auto *shape = new btSphereShape(btScalar(1.0f));
	shape->setLocalScaling(convert(node->transform.scale));
	ghost->setCollisionShape(shape);
	ghost->setCollisionFlags(btCollisionObject::CF_CHARACTER_OBJECT);
	ghost->setActivationState(DISABLE_DEACTIVATION);
	world->addCollisionObject(ghost, btBroadphaseProxy::CharacterFilter);

	btTransform t;
	t.setIdentity();
	t.setOrigin(convert(node->transform.translation));
	t.setRotation(convert(node->transform.rotation));
	ghost->setWorldTransform(t);
	impl.reset(new Impl(ghost, shape, 0.01f, btVector3(0.0f, 1.0f, 0.0f)));
	impl->world = world;
	impl->node = node;
	impl->tick = PHYSICS_TICK;
	world->addAction(impl.get());
}

KinematicCharacter &KinematicCharacter::operator=(KinematicCharacter &&other) noexcept
{
	impl = move(other.impl);
	return *this;
}

void KinematicCharacter::set_move_velocity(const vec3 &v)
{
	impl->setWalkDirection(convert(v * impl->tick));
}

bool KinematicCharacter::is_grounded()
{
	return impl->onGround();
}

void KinematicCharacter::jump(const vec3 &v)
{
	impl->jump(convert(v));
}

KinematicCharacter::KinematicCharacter(KinematicCharacter &&other) noexcept
{
	*this = move(other);
}

KinematicCharacter::KinematicCharacter()
{
}

KinematicCharacter::~KinematicCharacter()
{
}

KinematicCharacter PhysicsSystem::add_kinematic_character(Scene::NodeHandle node)
{
	KinematicCharacter character(world.get(), node);
	return character;
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
	// System which applies forces to objects every iteration.
	if (forces)
	{
		for (auto &force : *forces)
		{
			auto *handle = get_component<PhysicsComponent>(force)->handle;
			auto *body = btRigidBody::upcast(handle->bt_object);
			if (body)
			{
				auto *f = get_component<ForceComponent>(force);
				if (any(notEqual(f->linear_force, vec3(0.0f))) || any(notEqual(f->torque, vec3(0.0f))))
					body->activate();
				body->applyCentralForce(convert(f->linear_force));
				body->applyTorque(convert(f->torque));
			}
		}
	}

	// Update ghost object locations.
	for (auto *handle : handles)
	{
		if (!handle->node || !handle->copy_transform_from_node)
			continue;

		auto *obj = handle->bt_object;
		auto *ghost = btPairCachingGhostObject::upcast(obj);
		auto *body = btRigidBody::upcast(obj);

		if (ghost)
		{
			btTransform t;
			t.setIdentity();
			auto &rot = handle->node->transform.rotation;
			auto &pos = handle->node->transform.translation;
			t.setOrigin(convert(pos));
			t.setRotation(convert(rot));
			ghost->setWorldTransform(t);
			if (ghost->getBroadphaseHandle())
				world->updateSingleAabb(ghost);
		}
		else if (body)
		{
			btTransform t;
			t.setIdentity();
			auto &rot = handle->node->transform.rotation;
			auto &pos = handle->node->transform.translation;
			t.setOrigin(convert(pos));
			t.setRotation(convert(rot));
			if (body->getMotionState())
				body->getMotionState()->setWorldTransform(t);
			else
				body->setWorldTransform(t);
			body->setCenterOfMassTransform(t);
			if (body->getBroadphaseHandle())
				world->updateSingleAabb(body);
		}
	}

	world->stepSimulation(btScalar(frame_time), 20, PHYSICS_TICK);

	// Update node transforms from physics engine.
	for (auto *handle : handles)
	{
		if (!handle->node || handle->copy_transform_from_node)
			continue;

		auto *obj = handle->bt_object;
		auto *ghost = btPairCachingGhostObject::upcast(obj);
		if (ghost)
			continue;

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
	index_vertex_array->setPremadeAabb(convert(lo), convert(hi));
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

	if (node)
	{
		t.setOrigin(convert(node->transform.translation));
		t.setRotation(convert(node->transform.rotation));
		shape->setLocalScaling(convert(node->transform.scale));
	}

	shape->setMargin(info.margin);
	btVector3 local_inertia(0, 0, 0);
	if (info.mass != 0.0f && info.type != InteractionType::Static)
		shape->calculateLocalInertia(info.mass, local_inertia);

	PhysicsHandle *handle = nullptr;

	if (info.type == InteractionType::Ghost || info.type == InteractionType::Area)
	{
		auto *body = new btPairCachingGhostObject();
		body->setCollisionShape(shape);
		body->setCollisionFlags(body->getCollisionFlags() | btCollisionObject::CF_NO_CONTACT_RESPONSE);
		body->setWorldTransform(t);

		// Don't collide against other static objects and sensors.
		world->addCollisionObject(body,
		                          btBroadphaseProxy::SensorTrigger,
		                          btBroadphaseProxy::AllFilter &
		                          ~(btBroadphaseProxy::SensorTrigger | btBroadphaseProxy::StaticFilter));

		handle = handle_pool.allocate();
		body->setUserPointer(handle);
		handle->node = node;
		handle->bt_object = body;
		handle->bt_shape = shape;
		handle->copy_transform_from_node = info.type == InteractionType::Ghost;
		handles.push_back(handle);
	}
	else if (info.type == InteractionType::Kinematic)
	{
		auto *motion = new btDefaultMotionState(t);
		btRigidBody::btRigidBodyConstructionInfo rb_info(info.type == InteractionType::Static ? 0.0f : info.mass,
		                                                 motion, shape, local_inertia);

		auto *body = new btRigidBody(rb_info);
		body->setCollisionShape(shape);
		body->setCollisionFlags(body->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
		body->setActivationState(DISABLE_DEACTIVATION);

		world->addRigidBody(body, btBroadphaseProxy::CharacterFilter, btBroadphaseProxy::AllFilter);
		handle = handle_pool.allocate();
		body->setUserPointer(handle);
		handle->node = node;
		handle->bt_object = body;
		handle->bt_shape = shape;
		handle->copy_transform_from_node = true;
		handles.push_back(handle);
	}
	else
	{
		auto *motion = new btDefaultMotionState(t);
		btRigidBody::btRigidBodyConstructionInfo rb_info(info.type != InteractionType::Dynamic ? 0.0f : info.mass,
		                                                 motion, shape, local_inertia);
		if (info.mass != 0.0f && info.type == InteractionType::Dynamic)
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
		if (info.type != InteractionType::Dynamic)
			body->setCollisionFlags(body->getCollisionFlags() | btCollisionObject::CF_STATIC_OBJECT);

		world->addRigidBody(body, info.type == InteractionType::Dynamic ?
		                          btBroadphaseProxy::DefaultFilter : btBroadphaseProxy::StaticFilter,
		                          btBroadphaseProxy::AllFilter);

		handle = handle_pool.allocate();
		body->setUserPointer(handle);
		handle->node = node;
		handle->bt_object = body;
		handle->bt_shape = shape;
		handles.push_back(handle);
	}

	handle->type = info.type;
	return handle;
}

PhysicsSystem::InteractionType PhysicsSystem::get_interaction_type(PhysicsHandle *handle)
{
	return handle->type;
}

btCollisionShape *PhysicsSystem::create_shape(const ConvexMeshPart &part)
{
	btCollisionShape *shape = nullptr;
	switch (part.type)
	{
	case MeshType::Cylinder:
		shape = new btCylinderShape(btVector3(
				part.radius,
				0.5f * part.height,
				part.radius));
		break;

	case MeshType::Cone:
		shape = new btConeShape(part.radius, 0.5f * part.height);
		break;

	case MeshType::Capsule:
		shape = new btCapsuleShape(part.radius, 0.5f * part.height);
		break;

	case MeshType::Cube:
		shape = new btBoxShape(btVector3(1.0f, 1.0f, 1.0f));
		break;

	case MeshType::Sphere:
		shape = new btSphereShape(1.0f);
		break;

	case MeshType::ConvexHull:
	{
		assert(part.index < mesh_collision_shapes.size());

		auto *mesh = mesh_collision_shapes[part.index]->getMeshInterface();
		const unsigned char *vertex_base = nullptr;
		int num_verts = 0;
		PHY_ScalarType type;
		int stride = 0;
		const unsigned char *index_base = nullptr;
		int index_stride;
		int num_faces;
		PHY_ScalarType indices_type;

		mesh->getLockedReadOnlyVertexIndexBase(&vertex_base, num_verts, type, stride,
		                                       &index_base, index_stride, num_faces, indices_type);

		if (type != PHY_FLOAT)
			break;

		shape = new btConvexHullShape(reinterpret_cast<const btScalar *>(vertex_base), num_verts, stride);
		break;
	}

	case MeshType::None:
		break;
	}

	return shape;
}

PhysicsHandle *PhysicsSystem::add_object(Scene::Node *node,
                                         const ConvexMeshPart &part,
                                         const MaterialInfo &info)
{
	auto *shape = create_shape(part);
	if (shape)
	{
		auto *handle = add_shape(node, info, shape);
		return handle;
	}
	else
		return nullptr;
}

PhysicsHandle *PhysicsSystem::add_compound_object(Scene::Node *node,
                                                  const ConvexMeshPart *parts, unsigned num_parts,
                                                  const MaterialInfo &info)
{
	auto *compound_shape = new btCompoundShape();

	for (unsigned i = 0; i < num_parts; i++)
	{
		auto &part = parts[i];
		auto *shape = create_shape(part);

		if (shape)
		{
			btTransform t;
			t.setIdentity();
			if (part.child_node)
			{
				auto &child_t = part.child_node->transform;
				shape->setLocalScaling(convert(child_t.scale));
				t.setRotation(convert(child_t.rotation));
				t.setOrigin(convert(child_t.translation));
			}
			compound_shape->addChildShape(t, shape);
		}
	}

	auto *handle = add_shape(node, info, compound_shape);
	return handle;
}

PhysicsHandle *PhysicsSystem::add_mesh(Scene::Node *node, unsigned index, const MaterialInfo &info)
{
	assert(index < mesh_collision_shapes.size());
	auto *shape = new btScaledBvhTriangleMeshShape(mesh_collision_shapes[index].get(),
	                                               btVector3(1.0f, 1.0f, 1.0f));

	// Mesh objects cannot be dynamic.
	MaterialInfo tmp = info;
	tmp.type = InteractionType::Static;
	tmp.mass = 0.0f;
	tmp.restitution = 1.0f;

	auto *handle = add_shape(node, tmp, shape);
	return handle;
}

PhysicsHandle *PhysicsSystem::add_convex_hull(Scene::Node *node, unsigned index, const MaterialInfo &info)
{
	assert(index < mesh_collision_shapes.size());

	auto *mesh = mesh_collision_shapes[index]->getMeshInterface();
	const unsigned char *vertex_base = nullptr;
	int num_verts = 0;
	PHY_ScalarType type;
	int stride = 0;
	const unsigned char *index_base = nullptr;
	int index_stride;
	int num_faces;
	PHY_ScalarType indices_type;

	mesh->getLockedReadOnlyVertexIndexBase(&vertex_base, num_verts, type, stride,
	                                       &index_base, index_stride, num_faces, indices_type);

	if (type != PHY_FLOAT)
		return nullptr;

	auto *shape = new btConvexHullShape(reinterpret_cast<const btScalar *>(vertex_base), num_verts, stride);
	auto *handle = add_shape(node, info, shape);
	return handle;
}

PhysicsHandle *PhysicsSystem::add_cube(Scene::Node *node, const MaterialInfo &info)
{
	auto *shape = new btBoxShape(btVector3(1.0f, 1.0f, 1.0f));
	auto *handle = add_shape(node, info, shape);
	return handle;
}

PhysicsHandle *PhysicsSystem::add_cone(Scene::Node *node, float height, float radius, const MaterialInfo &info)
{
	auto *shape = new btConeShape(radius, height);
	auto *handle = add_shape(node, info, shape);
	return handle;
}

PhysicsHandle *PhysicsSystem::add_cylinder(Scene::Node *node, float height, float radius, const MaterialInfo &info)
{
	auto *shape = new btCylinderShape(btVector3(radius, 0.5f * height, radius));
	auto *handle = add_shape(node, info, shape);
	return handle;
}

PhysicsHandle *PhysicsSystem::add_capsule(Scene::Node *node, float height, float radius, const MaterialInfo &info)
{
	auto *shape = new btCapsuleShape(radius, 0.5f * height);
	auto *handle = add_shape(node, info, shape);
	return handle;
}

void PhysicsSystem::set_linear_velocity(PhysicsHandle *handle, const vec3 &v)
{
	auto *body = btRigidBody::upcast(handle->bt_object);
	if (body)
		body->setLinearVelocity(convert(v));
}

void PhysicsSystem::set_angular_velocity(PhysicsHandle *handle, const vec3 &v)
{
	auto *body = btRigidBody::upcast(handle->bt_object);
	if (body)
		body->setAngularVelocity(convert(v));
}

void PhysicsSystem::apply_force(PhysicsHandle *handle, const vec3 &v)
{
	auto *body = btRigidBody::upcast(handle->bt_object);
	if (body)
	{
		body->activate();
		body->applyCentralForce(convert(v));
	}
}

void PhysicsSystem::apply_force(PhysicsHandle *handle, const vec3 &v, const vec3 &world_pos)
{
	auto *body = btRigidBody::upcast(handle->bt_object);
	if (body)
	{
		body->activate();
		body->applyForce(convert(v), convert(world_pos) -body->getCenterOfMassPosition());
	}
}

PhysicsHandle *PhysicsSystem::add_sphere(Scene::Node *node, const MaterialInfo &info)
{
	auto *shape = new btSphereShape(btScalar(node->transform.scale.x));
	auto *handle = add_shape(node, info, shape);
	return handle;
}

PhysicsHandle *PhysicsSystem::add_infinite_plane(const vec4 &plane, const MaterialInfo &info)
{
	auto *shape = new btStaticPlaneShape(convert(plane.xyz()), plane.w);

	MaterialInfo tmp = info;
	tmp.type = InteractionType::Static;
	tmp.mass = 0.0f;
	tmp.restitution = 1.0f;

	auto *handle = add_shape(nullptr, tmp, shape);
	return handle;
}

void PhysicsSystem::apply_impulse(PhysicsHandle *handle, const vec3 &impulse, const vec3 &world_position)
{
	auto *body = btRigidBody::upcast(handle->bt_object);
	if (body)
	{
		body->activate();
		body->applyImpulse(convert(impulse), convert(world_position) - body->getCenterOfMassPosition());
	}
}

void PhysicsSystem::add_point_constraint(PhysicsHandle *handle, const vec3 &local_pivot)
{
	auto *body = btRigidBody::upcast(handle->bt_object);
	if (!body)
		return;

	auto *constraint = new btPoint2PointConstraint(*body, convert(local_pivot));
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
			convert(local_pivot0),
			convert(local_pivot1));

	world->addConstraint(constraint, skip_collision);
	body0->addConstraintRef(constraint);
	body1->addConstraintRef(constraint);
}

struct TriggerContactResultCallback : btCollisionWorld::ContactResultCallback
{
	bool hit = false;

	btScalar addSingleResult(btManifoldPoint &point,
	                         const btCollisionObjectWrapper *, int, int,
	                         const btCollisionObjectWrapper *, int, int) override
	{
		if (point.getDistance() <= 0.0f)
		{
			hit = true;
			return btScalar(0);
		}
		else
			return btScalar(1);
	}
};

bool PhysicsSystem::get_overlapping_objects(PhysicsHandle *handle, vector<PhysicsHandle *> &other,
                                            OverlapMethod method)
{
	other.clear();
	auto *ghost = btPairCachingGhostObject::upcast(handle->bt_object);
	if (!ghost)
		return false;

	auto &pairs = ghost->getOverlappingPairs();

	int count = pairs.size();
	other.reserve(count);
	for (int i = 0; i < count; i++)
	{
		auto *object = pairs[i];

		bool response = (ghost->getBroadphaseHandle()->m_collisionFilterGroup &
		                 object->getBroadphaseHandle()->m_collisionFilterMask) != 0;

		response = response &&
				(object->getBroadphaseHandle()->m_collisionFilterGroup &
				 ghost->getBroadphaseHandle()->m_collisionFilterMask) != 0;

		if (!response)
			continue;

		if (method == OverlapMethod::Broadphase)
		{
			other.push_back(static_cast<PhysicsHandle *>(object->getUserPointer()));
		}
		else if (method == OverlapMethod::Nearphase)
		{
			TriggerContactResultCallback cb;
			world->contactPairTest(ghost, object, cb);
			if (cb.hit)
				other.push_back(static_cast<PhysicsHandle *>(object->getUserPointer()));
		}
	}

	return true;
}

PhysicsComponent::~PhysicsComponent()
{
	if (handle)
		Global::physics()->remove_body(handle);
}
}
