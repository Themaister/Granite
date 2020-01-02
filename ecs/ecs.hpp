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

#pragma once

#include <tuple>
#include <vector>
#include <memory>
#include <algorithm>
#include "object_pool.hpp"
#include "intrusive.hpp"
#include "intrusive_hash_map.hpp"
#include "compile_time_hash.hpp"
#include "enum_cast.hpp"
#include <assert.h>

namespace Granite
{
struct ComponentBase
{
};

template <typename T, typename Tup>
inline T *get_component(Tup &t)
{
	return std::get<T *>(t);
}

template <typename T>
inline T *get(const std::tuple<T *> &t)
{
	return std::get<0>(t);
}

template <typename... Ts>
using ComponentGroupVector = std::vector<std::tuple<Ts *...>>;

class Entity;

#define GRANITE_COMPONENT_TYPE_HASH(x) ::Util::compile_time_fnv1(#x)
using ComponentType = uint64_t;

#define GRANITE_COMPONENT_TYPE_DECL(x) \
enum class ComponentTypeWrapper : ::Granite::ComponentType { \
	type_id = GRANITE_COMPONENT_TYPE_HASH(x) \
}; \
static inline constexpr ::Granite::ComponentType get_component_id_hash() { \
	return ::Granite::ComponentType(ComponentTypeWrapper::type_id); \
}

struct ComponentSetKey : Util::IntrusiveHashMapEnabled<ComponentSetKey>
{
};

class ComponentSet : public Util::IntrusiveHashMapEnabled<ComponentSet>
{
public:
	void insert(ComponentType type);

	Util::IntrusiveList<ComponentSetKey>::Iterator begin()
	{
		return set.begin();
	}

	Util::IntrusiveList<ComponentSetKey>::Iterator end()
	{
		return set.end();
	}

private:
	Util::IntrusiveHashMap<ComponentSetKey> set;
};

using ComponentNode = Util::IntrusivePODWrapper<ComponentBase *>;
using ComponentHashMap = Util::IntrusiveHashMapHolder<ComponentNode>;
using ComponentGroupHashMap = Util::IntrusiveHashMap<ComponentSet>;

struct ComponentIDMapping
{
	template <typename T>
	constexpr static Util::Hash get_id()
	{
		enum class Result : Util::Hash { result = T::get_component_id_hash() };
		return Util::Hash(Result::result);
	}

	template <typename... Ts>
	constexpr static Util::Hash get_group_id()
	{
		enum class Result : Util::Hash { result = Util::compile_time_fnv1_merged(Ts::get_component_id_hash()...) };
		return Util::Hash(Result::result);
	}
};

class EntityGroupBase : public Util::IntrusiveHashMapEnabled<EntityGroupBase>
{
public:
	virtual ~EntityGroupBase() = default;
	virtual void add_entity(Entity &entity) = 0;
	virtual void remove_entity(const Entity &entity) = 0;
	virtual void reset() = 0;
};

class EntityPool;

struct EntityDeleter
{
	void operator()(Entity *entity);
};

class Entity : public Util::IntrusiveListEnabled<Entity>
{
public:
	friend class EntityPool;

	Entity(EntityPool *pool_, Util::Hash hash_)
		: pool(pool_), hash(hash_)
	{
	}

	bool has_component(ComponentType id) const
	{
		auto itr = components.find(id);
		return itr != nullptr;
	}

	template <typename T>
	bool has_component() const
	{
		return has_component(ComponentIDMapping::get_id<T>());
	}

	template <typename T>
	T *get_component()
	{
		auto *t = components.find(ComponentIDMapping::get_id<T>());
		if (t)
			return static_cast<T *>(t->get());
		else
			return nullptr;
	}

	template <typename T>
	const T *get_component() const
	{
		auto *t = components.find(ComponentIDMapping::get_id<T>());
		if (t)
			return static_cast<const T *>(t->get());
		else
			return nullptr;
	}

	template <typename T, typename... Ts>
	T *allocate_component(Ts&&... ts);

	template <typename T>
	void free_component();

	ComponentHashMap &get_components()
	{
		return components;
	}

	EntityPool *get_pool()
	{
		return pool;
	}

	Util::Hash get_hash() const
	{
		return hash;
	}

	bool mark_for_destruction()
	{
		bool ret = !marked;
		marked = true;
		return ret;
	}

private:
	EntityPool *pool;
	Util::Hash hash;
	size_t pool_offset = 0;
	ComponentHashMap components;
	bool marked = false;
};

template <typename... Ts>
class EntityGroup : public EntityGroupBase
{
public:
	void add_entity(Entity &entity) override final
	{
		if (has_all_components<Ts...>(entity))
		{
			entity_to_index[entity.get_hash()].get() = entities.size();
			groups.push_back(std::make_tuple(entity.get_component<Ts>()...));
			entities.push_back(&entity);
		}
	}

