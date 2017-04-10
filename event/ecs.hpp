#pragma once

#include <tuple>
#include <vector>
#include <memory>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
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

class Entity
{
public:
	bool has_component(uint32_t id) const
	{
		auto itr = components.find(id);
		return itr != std::end(components) && itr->second;
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

	std::unordered_map<uint32_t, ComponentBase *> &get_components()
	{
		return components;
	}

private:
	std::unordered_map<uint32_t, ComponentBase *> components;
};

template <typename... Ts>
class EntityGroup : public EntityGroupBase
{
public:
	EntityGroup()
	{
		set_ids<Ts...>(group_ids);
	}

	void add_entity(Entity &entity) override final
	{
		if (has_all_components<Ts...>(entity))
		{
			entities.push_back(&entity);
			groups.push_back(make_tuple(entity.get_component<Ts>()...));
		}
	}

	void remove_component(uint32_t id, ComponentBase *component) override final
	{
		auto itr = std::find_if(begin(groups), end(groups), [&](const std::tuple<Ts *...> &t) {
			return has_component(t, component);
		});

		if (itr == end(groups))
			return;

		auto offset = itr - begin(groups);
		if (offset != groups.size() - 1)
		{
			std::swap(groups[offset], groups.back());
			std::swap(entities[offset], entities.back());
		}
		groups.pop_back();
		entities.pop_back();
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

	template <typename U, typename... Us>
	bool has_all_components(const Entity &entity)
	{
		return entity.has_component(ComponentIDMapping::get_id<U>()) &&
	           has_all_components<Us...>(entity);
	}

	template <typename U>
	bool has_all_components(const Entity &entity)
	{
		return entity.has_component(ComponentIDMapping::get_id<U>());
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

using EntityHandle = std::shared_ptr<Entity>;

class EntityPool
{
public:
	EntityHandle create_entity()
	{
		auto itr = EntityHandle(entity_pool.allocate(), EntityDeleter(this));
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
		auto offset = itr - std::begin(entities);
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
			itr = groups.insert(std::make_pair(group_id, std::unique_ptr<EntityGroupBase>(new EntityGroup<Ts...>())));

			auto *group = static_cast<EntityGroup<Ts...> *>(itr->second.get());
			for (auto &entity : entities)
				group->add_entity(entity);
		}

		auto *group = static_cast<EntityGroup<Ts...> *>(itr->second.get());
		return group;
	}

	template <typename T, typename... Ts>
	void allocate_component(Entity &entity, Ts&&... ts)
	{
		uint32_t id = ComponentIDMapping::get_id<T>();
		auto itr = components.find(id);
		if (itr == std::end(components))
			itr = components.insert(std::make_pair(id, std::unique_ptr<ComponentAllocatorBase>(new ComponentAllocator<T>)));

		auto *allocator = static_cast<ComponentAllocator<T> *>(itr->second.get());
		auto &comp = entity.components[id];
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
	}

	void free_component(uint32_t id, ComponentBase *component)
	{
		components[id]->free_component(component);
		for (auto &group : component_to_groups[id])
			groups[group]->remove_component(id, component);
	}

private:
	Util::ObjectPool<Entity> entity_pool;
	std::unordered_map<uint32_t, std::unique_ptr<EntityGroupBase>> groups;
	std::unordered_map<uint32_t, std::unique_ptr<ComponentAllocatorBase>> components;
	std::unordered_map<uint32_t, std::unordered_set<uint32_t>> component_to_groups;
	std::vector<Entity *> entities;

	template <typename U, typename... Us>
	void register_group(uint32_t group_id)
	{
		component_to_groups[ComponentIDMapping::get_id<U>()].insert(group_id);
		register_group<Us...>(group_id);
	}

	template <typename U>
	void register_group(uint32_t group_id)
	{
		component_to_groups[ComponentIDMapping::get_id<U>()].insert(group_id);
	}

	struct EntityDeleter
	{
		EntityDeleter(EntityPool *pool)
			: pool(pool)
		{
		}

		void operator()(Entity *entity)
		{
			pool->delete_entity(entity);
		}

		EntityPool *pool;
	};
};
}