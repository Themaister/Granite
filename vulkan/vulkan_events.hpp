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

#include "event.hpp"
#include "vulkan.hpp"

namespace Vulkan
{
class Device;


class DeviceCreatedEvent : public Granite::Event
{
public:
	GRANITE_EVENT_TYPE_DECL(DeviceCreatedEvent)

	DeviceCreatedEvent(Device *device)
		: device(*device)
	{}

	Device &get_device() const
	{
		return device;
	}

private:
	Device &device;
};

class SwapchainParameterEvent : public Granite::Event
{
public:
	GRANITE_EVENT_TYPE_DECL(SwapchainParameterEvent)

	SwapchainParameterEvent(Device *device, unsigned width, unsigned height, float aspect_ratio, unsigned count, VkFormat format)
		: device(*device), width(width), height(height), aspect_ratio(aspect_ratio), image_count(count), format(format)
	{}

	Device &get_device() const
	{
		return device;
	}

	unsigned get_width() const
	{
		return width;
	}

	unsigned get_height() const
	{
		return height;
	}

	float get_aspect_ratio() const
	{
		return aspect_ratio;
	}

	unsigned get_image_count() const
	{
		return image_count;
	}

	VkFormat get_format() const
	{
		return format;
	}

private:
	Device &device;
	unsigned width;
	unsigned height;
	float aspect_ratio;
	unsigned image_count;
	VkFormat format;
};


class SwapchainIndexEvent : public Granite::Event
{
public:
	GRANITE_EVENT_TYPE_DECL(SwapchainIndexEvent)

	SwapchainIndexEvent(Device *device, unsigned index)
		: device(*device), index(index)
	{}

	Device &get_device() const
	{
		return device;
	}

	unsigned get_index() const
	{
		return index;
	}

private:
	Device &device;
	unsigned index;
};
}