	void remove_entity(const Entity &entity) override final
	{
		size_t offset;
		if (entity_to_index.find_and_consume_pod(entity.get_hash(), offset))
		{
			entities[offset] = entities.back();
			groups[offset] = groups.back();
			entity_to_index[entities[offset]->get_hash()].get() = offset;

			entity_to_index.erase(entity.get_hash());
			entities.pop_back();
			groups.pop_back();
		}
	}

	const ComponentGroupVector<Ts...> &get_groups() const
	{
		return groups;
	}

	const std::vector<Entity *> &get_entities() const
	{
		return entities;
	}

	void reset() override final
	{
		groups.clear();
		entities.clear();
		entity_to_index.clear();
	}

private:
	ComponentGroupVector<Ts...> groups;
	std::vector<Entity *> entities;
	Util::IntrusiveHashMap<Util::IntrusivePODWrapper<size_t>> entity_to_index;

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
};

class ComponentAllocatorBase : public Util::IntrusiveHashMapEnabled<ComponentAllocatorBase>
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

class EntityPool
{
public:
	~EntityPool();

	EntityPool() = default;
	void operator=(const EntityPool &) = delete;
	EntityPool(const EntityPool &) = delete;

	Entity *create_entity();
	void delete_entity(Entity *entity);

	template <typename... Ts>
	EntityGroup<Ts...> *get_component_group_holder()
	{
		ComponentType group_id = ComponentIDMapping::get_group_id<Ts...>();
		auto *t = groups.find(group_id);
		if (!t)
		{
			register_group<Ts...>(group_id);

			t = new EntityGroup<Ts...>();
			t->set_hash(group_id);
			groups.insert_yield(t);

			auto *group = static_cast<EntityGroup<Ts...> *>(t);
			for (auto &entity : entities)
				group->add_entity(*entity);
		}

		return static_cast<EntityGroup<Ts...> *>(t);
	}

	template <typename... Ts>
	const ComponentGroupVector<Ts...> &get_component_group()
	{
		auto *group = get_component_group_holder<Ts...>();
		return group->get_groups();
	}

	template <typename... Ts>
	const std::vector<Entity *> &get_component_entities()
	{
		auto *group = get_component_group_holder<Ts...>();
		return group->get_entities();
	}

	template <typename T, typename... Ts>
	T *allocate_component(Entity &entity, Ts&&... ts)
	{
		ComponentType id = ComponentIDMapping::get_id<T>();
		auto *t = component_types.find(id);
		if (!t)
		{
			t = new ComponentAllocator<T>();
			t->set_hash(id);
			component_types.insert_yield(t);
		}

		auto *allocator = static_cast<ComponentAllocator<T> *>(t);
		auto *existing = entity.components.find(id);

		if (existing)
		{
			auto *comp = static_cast<T *>(existing->get());
			// In-place modify. Destroy old data, and in-place construct.
			// Do not need to fiddle with data structures internally.
			comp->~T();
			return new (comp) T(std::forward<Ts>(ts)...);
		}
		else
		{
			auto *comp = allocator->pool.allocate(std::forward<Ts>(ts)...);
			auto *node = component_nodes.allocate(comp);
			node->set_hash(id);
			entity.components.insert_replace(node);

			auto *component_groups = component_to_groups.find(id);
			if (component_groups)
				for (auto &group : *component_groups)
					groups.find(group.get_hash())->add_entity(entity);

			return comp;
		}
	}

	void free_component(Entity &entity, ComponentType id, ComponentNode *component);
	void reset_groups();
	void reset_groups_for_component_type(ComponentType id);

private:
	Util::ObjectPool<Entity> entity_pool;
	Util::IntrusiveHashMapHolder<EntityGroupBase> groups;
	Util::IntrusiveHashMapHolder<ComponentAllocatorBase> component_types;
	Util::ObjectPool<ComponentNode> component_nodes;
	ComponentGroupHashMap component_to_groups;
	std::vector<Entity *> entities;
	uint64_t cookie = 0;

	template <typename... Us>
	struct GroupRegisters;

	template <typename U, typename... Us>
	struct GroupRegisters<U, Us...>
	{
		static void register_group(ComponentGroupHashMap &groups,
		                           ComponentType group_id)
		{
			groups.emplace_yield(ComponentIDMapping::get_id<U>())->insert(group_id);
			GroupRegisters<Us...>::register_group(groups, group_id);
		}
	};

	template <typename U>
	struct GroupRegisters<U>
	{
		static void register_group(ComponentGroupHashMap &groups,
		                           ComponentType group_id)
		{
			groups.emplace_yield(ComponentIDMapping::get_id<U>())->insert(group_id);
		}
	};

	template <typename U, typename... Us>
	void register_group(ComponentType group_id)
	{
		GroupRegisters<U, Us...>::register_group(component_to_groups, group_id);
	}

	void free_groups();
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
	auto *t = components.find(id);
	if (t)
	{
		components.erase(t);
		pool->free_component(*this, t->get_hash(), t);
	}
}

}