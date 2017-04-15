#pragma once

#include "event.hpp"
#include "vulkan.hpp"
#include "input.hpp"

namespace Vulkan
{
class Device;

class KeyboardEvent : public Granite::Event
{
public:
	static constexpr Granite::EventType type_id = GRANITE_EVENT_TYPE_HASH(KeyboardEvent);

	KeyboardEvent(Key key, KeyState state)
		: Granite::Event(type_id), key(key), state(state)
	{
	}

	Key get_key() const
	{
		return key;
	}

	KeyState get_key_state() const
	{
		return state;
	}

private:
	Key key;
	KeyState state;
};

class MouseButtonEvent : public Granite::Event
{
public:
	static constexpr Granite::EventType type_id = GRANITE_EVENT_TYPE_HASH(MouseButtonEvent);

	MouseButtonEvent(MouseButton button, bool pressed)
		: Granite::Event(type_id), button(button), pressed(pressed)
	{
	}

	MouseButton get_button() const
	{
		return button;
	}

	bool get_pressed() const
	{
		return pressed;
	}

private:
	MouseButton button;
	bool pressed;
};

class MouseMoveEvent : public Granite::Event
{
public:
	static constexpr Granite::EventType type_id = GRANITE_EVENT_TYPE_HASH(MouseMoveEvent);

	MouseMoveEvent(double delta_x, double delta_y, double abs_x, double abs_y,
	               uint64_t key_mask, uint8_t btn_mask)
		: Granite::Event(type_id),
		  delta_x(delta_x), delta_y(delta_y), abs_x(abs_x), abs_y(abs_y), key_mask(key_mask), btn_mask(btn_mask)
	{
	}

	bool get_mouse_button_pressed(MouseButton button) const
	{
		return (btn_mask & (1 << Util::ecast(button))) != 0;
	}

	bool get_key_pressed(Key key) const
	{
		return (key_mask & (1 << Util::ecast(key))) != 0;
	}

	double get_delta_x() const
	{
		return delta_x;
	}

	double get_delta_y() const
	{
		return delta_y;
	}

	double get_abs_x() const
	{
		return abs_x;
	}

	double get_abs_y() const
	{
		return abs_y;
	}

private:
	double delta_x, delta_y, abs_x, abs_y;
	uint64_t key_mask;
	uint8_t btn_mask;
};

class InputStateEvent : public Granite::Event
{
public:
	static constexpr Granite::EventType type_id = GRANITE_EVENT_TYPE_HASH(InputStateEvent);

	InputStateEvent(double abs_x, double abs_y, double delta_time, uint64_t key_mask, uint8_t btn_mask, bool mouse_active)
		: Granite::Event(type_id), abs_x(abs_x), abs_y(abs_y), delta_time(delta_time), key_mask(key_mask), btn_mask(btn_mask), mouse_active(mouse_active)
	{
	}

	double get_delta_time() const
	{
		return delta_time;
	}

	bool get_mouse_active() const
	{
		return mouse_active;
	}

	bool get_mouse_button_pressed(MouseButton button) const
	{
		return (btn_mask & (1 << Util::ecast(button))) != 0;
	}

	bool get_key_pressed(Key key) const
	{
		return (key_mask & (1 << Util::ecast(key))) != 0;
	}

	double get_mouse_x() const
	{
		return abs_x;
	}

	double get_mouse_y() const
	{
		return abs_y;
	}

private:
	double abs_x, abs_y;
	double delta_time;
	uint64_t key_mask;
	uint8_t btn_mask;
	bool mouse_active;
};

class FrameTickEvent : public Granite::Event
{
public:
	static constexpr Granite::EventType type_id = GRANITE_EVENT_TYPE_HASH(FrameTickEvent);

	FrameTickEvent(double frame_time, double elapsed_time)
		: Granite::Event(type_id), frame_time(frame_time), elapsed_time(elapsed_time)
	{
	}

	double get_frame_time() const
	{
		return frame_time;
	}

	double get_elapsed_time() const
	{
		return elapsed_time;
	}

private:
	double frame_time;
	double elapsed_time;
};

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