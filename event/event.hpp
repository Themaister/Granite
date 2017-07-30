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

#include <vector>
#include <memory>
#include <unordered_map>
#include <stdexcept>
#include <utility>

#define EVENT_MANAGER_REGISTER(clazz, member, event) \
	::Granite::EventManager::get_global().register_handler<clazz, &clazz::member>(event::type_id, this)
#define EVENT_MANAGER_REGISTER_LATCH(clazz, up_event, down_event, event) \
	::Granite::EventManager::get_global().register_latch_handler<clazz, &clazz::up_event, &clazz::down_event>(event::type_id, this)

namespace Granite
{
class Event;

template <typename Return, typename T, Return (T::*callback)(const Event &e)>
Return member_function_invoker(void *object, const Event &e)
{
	return (static_cast<T *>(object)->*callback)(e);
}

namespace Detail
{

#ifdef _MSC_VER
// MSVC generates bogus warnings here.
#pragma warning(disable: 4307)
#endif

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
	return compile_time_fnv1_inner<len - 1>(0xcbf29ce484222325ull, str);
}
}

#define GRANITE_EVENT_TYPE_HASH(x) ::Granite::Detail::compile_time_fnv1(#x)
using EventType = uint64_t;

class Event
{
public:
	virtual ~Event() = default;
	Event(EventType type_id)
		: id(type_id)
	{
	}
	Event() = default;

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

	void set_cookie(uint64_t cookie)
	{
		this->cookie = cookie;
	}

	uint64_t get_cookie() const
	{
		return cookie;
	}

private:
	EventType id;
	uint64_t cookie;
};

class EventHandler
{
public:
	EventHandler(const EventHandler &) = delete;
	void operator=(const EventHandler &) = delete;
	EventHandler() = default;
	~EventHandler();
};

class EventManager
{
public:
	static EventManager &get_global()
	{
		static EventManager static_manager;
		return static_manager;
	}

	template<typename T, typename... P>
	void enqueue(P&&... p)
	{
		EventType type = T::type_id;
		auto &l = events[type];

		auto ptr = std::unique_ptr<Event>(new T(std::forward<P>(p)...));
		ptr->set_type(type);
		l.queued_events.emplace_back(std::move(ptr));
	}

	template<typename T, typename... P>
	uint64_t enqueue_latched(P&&... p)
	{
		EventType type = T::type_id;
		auto &l = latched_events[type];
		auto ptr = std::unique_ptr<Event>(new T(std::forward<P>(p)...));
		uint64_t cookie = ++cookie_counter;
		ptr->set_type(type);
		ptr->set_cookie(cookie);

		if (l.enqueueing)
			throw std::logic_error("Cannot enqueue more latched events while handling events.");
		l.enqueueing = true;

		auto *event = ptr.get();
		l.queued_events.emplace_back(std::move(ptr));
		dispatch_up_event(l, *event);
		l.enqueueing = false;
		return cookie;
	}

	void dequeue_latched(uint64_t cookie);
	void dequeue_all_latched(EventType type);

	template<typename T>
	void dispatch_inline(const T &t)
	{
		EventType type = T::type_id;
		auto &l = events[type];
		dispatch_event(l.handlers, t);
	}

	void dispatch();

	template<typename T, bool (T::*mem_fn)(const Event &)>
	void register_handler(EventType type, T *handler)
	{
		auto &l = events[type];
		if (l.dispatching)
			l.recursive_handlers.push_back({ member_function_invoker<bool, T, mem_fn>, handler, handler });
		else
			l.handlers.push_back({ member_function_invoker<bool, T, mem_fn>, handler, handler });
	}

	void unregister_handler(EventHandler *handler);

#if 0
	template<typename T, bool (T::*mem_fn)(const Event &)>
	void unregister_handler(EventHandler *handler)
	{
		Handler h{ member_function_invoker<bool, T, mem_fn>, nullptr, handler };
		unregister_handler(h);
	}
#endif

	template<typename T, void (T::*up_fn)(const Event &), void (T::*down_fn)(const Event &)>
	void register_latch_handler(EventType type, T *handler)
	{
		LatchHandler h{
			member_function_invoker<void, T, up_fn>,
			member_function_invoker<void, T, down_fn>,
			handler, handler };

		auto &events = latched_events[type];
		dispatch_up_events(events.queued_events, h);

		auto &l = latched_events[type];
		if (l.dispatching)
			l.recursive_handlers.push_back(h);
		else
			l.handlers.push_back(h);
	}

	void unregister_latch_handler(EventHandler *handler);

#if 0
	template<typename T, void (T::*up_fn)(const Event &), void (T::*down_fn)(const Event &)>
	void unregister_latch_handler(EventHandler *handler)
	{
		LatchHandler h{
			member_function_invoker<void, T, up_fn>,
			member_function_invoker<void, T, down_fn>,
			nullptr, handler };

		unregister_latch_handler(h);
	}
#endif

	~EventManager();

private:
	struct Handler
	{
		bool (*mem_fn)(void *object, const Event &event);
		void *handler;
		EventHandler *unregister_key;
	};

	struct LatchHandler
	{
		void (*up_fn)(void *object, const Event &event);
		void (*down_fn)(void *object, const Event &event);
		void *handler;
		EventHandler *unregister_key;
	};

	struct EventTypeData
	{
		std::vector<std::unique_ptr<Event>> queued_events;
		std::vector<Handler> handlers;
		std::vector<Handler> recursive_handlers;
		bool enqueueing = false;
		bool dispatching = false;

		void flush_recursive_handlers();
	};

	struct LatchEventTypeData
	{
		std::vector<std::unique_ptr<Event>> queued_events;
		std::vector<LatchHandler> handlers;
		std::vector<LatchHandler> recursive_handlers;
		bool enqueueing = false;
		bool dispatching = false;

		void flush_recursive_handlers();
	};

	void dispatch_event(std::vector<Handler> &handlers, const Event &e);
	void dispatch_up_events(std::vector<std::unique_ptr<Event>> &events, const LatchHandler &handler);
	void dispatch_down_events(std::vector<std::unique_ptr<Event>> &events, const LatchHandler &handler);
	void dispatch_up_event(LatchEventTypeData &event_type, const Event &event);
	void dispatch_down_event(LatchEventTypeData &event_type, const Event &event);

#if 0
	void unregister_handler(const Handler &handler);
#endif
	void unregister_latch_handler(const LatchHandler &handler);

	struct EventHasher
	{
		size_t operator()(EventType hash) const
		{
			return static_cast<size_t>(hash);
		}
	};
	std::unordered_map<EventType, EventTypeData, EventHasher> events;
	std::unordered_map<EventType, LatchEventTypeData, EventHasher> latched_events;
	uint64_t cookie_counter = 0;
};
}
