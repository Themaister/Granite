/* Copyright (c) 2017-2024 Hans-Kristian Arntzen
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

#include "event.hpp"
#include <algorithm>
#include <assert.h>

namespace Granite
{
EventManager::~EventManager()
{
	dispatch();
	for (auto &event_type : latched_events)
	{
		for (auto &handler : event_type.handlers)
		{
			dispatch_down_events(event_type.queued_events, handler);
			// Before the event manager dies, make sure no stale EventHandler objects try to unregister themselves.
			handler.unregister_key->release_manager_reference();
		}
	}
}

void EventManager::dispatch()
{
	for (auto &event_type : events)
	{
		auto &handlers = event_type.handlers;
		auto &queued_events = event_type.queued_events;
		auto itr = remove_if(begin(handlers), end(handlers), [&](const Handler &handler) {
			for (auto &event : queued_events)
			{
				if (!handler.mem_fn(handler.handler, *event))
				{
					handler.unregister_key->release_manager_reference();
					return true;
				}
			}
			return false;
		});

		handlers.erase(itr, end(handlers));
		queued_events.clear();
	}
}

void EventManager::dispatch_event(std::vector<Handler> &handlers, const Event &e)
{
	auto itr = remove_if(begin(handlers), end(handlers), [&](const Handler &handler) -> bool {
		bool to_remove = !handler.mem_fn(handler.handler, e);
		if (to_remove)
			handler.unregister_key->release_manager_reference();
		return to_remove;
	});

	handlers.erase(itr, end(handlers));
}

void EventManager::dispatch_up_events(std::vector<std::unique_ptr<Event>> &up_events, const LatchHandler &handler)
{
	for (auto &event : up_events)
		handler.up_fn(handler.handler, *event);
}

void EventManager::dispatch_down_events(std::vector<std::unique_ptr<Event>> &down_events, const LatchHandler &handler)
{
	for (auto &event : down_events)
		handler.down_fn(handler.handler, *event);
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
		handler.up_fn(handler.handler, event);
	event_type.flush_recursive_handlers();
	event_type.dispatching = false;
}

void EventManager::dispatch_down_event(LatchEventTypeData &event_type, const Event &event)
{
	event_type.dispatching = true;
	for (auto &handler : event_type.handlers)
		handler.down_fn(handler.handler, event);
	event_type.flush_recursive_handlers();
	event_type.dispatching = false;
}

void EventManager::unregister_handler(EventHandler *handler)
{
	for (auto &event_type : events)
	{
		auto itr = remove_if(begin(event_type.handlers), end(event_type.handlers), [&](const Handler &h) -> bool {
			bool to_remove = h.unregister_key == handler;
			if (to_remove)
				h.unregister_key->release_manager_reference();
			return to_remove;
		});

		if (itr != end(event_type.handlers) && event_type.dispatching)
			throw std::logic_error("Unregistering handlers while dispatching events.");

		if (itr != end(event_type.handlers))
			event_type.handlers.erase(itr, end(event_type.handlers));
	}
}

void EventManager::unregister_latch_handler(EventHandler *handler)
{
	for (auto &event_type : latched_events)
	{
		auto itr = remove_if(begin(event_type.handlers), end(event_type.handlers), [&](const LatchHandler &h) -> bool {
			bool to_remove = h.unregister_key == handler;
			if (to_remove)
				h.unregister_key->release_manager_reference();
			return to_remove;
		});

		if (itr != end(event_type.handlers))
			event_type.handlers.erase(itr, end(event_type.handlers));
	}
}

void EventManager::dequeue_latched(uint64_t cookie)
{
	for (auto &event_type : latched_events)
	{
		auto &queued_events = event_type.queued_events;
		if (event_type.enqueueing)
			throw std::logic_error("Dequeueing latched while queueing events.");
		event_type.enqueueing = true;

		auto itr = remove_if(begin(queued_events), end(queued_events), [&](const std::unique_ptr<Event> &event) {
			bool signal = event->get_cookie() == cookie;
			if (signal)
				dispatch_down_event(event_type, *event);
			return signal;
		});

		event_type.enqueueing = false;
		queued_events.erase(itr, end(queued_events));
	}
}

void EventManager::dequeue_all_latched(EventType type)
{
	auto &event_type = latched_events[type];
	if (event_type.enqueueing)
		throw std::logic_error("Dequeueing latched while queueing events.");

	event_type.enqueueing = true;
	for (auto &event : event_type.queued_events)
		dispatch_down_event(event_type, *event);
	event_type.queued_events.clear();
	event_type.enqueueing = false;
}

void EventHandler::release_manager_reference()
{
	assert(event_manager_ref_count > 0);
	assert(event_manager);
	if (--event_manager_ref_count == 0)
		event_manager = nullptr;
}

void EventHandler::add_manager_reference(EventManager *manager)
{
	assert(!event_manager_ref_count || manager == event_manager);
	event_manager = manager;
	event_manager_ref_count++;
}

EventHandler::~EventHandler()
{
	if (event_manager)
		event_manager->unregister_handler(this);
	// Splitting the branch is significant since event manager can release its last reference in between.
	if (event_manager)
		event_manager->unregister_latch_handler(this);
	assert(event_manager_ref_count == 0 && !event_manager);
}
}
