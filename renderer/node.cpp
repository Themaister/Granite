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

#include "node.hpp"
#include "scene.hpp"

namespace Granite
{
Node::Node(Scene &parent_)
	: parent_scene(parent_)
{
	if (!parent_scene.get_transforms().allocate(1, &transform))
	{
		LOGE("Transform pool is exhausted.\n");
	}
	else
	{
		auto &t = get_transform();
		t.rotation = quat(1.0f, 0.0f, 0.0f, 0.0f);
		t.translation = vec3(0.0f);
		t.scale = vec3(1.0f);
	}

	node_is_pending_update.store(false, std::memory_order_relaxed);
	invalidate_cached_transform();
	assert(node_is_pending_update.is_lock_free());
}

Transform &Node::get_transform()
{
	return parent_scene.get_transforms().get_transforms()[transform.offset];
}

mat4 &Node::get_cached_transform()
{
	return parent_scene.get_transforms().get_cached_transforms()[transform.offset];
}

mat4 &Node::get_cached_prev_transform()
{
	return parent_scene.get_transforms().get_cached_prev_transforms()[transform.offset];
}

Transform *Node::get_transform_base()
{
	return parent_scene.get_transforms().get_transforms();
}

mat4 *Node::get_skin_cached()
{
	assert(skinning);
	return parent_scene.get_transforms().get_cached_transforms() + skinning->transform.offset;
}

mat4 *Node::get_skin_prev_cached()
{
	assert(skinning);
	return parent_scene.get_transforms().get_cached_prev_transforms() + skinning->transform.offset;
}

Node::~Node()
{
	if (skinning)
	{
		if (skinning->transform.count)
			parent_scene.get_transforms().free(skinning->transform);
		parent_scene.skinning_pool.free(skinning);
	}

	if (transform.count)
		parent_scene.get_transforms().free(transform);
}

void Node::set_skin(Skinning *skinning_)
{
	if (skinning)
	{
		if (skinning->transform.count)
			parent_scene.get_transforms().free(skinning->transform);
		parent_scene.skinning_pool.free(skinning);
	}
	skinning = skinning_;

	if (!parent_scene.get_transforms().allocate(skinning->skin.size(), &skinning->transform))
	{
		LOGE("Transform pool is exhausted.\n");
		parent_scene.skinning_pool.free(skinning);
		skinning = nullptr;
	}
}

unsigned Node::get_dirty_transform_depth() const
{
	unsigned level_candidate = 0;
	unsigned level = 0;

	auto *node = this;
	while (node->parent)
	{
		level++;
		if (node->parent->node_is_pending_update.load(std::memory_order_relaxed))
			level_candidate = level;
		node = node->parent;
	}

	return level_candidate;
}

void Node::add_child(NodeHandle node)
{
	assert(this != node.get());
	assert(node->parent == nullptr);
	node->parent = this;
	node->invalidate_cached_transform();
	children.push_back(node);
}

NodeHandle Node::remove_child(Node *node)
{
	assert(node->parent == this);
	node->parent = nullptr;
	auto handle = node->reference_from_this();
	node->invalidate_cached_transform();

	auto itr = remove_if(begin(children), end(children), [&](const NodeHandle &h) {
	  return node == h.get();
	});
	assert(itr != end(children));
	children.erase(itr, end(children));
	return handle;
}

NodeHandle Node::remove_node_from_hierarchy(Node *node)
{
	if (node->parent)
		return node->parent->remove_child(node);
	else
		return NodeHandle(nullptr);
}

void Node::invalidate_cached_transform()
{
	// Order does not matter. We will synchronize where we actually read from this.
	if (!node_is_pending_update.exchange(true, std::memory_order_relaxed))
		parent_scene.push_pending_node_update(this);
}
}
