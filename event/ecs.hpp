#pragma once

#include <tuple>
#include <vector>
#include <memory>
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

class ComponentAllocatorBase
{
public:
	virtual ~ComponentAllocatorBase() = default;
};

template <typename T>
class ComponentAllocator : public ComponentAllocatorBase
{
public:
private:
	Util::ObjectPool<T> pool;
};

class EntityPool
{
public:
private:
	std::unordered_map<uint32_t, std::unique_ptr<EntityGroupBase>> groups;
	std::unordered_map<uint32_t, std::unique_ptr<ComponentAllocatorBase>> components;
};
}