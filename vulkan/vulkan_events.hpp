#pragma once

#include "event.hpp"
#include "vulkan.hpp"

namespace Vulkan
{
class Device;


class DeviceCreatedEvent : public Granite::Event
{
public:
	static constexpr Granite::EventType type_id = GRANITE_EVENT_TYPE_HASH(DeviceCreatedEvent);

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
	static constexpr Granite::EventType type_id = GRANITE_EVENT_TYPE_HASH(SwapchainParameterEvent);

	SwapchainParameterEvent(Device *device, unsigned width, unsigned height, unsigned count, VkFormat format)
		: device(*device), width(width), height(height), image_count(count), format(format)
	{}

	unsigned get_width() const
	{
		return width;
	}

	unsigned get_height() const
	{
		return height;
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
	unsigned image_count;
	VkFormat format;
};

class SwapchainIndexEvent : public Granite::Event
{
public:
	static constexpr Granite::EventType type_id = GRANITE_EVENT_TYPE_HASH(SwapchainIndexEvent);

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