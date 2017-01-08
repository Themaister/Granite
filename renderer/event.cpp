#include "event.hpp"

namespace Granite
{
void EventManager::dispatch()
{
	for (unsigned i = 0; i < static_cast<unsigned>(EventType::Count); i++)
	{
		for (auto &handler : handlers[i])
			for (auto &event : events[i])
				handler->handle(*event);

		events[i].clear();
	}
}

void EventManager::register_handler(EventType type, EventHandler &handler)
{
	handlers[static_cast<unsigned>(type)].push_back(&handler);
}
}
