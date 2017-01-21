#pragma once

#include <vector>
#include <memory>
#include <unordered_map>

namespace Granite
{

namespace Detail
{
constexpr uint64_t fnv_iterate(uint64_t hash, char c)
{
	return (hash * 0x100000001b3ull) ^ uint8_t(c);
}

template<size_t index>
constexpr uint64_t compile_time_fnv1_inner(uint64_t hash, const char *str)
{
	return compile_time_fnv1_inner<index - 1>(fnv_iterate(hash, str[index]), str);
}

template<>
constexpr uint64_t compile_time_fnv1_inner<size_t(-1)>(uint64_t hash, const char *)
{
	return hash;
}

template<size_t len>
constexpr uint64_t compile_time_fnv1(const char (&str)[len])
{
	return compile_time_fnv1_inner<len - 2>(0xcbf29ce484222325ull, str);
}
}

#define GRANITE_EVENT_TYPE_HASH(x) Detail::compile_time_fnv1(#x)
using EventType = uint64_t;

class Event
{
public:
	virtual ~Event() = default;

	template<typename T>
	T &as()
	{
		if (id != T::type_id)
			throw std::bad_cast();

		return static_cast<T&>(*this);
	}

	template<typename T>
	const T &as() const
	{
		if (id != T::type_id)
			throw std::bad_cast();

		return static_cast<const T&>(*this);
	}

	void set_type(EventType type_id)
	{
		id = type_id;
	}

private:
	EventType id;
};

struct EventHandler
{
};

class EventManager
{
public:
	template<typename T, typename... P>
	void enqueue(P&&... p)
	{
		EventType type = T::type_id;
		auto &l = events[type];

		auto ptr = std::unique_ptr<Event>(new T(std::forward<P>(p)...));
		ptr->set_type(type);
		l.queued_events.emplace_back(std::move(ptr));
	}

	void dispatch();

	template<typename T>
	void register_handler(EventType type, bool (T::*mem_fn)(const Event &event), T *handler)
	{
		events[type].handlers.push_back({ static_cast<bool (EventHandler::*)(const Event &event)>(mem_fn), static_cast<EventHandler *>(handler) });
	}

private:
	struct EventTypeData
	{
		std::vector<std::unique_ptr<Event>> queued_events;

		struct Handler
		{
			bool (EventHandler::*mem_fn)(const Event &event);
			EventHandler *handler;
		};
		std::vector<Handler> handlers;
	};

	struct EventHasher
	{
		size_t operator()(EventType hash) const
		{
			return static_cast<size_t>(hash);
		}
	};
	std::unordered_map<EventType, EventTypeData, EventHasher> events;
};

class AEvent : public Event
{
public:
	static constexpr EventType type_id = GRANITE_EVENT_TYPE_HASH("AEvent");

	AEvent(int a) : a(a) {}
	int a;
};

class BEvent : public Event
{
public:
	static constexpr EventType type_id = GRANITE_EVENT_TYPE_HASH("BEvent");

	BEvent(int a, int b) : a(a), b(b) {}
	int a, b;
};
}