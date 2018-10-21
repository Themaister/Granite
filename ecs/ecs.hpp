/* Copyright (c) 2017-2018 Hans-Kristian Arntzen
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
struct ComponentBase : Util::IntrusiveHashMapEnabled<ComponentBase>
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

using ComponentHashMap = Util::IntrusiveHashMapHolder<ComponentBase>;
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
	friend class EntityPool;

	Entity(EntityPool *pool)
		: pool(pool)
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
		return static_cast<T *>(t);
	}

	template <typename T>
	const T *get_component() const
	{
		auto *t = components.find(ComponentIDMapping::get_id<T>());
		return static_cast<T *>(t);
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

private:
	EntityPool *pool;
	ComponentHashMap components;
};

template <typename... Ts>
class EntityGroup : public EntityGroupBase
{
public:
	void add_entity(Entity &entity) override final
	{
		if (has_all_components<Ts...>(entity))
			groups.push_back(std::make_tuple(entity.get_component<Ts>()...));
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
			std::swap(groups[offset], groups.back());
		groups.pop_back();
	}

	std::vector<std::tuple<Ts *...>> &get_groups()
	{
		return groups;
	}

private:
	std::vector<std::tuple<Ts *...>> groups;

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

using EntityHandle = Util::IntrusivePtr<Entity>;

class EntityPool
{
public:
	~EntityPool();

	EntityPool() = default;
	void operator=(const EntityPool &) = delete;
	EntityPool(const EntityPool &) = delete;

	EntityHandle create_entity()
	{
		auto itr = EntityHandle(entity_pool.allocate(this));
		entities.push_back(itr.get());
		return itr;
	}

	void delete_entity(Entity *entity)
	{
		{
			auto &components = entity->get_components();
			auto &list = components.inner_list();
			auto itr = list.begin();
			while (itr != list.end())
			{
				auto *component = itr.get();
				itr = list.erase(itr);
				free_component(component->get_hash(), component);
			}
		}
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

		auto *group = static_cast<EntityGroup<Ts...> *>(t);
		return group->get_groups();
	}

	template <typename T, typename... Ts>
	T *allocate_component(Entity &entity, Ts&&... ts)
	{
		ComponentType id = ComponentIDMapping::get_id<T>();
		auto *t = components.find(id);
		if (!t)
		{
			t = new ComponentAllocator<T>();
			t->set_hash(id);
			components.insert_yield(t);
		}

		auto *allocator = static_cast<ComponentAllocator<T> *>(t);
		auto *comp = allocator->pool.allocate(std::forward<Ts>(ts)...);
		comp->set_hash(id);
		auto *to_delete = entity.components.insert_replace(comp);
		if (to_delete)
			allocator->free_component(to_delete);

		auto *component_groups = component_to_groups.find(id);

		if (to_delete && component_groups)
			for (auto &group : *component_groups)
				groups.find(group.get_hash())->remove_component(to_delete);

		if (component_groups)
			for (auto &group : *component_groups)
				groups.find(group.get_hash())->add_entity(entity);

		return static_cast<T *>(comp);
	}

	void free_component(ComponentType id, ComponentBase *component)
	{
		auto *c = components.find(id);
		if (c)
			c->free_component(component);

		auto *component_groups = component_to_groups.find(id);
		if (component_groups)
		{
			for (auto &group : *component_groups)
			{
				auto *g = groups.find(group.get_hash());
				if (g)
					g->remove_component(component);
			}
		}
	}

	void reset_groups();

private:
	Util::ObjectPool<Entity> entity_pool;
	Util::IntrusiveHashMapHolder<EntityGroupBase> groups;
	Util::IntrusiveHashMapHolder<ComponentAllocatorBase> components;
	ComponentGroupHashMap component_to_groups;
	std::vector<Entity *> entities;

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
		pool->free_component(t->get_hash(), t);
	}
}

}