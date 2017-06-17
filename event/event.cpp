#include "event.hpp"
#include <algorithm>
#include <assert.h>

using namespace std;

namespace Granite
{

EventManager::~EventManager()
{
	dispatch();
	for (auto &event_type : latched_events)
		for (auto &handler : event_type.second.handlers)
			dispatch_down_events(event_type.second.queued_events, handler);
}

void EventManager::dispatch()
{
	for (auto &event_type : events)
	{
		auto &handlers = event_type.second.handlers;
		auto &queued_events = event_type.second.queued_events;
		auto itr = remove_if(begin(handlers), end(handlers), [&](const Handler &handler) {
			for (auto &event : queued_events)
				if (!(handler.handler->*(handler.mem_fn))(*event))
					return true;
			return false;
		});

		handlers.erase(itr, end(handlers));
		queued_events.clear();
	}
}

void EventManager::dispatch_event(std::vector<Handler> &handlers, const Event &e)
{
	auto itr = remove_if(begin(handlers), end(handlers), [&](const Handler &handler) {
		bool keep_event = (handler.handler->*(handler.mem_fn))(e);
		return !keep_event;
	});

	handlers.erase(itr, end(handlers));
}

void EventManager::dispatch_up_events(std::vector<std::unique_ptr<Event>> &events, const LatchHandler &handler)
{
	for (auto &event : events)
		(handler.handler->*(handler.up_fn))(*event);
}

void EventManager::dispatch_down_events(std::vector<std::unique_ptr<Event>> &events, const LatchHandler &handler)
{
	for (auto &event : events)
		(handler.handler->*(handler.down_fn))(*event);
}

void EventManager::LatchEventTypeData::flush_recursive_handlers()
{
	handlers.insert(end(handlers), begin(recursive_handlers), end(recursive_handlers));
	recursive_handlers.clear();
}

void EventManager::EventTypeData::flush_recursive_handlers()
{
	handlers.insert(end(handlers), begin(recursive_handlers), end(recursive_handlers));
	recursive_handlers.clear();
}

void EventManager::dispatch_up_event(LatchEventTypeData &event_type, const Event &event)
{
	event_type.dispatching = true;
	for (auto &handler : event_type.handlers)
		(handler.handler->*(handler.up_fn))(event);
	event_type.flush_recursive_handlers();
	event_type.dispatching = false;
}

void EventManager::dispatch_down_event(LatchEventTypeData &event_type, const Event &event)
{
	event_type.dispatching = true;
	for (auto &handler : event_type.handlers)
		(handler.handler->*(handler.down_fn))(event);
	event_type.flush_recursive_handlers();
	event_type.dispatching = false;
}

void EventManager::unregister_handler(EventHandler *handler)
{
	for (auto &event_type : events)
	{
		auto itr = remove_if(begin(event_type.second.handlers), end(event_type.second.handlers), [&](const Handler &h) {
			return h.handler == handler;
		});

		if (itr != end(event_type.second.handlers) && event_type.second.dispatching)
			throw logic_error("Unregistering handlers while dispatching events.");

		if (itr != end(event_type.second.handlers))
			event_type.second.handlers.erase(itr, end(event_type.second.handlers));
	}
}

void EventManager::unregister_handler(const Handler &handler)
{
	for (auto &event_type : events)
	{
		auto itr = remove_if(begin(event_type.second.handlers), end(event_type.second.handlers), [&](const Handler &h) {
			return h.handler == handler.handler && h.mem_fn == handler.mem_fn;
		});

		if (itr != end(event_type.second.handlers) && event_type.second.dispatching)
			throw logic_error("Unregistering handlers while dispatching events.");

		if (itr != end(event_type.second.handlers))
			event_type.second.handlers.erase(itr, end(event_type.second.handlers));
	}
}

void EventManager::unregister_latch_handler(EventHandler *handler)
{
	for (auto &event_type : latched_events)
	{
		auto itr = remove_if(begin(event_type.second.handlers), end(event_type.second.handlers), [&](const LatchHandler &h) {
			return h.handler == handler;
		});

		if (itr != end(event_type.second.handlers))
			event_type.second.handlers.erase(itr, end(event_type.second.handlers));
	}
}

void EventManager::unregister_latch_handler(const LatchHandler &handler)
{
	for (auto &event_type : latched_events)
	{
		auto itr = remove_if(begin(event_type.second.handlers), end(event_type.second.handlers), [&](const LatchHandler &h) {
			bool signal = h.handler == handler.handler && h.up_fn == handler.up_fn && h.down_fn == handler.down_fn;
			if (signal)
				dispatch_down_events(event_type.second.queued_events, h);
			return signal;
		});

		if (itr != end(event_type.second.handlers) && event_type.second.dispatching)
			throw logic_error("Unregistering handlers while dispatching events.");

		if (itr != end(event_type.second.handlers))
			event_type.second.handlers.erase(itr, end(event_type.second.handlers));
	}
}

void EventManager::dequeue_latched(uint64_t cookie)
{
	for (auto &event_type : latched_events)
	{
		auto &events = event_type.second.queued_events;
		if (event_type.second.enqueueing)
			throw logic_error("Dequeueing latched while queueing events.");
		event_type.second.enqueueing = true;

		auto itr = remove_if(begin(events), end(events), [&](const unique_ptr<Event> &event) {
			bool signal = event->get_cookie() == cookie;
			if (signal)
				dispatch_down_event(event_type.second, *event);
			return signal;
		});

		event_type.second.enqueueing = false;
		events.erase(itr, end(events));
	}
}

void EventManager::dequeue_all_latched(EventType type)
{
	auto &event_type = latched_events[type];
	if (event_type.enqueueing)
		throw logic_error("Dequeueing latched while queueing events.");

	event_type.enqueueing = true;
	for (auto &event : event_type.queued_events)
		dispatch_down_event(event_type, *event);
	event_type.queued_events.clear();
	event_type.enqueueing = false;
}

EventHandler::~EventHandler()
{
	EventManager::get_global().unregister_handler(this);
	EventManager::get_global().unregister_latch_handler(this);
}
}
