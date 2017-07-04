#include "event_manager.hpp"

namespace Vulkan
{
void EventManager::init(VkDevice device)
{
	this->device = device;
}

EventManager::~EventManager()
{
	for (auto &event : events)
		vkDestroyEvent(device, event, nullptr);
}

void EventManager::recycle(VkEvent event)
{
	if (event != VK_NULL_HANDLE)
	{
		vkResetEvent(device, event);
		events.push_back(event);
	}
}

VkEvent EventManager::request_cleared_event()
{
	if (events.empty())
	{
		VkEvent event;
		VkEventCreateInfo info = { VK_STRUCTURE_TYPE_EVENT_CREATE_INFO };
		vkCreateEvent(device, &info, nullptr, &event);
		return event;
	}
	else
	{
		auto event = events.back();
		events.pop_back();
		return event;
	}
}
}