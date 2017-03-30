#pragma once

#include <tuple>
#include <vector>
#include <memory>
#include <unordered_set>
#include <unordered_map>
#include "object_pool.hpp"

namespace Granite
{
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

struct ComponentBase
{

};

class EntityGroupBase
{
public:
	virtual ~EntityGroupBase() = default;
	virtual void add_entity(const Entity &entity) = 0;
	virtual void remove_component(uint32_t id, ComponentBase *component) = 0;
};

template <typename... Ts>
class EntityGroup : public EntityGroupBase
{
public:
	EntityGroup()
	{
		set_ids<Ts...>(group_ids);
	}

	void add_entity(const Entity &entity) override final
	{
	}

	void remove_component(uint32_t id, ComponentBase *component) override final
	{
	}

private:
	std::vector<std::tuple<Ts *...>> groups;
	std::vector<Entity *> entities;
	uint32_t group_ids[sizeof...(Ts)];

	template <typename U, typename... Us>
	void set_ids(uint32_t *ids)
	{
		*ids = ComponentIDMapping::get_id<U>();
		set_ids<Us...>(ids + 1);
	}

	template <typename U>
	void set_ids(uint32_t *ids)
	{
		*ids = ComponentIDMapping::get_id<U>();
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

struct Entity
{
	std::unordered_map<uint32_t, ComponentBase *> components;
};

using EntityHandle = std::shared_ptr<Entity>;

class EntityPool
{
public:
	EntityHandle create_entity()
	{
		return std::make_shared<Entity>();
	}

	template <typename T, typename... Ts>
	void allocate_component(Entity &entity, Ts&&... ts)
	{
		uint32_t id = ComponentIDMapping::get_id<T>();
		auto itr = components.find(id);
		if (itr == end(components))
			itr = components.insert(make_pair(id, std::unique_ptr<ComponentAllocatorBase>(new ComponentAllocator<T>)));

		auto *allocator = static_cast<ComponentAllocator<T> *>(itr->second.get());
		auto &comp = entity.components[id];
		if (comp)
		{
			allocator->free_component(comp);
			comp = allocator->pool.allocate(std::forward<Ts>(ts)...);
		}
		else
		{
			comp = allocator->pool.allocate(std::forward<Ts>(ts)...);
			for (auto &group : component_to_groups[id])
				groups[group]->add_entity(entity);
		}
	}

	void free_component(uint32_t id, ComponentBase *component)
	{
		components[id]->free_component(component);
		for (auto &group : component_to_groups[id])
			groups[group]->remove_component(id, component);
	}

private:
	Util::ObjectPool<Entity> entities;
	std::unordered_map<uint32_t, std::unique_ptr<EntityGroupBase>> groups;
	std::unordered_map<uint32_t, std::unique_ptr<ComponentAllocatorBase>> components;
	std::unordered_map<uint32_t, std::unordered_set<uint32_t>> component_to_groups;
};
}