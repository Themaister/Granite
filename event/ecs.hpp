/* Copyright (c) 2017 Hans-Kristian Arntzen
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

#include <tuple>
#include <vector>
#include <memory>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include "object_pool.hpp"
#include "intrusive.hpp"
#include <assert.h>

namespace Granite
{
struct ComponentBase
{
};

namespace Internal
{
template <size_t Index>
struct HasComponent
{
	template <typename T>
	static bool has_component(const T &t, const ComponentBase *component)
	{
		return static_cast<ComponentBase *>(std::get<Index>(t)) == component ||
		       HasComponent<Index - 1>::has_component(t, component);
	}
};

template <>
struct HasComponent<0>
{
	template <typename T>
	static bool has_component(const T &t, const ComponentBase *component)
	{
		return static_cast<ComponentBase *>(std::get<0>(t)) == component;
	}
};
}

class Entity;

struct ComponentIDMapping
{
public:
	template <typename T>
	static uint32_t get_id()
	{
		static uint32_t id = ids++;
		return id;
	}

	template <typename... Ts>
	static uint32_t get_group_id()
	{
		static uint32_t id = group_ids++;
		return id;
	}

private:
	static uint32_t ids;
	static uint32_t group_ids;
};

class EntityGroupBase
{
public:
	virtual ~EntityGroupBase() = default;
	virtual void add_entity(Entity &entity) = 0;
	virtual void remove_component(ComponentBase *component) = 0;
};

class EntityPool;

struct EntityDeleter
{
	void operator()(Entity *entity);
};

class Entity : public Util::IntrusivePtrEnabled<Entity, EntityDeleter>
{
public:
	Entity(EntityPool *pool)
		: pool(pool)
	{
	}

	bool has_component(uint32_t id) const
	{
		auto itr = components.find(id);
		return itr != std::end(components) && itr->second;
	}

	template <typename T>
	bool has_component() const
	{
		return has_component(ComponentIDMapping::get_id<T>());
	}

	template <typename T>
	T *get_component()
	{
		auto itr = components.find(ComponentIDMapping::get_id<T>());
		if (itr == std::end(components))
			return nullptr;

		return static_cast<T *>(itr->second);
	}

	template <typename T>
	const T *get_component() const
	{
		auto itr = components.find(ComponentIDMapping::get_id<T>());
		if (itr == std::end(components))
			return nullptr;

		return static_cast<T *>(itr->second);
	}

	template <typename T, typename... Ts>
	T *allocate_component(Ts&&... ts);

	template <typename T>
	void free_component();

	std::unordered_map<uint32_t, ComponentBase *> &get_components()
	{
		return components;
	}

	EntityPool *get_pool()
	{
		return pool;
	}

private:
	EntityPool *pool;
	std::unordered_map<uint32_t, ComponentBase *> components;
};

template <typename... Ts>
class EntityGroup : public EntityGroupBase
{
public:
	void add_entity(Entity &entity) override final
	{
		if (has_all_components<Ts...>(entity))
		{
			entities.push_back(&entity);
			groups.push_back(std::make_tuple(entity.get_component<Ts>()...));
		}
	}

	void remove_component(ComponentBase *component) override final
	{
		auto itr = std::find_if(std::begin(groups), std::end(groups), [&](const std::tuple<Ts *...> &t) {
			return has_component(t, component);
		});

		if (itr == end(groups))
			return;

		auto offset = size_t(itr - begin(groups));
		if (offset != groups.size() - 1)
		{
			std::swap(groups[offset], groups.back());
			std::swap(entities[offset], entities.back());
		}
		groups.pop_back();
		entities.pop_back();
	}

	std::vector<std::tuple<Ts *...>> &get_groups()
	{
		return groups;
	}

private:
	std::vector<std::tuple<Ts *...>> groups;
	std::vector<Entity *> entities;

	template <typename... Us>
	struct HasAllComponents;

	template <typename U, typename... Us>
	struct HasAllComponents<U, Us...>
	{
		static bool has_component(const Entity &entity)
		{
			return entity.has_component(ComponentIDMapping::get_id<U>()) &&
		           HasAllComponents<Us...>::has_component(entity);
		}
	};

	template <typename U>
	struct HasAllComponents<U>
	{
		static bool has_component(const Entity &entity)
		{
			return entity.has_component(ComponentIDMapping::get_id<U>());
		}
	};

	template <typename... Us>
	bool has_all_components(const Entity &entity)
	{
		return HasAllComponents<Us...>::has_component(entity);
	}

	template <typename... Us>
	static bool has_component(const std::tuple<Us *...> &t, const ComponentBase *component)
	{
		return Internal::HasComponent<sizeof...(Us) - 1>::has_component(t, component);
	}
};

class ComponentAllocatorBase
{
public:
	virtual ~ComponentAllocatorBase() = default;
	virtual void free_component(ComponentBase *component) = 0;
};

template <typename T>
struct ComponentAllocator : public ComponentAllocatorBase
{
	Util::ObjectPool<T> pool;

	void free_component(ComponentBase *component) override final
	{
		pool.free(static_cast<T *>(component));
	}
};

using EntityHandle = Util::IntrusivePtr<Entity>;

class EntityPool
{
public:
	EntityHandle create_entity()
	{
		auto itr = EntityHandle(entity_pool.allocate(this));
		entities.push_back(itr.get());
		return itr;
	}

	void delete_entity(Entity *entity)
	{
		auto &components = entity->get_components();
		for (auto &component : components)
			if (component.second)
				free_component(component.first, component.second);
		entity_pool.free(entity);

		auto itr = std::find(std::begin(entities), std::end(entities), entity);
		auto offset = size_t(itr - std::begin(entities));
		if (offset != entities.size() - 1)
			std::swap(entities[offset], entities.back());
		entities.pop_back();
	}

	template <typename... Ts>
	std::vector<std::tuple<Ts *...>> &get_component_group()
	{
		uint32_t group_id = ComponentIDMapping::get_group_id<Ts...>();
		auto itr = groups.find(group_id);
		if (itr == std::end(groups))
		{
			register_group<Ts...>(group_id);
			auto tmp = groups.insert(std::make_pair(group_id, std::unique_ptr<EntityGroupBase>(new EntityGroup<Ts...>())));
			itr = tmp.first;

			auto *group = static_cast<EntityGroup<Ts...> *>(itr->second.get());
			for (auto &entity : entities)
				group->add_entity(*entity);
		}

		auto *group = static_cast<EntityGroup<Ts...> *>(itr->second.get());
		return group->get_groups();
	}

	template <typename T, typename... Ts>
	T *allocate_component(Entity &entity, Ts&&... ts)
	{
		uint32_t id = ComponentIDMapping::get_id<T>();
		auto itr = components.find(id);
		if (itr == std::end(components))
		{
			auto tmp = components.insert(std::make_pair(id, std::unique_ptr<ComponentAllocatorBase>(new ComponentAllocator<T>)));
			itr = tmp.first;
		}

		auto *allocator = static_cast<ComponentAllocator<T> *>(itr->second.get());
		auto &comp = entity.get_components()[id];
		if (comp)
		{
			allocator->free_component(comp);
			comp = allocator->pool.allocate(std::forward<Ts>(ts)...);
			for (auto &group : component_to_groups[id])
				groups[group]->add_entity(entity);
		}
		else
		{
			comp = allocator->pool.allocate(std::forward<Ts>(ts)...);
			for (auto &group : component_to_groups[id])
				groups[group]->add_entity(entity);
		}
		return static_cast<T *>(comp);
	}

	void free_component(uint32_t id, ComponentBase *component)
	{
		components[id]->free_component(component);
		for (auto &group : component_to_groups[id])
			groups[group]->remove_component(component);
	}

	void reset_groups();

private:
	Util::ObjectPool<Entity> entity_pool;
	std::unordered_map<uint32_t, std::unique_ptr<EntityGroupBase>> groups;
	std::unordered_map<uint32_t, std::unique_ptr<ComponentAllocatorBase>> components;
	std::unordered_map<uint32_t, std::unordered_set<uint32_t>> component_to_groups;
	std::vector<Entity *> entities;

	template <typename... Us>
	struct GroupRegisters;

	template <typename U, typename... Us>
	struct GroupRegisters<U, Us...>
	{
		static void register_group(std::unordered_map<uint32_t, std::unordered_set<uint32_t>> &groups,
		                           uint32_t group_id)
		{
			groups[ComponentIDMapping::get_id<U>()].insert(group_id);
			GroupRegisters<Us...>::register_group(groups, group_id);
		}
	};

	template <typename U>
	struct GroupRegisters<U>
	{
		static void register_group(std::unordered_map<uint32_t, std::unordered_set<uint32_t>> &groups,
		                           uint32_t group_id)
		{
			groups[ComponentIDMapping::get_id<U>()].insert(group_id);
		}
	};

	template <typename U, typename... Us>
	void register_group(uint32_t group_id)
	{
		GroupRegisters<U, Us...>::register_group(component_to_groups, group_id);
	}
};

template <typename T, typename... Ts>
T *Entity::allocate_component(Ts&&... ts)
{
	return pool->allocate_component<T>(*this, std::forward<Ts>(ts)...);
}

template <typename T>
void Entity::free_component()
{
	auto id = ComponentIDMapping::get_id<T>();
	auto itr = components.find(id);
	if (itr != std::end(components))
	{
		assert(itr->second);
		pool->free_component(id, itr->second);
		components.erase(itr);
	}
}

}