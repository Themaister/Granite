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

#include "ecs.hpp"

namespace Granite
{
Entity *EntityPool::create_entity()
{
	Util::Hasher hasher;
	hasher.u64(++cookie);
	auto *entity = entity_pool.allocate(this, hasher.get());
	entity->pool_offset = entities.size();
	entities.push_back(entity);
	return entity;
}

void EntityPool::free_component(Entity &entity, ComponentType id, ComponentNode *component)
{
	auto *c = component_types.find(id);
	assert(c);
	c->free_component(component->get());
	component_nodes.free(component);

	auto *component_groups = component_to_groups.find(id);
	if (component_groups)
	{
		for (auto &group : *component_groups)
		{
			auto *g = groups.find(group.get_hash());
			if (g)
				g->remove_entity(entity);
		}
	}
}

void EntityPool::delete_entity(Entity *entity)
{
	{
		auto &components = entity->get_components();
		auto &list = components.inner_list();
		auto itr = list.begin();
		while (itr != list.end())
		{
			auto *component = itr.get();
			itr = list.erase(itr);
			free_component(*entity, component->get_hash(), component);
		}
	}

	auto offset = entity->pool_offset;
	assert(offset < entities.size());

	entities[offset] = entities.back();
	entities[offset]->pool_offset = offset;
	entities.pop_back();
	entity_pool.free(entity);
}

EntityPool::~EntityPool()
{
	{
		auto &list = component_types.inner_list();
		auto itr = list.begin();
		while (itr != list.end())
		{
			auto *to_free = itr.get();
			itr = list.erase(itr);
			delete to_free;
		}
	}

	reset_groups();
	free_groups();
}

void EntityDeleter::operator()(Entity *entity)
{
	entity->get_pool()->delete_entity(entity);
}

void EntityPool::free_groups()
{
	auto &list = groups.inner_list();
	auto itr = list.begin();
	while (itr != list.end())
	{
		auto *to_free = itr.get();
		itr = list.erase(itr);
		delete to_free;
	}
	groups.clear();
}

void EntityPool::reset_groups()
{
	for (auto &group : groups)
		group.reset();
}

void EntityPool::reset_groups_for_component_type(ComponentType id)
{
	auto *component_groups = component_to_groups.find(id);
	if (component_groups)
	{
		for (auto &group : *component_groups)
		{
			auto *g = groups.find(group.get_hash());
			if (g)
				g->reset();
		}
	}
}

void ComponentSet::insert(ComponentType type)
{
	set.emplace_yield(type);
}
}