#pragma once

#include <vector>
#include <memory>

namespace Granite
{
enum class EventType : unsigned
{
	AEvent,
	BEvent,

	Count
};

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

class EventHandler
{
public:
	virtual ~EventHandler() = default;
	virtual void handle(const Event &event) = 0;
};

class EventManager
{
public:
	template<typename T, typename... P>
	void enqueue(P&&... p)
	{
		EventType type = T::type_id;
		auto &l = events[static_cast<unsigned>(type)];

		auto ptr = std::unique_ptr<Event>(new T(std::forward<P>(p)...));
		ptr->set_type(type);
		l.emplace_back(std::move(ptr));
	}

	void dispatch();
	void register_handler(EventType type, EventHandler &handler);

private:
	std::vector<std::unique_ptr<Event>> events[static_cast<unsigned>(EventType::Count)];
	std::vector<EventHandler *> handlers[static_cast<unsigned>(EventType::Count)];
};

class AEvent : public Event
{
public:
	static constexpr EventType type_id = EventType::AEvent;

	AEvent(int a) : a(a) {}
	int a;
};

class BEvent : public Event
{
public:
	static constexpr EventType type_id = EventType::BEvent;

	BEvent(int a, int b) : a(a), b(b) {}
	int a, b;
};
}