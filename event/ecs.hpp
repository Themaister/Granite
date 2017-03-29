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

class EntityGroupBase
{
public:
	virtual ~EntityGroupBase() = default;
};

template <typename... Ts>
class EntityGroup : public EntityGroupBase
{
public:
private:
	std::vector<std::tuple<Ts *...>> groups;
	std::vector<Entity *> entities;
};

struct ComponentBase
{

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

	void free_component(ComponentBase *component) override
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
	EntityHandle create_entity();

	template <typename T>
	T *allocate_component()
	{
		uint32_t id = ComponentIDMapping::get_id<T>();
		return nullptr;
	}

	void free_component(uint32_t id, ComponentBase *component);

private:
	Util::ObjectPool<Entity> entities;
	std::unordered_map<uint32_t, std::unique_ptr<EntityGroupBase>> groups;
	std::unordered_map<uint32_t, std::unique_ptr<ComponentAllocatorBase>> components;
	std::unordered_map<uint32_t, std::unordered_set<uint32_t>> component_to_groups;
};
}