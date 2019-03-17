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
class btDbvtBroadphase;
class btSequentialImpulseConstraintSolver;
class btDiscreteDynamicsWorld;
class btCollisionShape;

namespace Granite
{
struct PhysicsHandle;

struct PhysicsComponent : ComponentBase
{
	GRANITE_COMPONENT_TYPE_DECL(PhysicsComponent)
	PhysicsHandle *handle = nullptr;
	~PhysicsComponent();
};

class PhysicsSystem
{
public:
	PhysicsSystem();
	~PhysicsSystem();

	PhysicsHandle *add_cube(Scene::Node *node, float mass);
	PhysicsHandle *add_sphere(Scene::Node *node, float mass);
	PhysicsHandle *add_infinite_plane(const vec4 &plane);
	void remove_body(PhysicsHandle *handle);

	void apply_impulse(PhysicsHandle *handle, const vec3 &impulse, const vec3 &relative);

	void iterate(double frame_time);

private:
	std::unique_ptr<btDefaultCollisionConfiguration> collision_config;
	std::unique_ptr<btCollisionDispatcher> dispatcher;
	std::unique_ptr<btDbvtBroadphase> broadphase;
	std::unique_ptr<btSequentialImpulseConstraintSolver> solver;
	std::unique_ptr<btDiscreteDynamicsWorld> world;

	Util::ObjectPool<PhysicsHandle> handle_pool;
	std::vector<PhysicsHandle *> handles;

	PhysicsHandle *add_shape(Scene::Node *node, float mass, btCollisionShape *shape);
};
}