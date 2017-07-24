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