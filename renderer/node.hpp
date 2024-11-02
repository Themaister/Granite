/* Copyright (c) 2017-2024 Hans-Kristian Arntzen
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

#include "intrusive.hpp"
#include "math.hpp"
#include "hash.hpp"
#include "arena_allocator.hpp"
#include <vector>

namespace Granite
{
class Node;
class Scene;

struct Transform
{
	vec3 scale;
	vec3 translation;
	quat rotation;
};

struct NodeDeleter
{
	void operator()(Node *node);
};

// TODO: Need to slim this down, and be more data oriented.
// Should possibly maintain separate large buffers with transform matrices, and just point to those
// in the node.
class Node : public Util::IntrusivePtrEnabled<Node, NodeDeleter>
{
public:
	explicit Node(Scene &parent_);
	~Node();

	Scene &parent_scene;
	Util::AllocatedSlice transform;

	Transform &get_transform();
	mat4 &get_cached_transform();
	mat4 &get_cached_prev_transform();
	Transform *get_transform_base();
	mat4 *get_skin_cached();
	mat4 *get_skin_prev_cached();

	void invalidate_cached_transform();
	void add_child(Util::IntrusivePtr<Node> node);
	Util::IntrusivePtr<Node> remove_child(Node *node);
	static Util::IntrusivePtr<Node> remove_node_from_hierarchy(Node *node);

	inline const std::vector<Util::IntrusivePtr<Node>> &get_children() const
	{
		return children;
	}

	inline std::vector<Util::IntrusivePtr<Node>> &get_children()
	{
		return children;
	}

	inline Node *get_parent() const
	{
		return parent;
	}

	struct Skinning
	{
		Util::AllocatedSlice transform;
		std::vector<uint32_t> skin;
		std::vector<mat4> inverse_bind_poses;
		Util::Hash skin_compat = 0;
	};

	void set_skin(Skinning *skinning_);

	inline Skinning *get_skin()
	{
		return skinning;
	}

	inline void update_timestamp()
	{
		timestamp++;
	}

	inline const uint32_t *get_timestamp_pointer() const
	{
		return &timestamp;
	}

	unsigned get_dirty_transform_depth() const;

	inline bool test_and_set_pending_update_no_atomic()
	{
		bool value = node_is_pending_update.load(std::memory_order_relaxed);
		if (!value)
			node_is_pending_update.store(true, std::memory_order_relaxed);
		return value;
	}

	inline void clear_pending_update_no_atomic()
	{
		node_is_pending_update.store(false, std::memory_order_relaxed);
	}

private:
	std::vector<Util::IntrusivePtr<Node>> children;
	Skinning *skinning = nullptr;
	Node *parent = nullptr;
	uint32_t timestamp = 0;
	std::atomic_bool node_is_pending_update;
};
using NodeHandle = Util::IntrusivePtr<Node>;
}
