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

#include <libudev.h>
#include <linux/input.h>
#include <functional>
#include <vector>
#include <memory>
#include "input.hpp"

namespace Granite
{
class LinuxInputManager
{
public:
	LinuxInputManager() = default;
	bool init();
	~LinuxInputManager();
	bool poll(InputTracker &tracker);

	LinuxInputManager(LinuxInputManager &&) = delete;
	void operator=(const LinuxInputManager &) = delete;

private:
	struct udev *udev = nullptr;
	struct udev_monitor *udev_monitor = nullptr;
	int queue_fd = -1;

	enum class DeviceType
	{
		Keyboard,
		Mouse,
		Touchpad,
		Joystick
	};

#if 0
	struct MouseState
	{
		int32_t x_abs, y_abs;
		int32_t x_min, y_min;
		int32_t x_max, y_max;
		int32_t x_rel, y_rel;
		bool l, r, m, b4, b5;
		bool wu, wd, whu, whd;
	};
#endif

	struct Device;
	using InputCallback = void (LinuxInputManager::*)(InputTracker &, Device &, const input_event &e);
	struct Device
	{
		int fd;
		DeviceType type;
		InputCallback callback;
		std::string devnode;
#if 0
		MouseState mouse;
#endif
	};
	std::vector<std::unique_ptr<Device>> devices;

	bool open_devices(DeviceType type, InputCallback callback);
	bool add_device(int fd, DeviceType type, const char *devnode, InputCallback callback);
	static const char *get_device_type_string(DeviceType type);

	void input_handle_keyboard(InputTracker &tracker, Device &dev, const input_event &e);
	void input_handle_mouse(InputTracker &tracker, Device &dev, const input_event &e);
	void input_handle_touchpad(InputTracker &tracker, Device &dev, const input_event &e);
	void input_handle_joystick(InputTracker &tracker, Device &dev, const input_event &e);
	void remove_device(const char *devnode);
	bool hotplug_available();
	void handle_hotplug();

	void init_key_table();
	Key keyboard_to_key[KEY_MAX];
};
}