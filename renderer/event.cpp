#include "event.hpp"
#include <algorithm>

using namespace std;

namespace Granite
{
void EventManager::dispatch()
{
	for (auto &event_type : events)
	{
		auto itr = remove_if(begin(event_type.second.handlers), end(event_type.second.handlers), [&](const EventTypeData::Handler &handler) {
			bool keep_event = true;
			for (auto &event : event_type.second.queued_events)
				keep_event = (handler.handler->*(handler.mem_fn))(*event) && keep_event;
			return !keep_event;
		});

		event_type.second.handlers.erase(itr, end(event_type.second.handlers));
		event_type.second.queued_events.clear();
	}
}
}
