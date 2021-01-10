/* Copyright (c) 2017-2020 Hans-Kristian Arntzen
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
#include "vulkan_headers.hpp"

namespace Vulkan
{
class Device;

class DeviceCreatedEvent : public Granite::Event
{
public:
	GRANITE_EVENT_TYPE_DECL(DeviceCreatedEvent)

	explicit DeviceCreatedEvent(Device *device_)
		: device(*device_)
	{}

	Device &get_device() const
	{
		return device;
	}

private:
	Device &device;
};

class DisplayTimingStutterEvent : public Granite::Event
{
public:
	GRANITE_EVENT_TYPE_DECL(WSIStutterEvent)

	DisplayTimingStutterEvent(uint32_t current_serial_, uint32_t observed_serial_,
	                          unsigned dropped_frames_)
		: current_serial(current_serial_),
		  observed_serial(observed_serial_),
		  dropped_frames(dropped_frames_)
	{
	}

	uint32_t get_current_serial() const
	{
		return current_serial;
	}

	uint32_t get_observed_serial() const
	{
		return observed_serial;
	}

	unsigned get_dropped_frames() const
	{
		return dropped_frames;
	}

private:
	uint32_t current_serial;
	uint32_t observed_serial;
	unsigned dropped_frames;
};

class SwapchainParameterEvent : public Granite::Event
{
public:
	GRANITE_EVENT_TYPE_DECL(SwapchainParameterEvent)

	SwapchainParameterEvent(Device *device_,
	                        unsigned width_, unsigned height_,
	                        float aspect_ratio_, unsigned count_,
	                        VkFormat format_, VkSurfaceTransformFlagBitsKHR transform_)
		: device(*device_), width(width_), height(height_),
		  aspect_ratio(aspect_ratio_), image_count(count_), format(format_), transform(transform_)
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

	VkSurfaceTransformFlagBitsKHR get_prerotate() const
	{
		return transform;
	}

private:
	Device &device;
	unsigned width;
	unsigned height;
	float aspect_ratio;
	unsigned image_count;
	VkFormat format;
	VkSurfaceTransformFlagBitsKHR transform;
};


class SwapchainIndexEvent : public Granite::Event
{
public:
	GRANITE_EVENT_TYPE_DECL(SwapchainIndexEvent)

	SwapchainIndexEvent(Device *device_, unsigned index_)
		: device(*device_), index(index_)
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
