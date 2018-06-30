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

#include "input_linux.hpp"
#include "unstable_remove_if.hpp"
#include "util.hpp"
#include <stdexcept>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <string.h>

using namespace std;

namespace Granite
{

const char *LinuxInputManager::get_device_type_string(DeviceType type)
{
	switch (type)
	{
	case DeviceType::Keyboard:
		return "ID_INPUT_KEYBOARD";
	case DeviceType::Mouse:
		return "ID_INPUT_MOUSE";
	case DeviceType::Touchpad:
		return "ID_INPUT_TOUCHPAD";
	case DeviceType::Joystick:
		return "ID_INPUT_JOYSTICK";
	}
	return nullptr;
}

bool LinuxInputManager::add_device(int fd, DeviceType type, const char *devnode, InputCallback callback)
{
	struct stat s;
	if (fstat(fd, &s) < 0)
	{
		close(fd);
		return false;
	}

	input_absinfo absinfo;
	epoll_event event;
	auto dev = make_unique<Device>();
	dev->type = type;
	dev->fd = fd;
	dev->callback = callback;
	dev->devnode = devnode;

	switch (type)
	{
	case DeviceType::Touchpad:
		if (ioctl(fd, EVIOCGABS(ABS_X), &absinfo) < 0)
			return false;
		dev->mouse.x_min = absinfo.minimum;
		dev->mouse.x_max = absinfo.maximum;

		if (ioctl(fd, EVIOCGABS(ABS_Y), &absinfo) < 0)
			return false;
		dev->mouse.y_min = absinfo.minimum;
		dev->mouse.y_max = absinfo.maximum;
		break;

	case DeviceType::Mouse:
		if (ioctl(fd, EVIOCGABS(ABS_X), &absinfo) >= 0)
		{
			if (absinfo.minimum >= absinfo.maximum)
				return false;

			dev->mouse.x_min = absinfo.minimum;
			dev->mouse.x_max = absinfo.maximum;
		}

		if (ioctl(fd, EVIOCGABS(ABS_Y), &absinfo) >= 0)
		{
			if (absinfo.minimum >= absinfo.maximum)
				return false;

			dev->mouse.y_min = absinfo.minimum;
			dev->mouse.y_max = absinfo.maximum;
		}
		break;

	default:
		break;
	}

	event.data.ptr = dev.get();
	event.events = EPOLLIN;

	if (epoll_ctl(queue_fd, EPOLL_CTL_ADD, fd, &event) < 0)
	{
		LOGE("Failed to add FD to epoll.\n");
		return false;
	}

	devices.push_back(move(dev));

	return true;
}

bool LinuxInputManager::hotplug_available()
{
	struct pollfd fds = {};
	fds.fd = udev_monitor_get_fd(udev_monitor);
	fds.events = POLLIN;

	return (::poll(&fds, 1, 0) == 1) && (fds.revents & POLLIN);
}

void LinuxInputManager::handle_hotplug()
{
	auto *dev = udev_monitor_receive_device(udev_monitor);

	auto *val_key = udev_device_get_property_value(dev, "ID_INPUT_KEYBOARD");
	auto *val_mouse = udev_device_get_property_value(dev, "ID_INPUT_MOUSE");
	auto *val_touchpad = udev_device_get_property_value(dev, "ID_INPUT_TOUCHPAD");
	auto *val_joystick = udev_device_get_property_value(dev, "ID_INPUT_JOYSTICK");
	auto *action = udev_device_get_action(dev);
	auto *devnode = udev_device_get_devnode(dev);

	InputCallback cb = nullptr;
	DeviceType type;
	if (val_key && strcmp(val_key, "1") == 0 && devnode)
	{
		type = DeviceType::Keyboard;
		cb = &LinuxInputManager::input_handle_keyboard;
	}
	else if (val_mouse && strcmp(val_mouse, "1") == 0 && devnode)
	{
		type = DeviceType::Mouse;
		cb = &LinuxInputManager::input_handle_mouse;
	}
	else if (val_touchpad && strcmp(val_touchpad, "1") == 0 && devnode)
	{
		type = DeviceType::Touchpad;
		cb = &LinuxInputManager::input_handle_touchpad;
	}
	else if (val_joystick && strcmp(val_joystick, "1") == 0 && devnode)
	{
		type = DeviceType::Joystick;
		cb = &LinuxInputManager::input_handle_joystick;
	}
	else
		return;

	if (strcmp(action, "add") == 0)
	{
		LOGI("Hotplugging %s\n", devnode);
		int fd = open(devnode, O_RDONLY | O_NONBLOCK);
		if (fd >= 0)
		{
			if (!add_device(fd, type, devnode, cb))
			{
				LOGE("Failed to hotplug: %s\n", devnode);
				close(fd);
			}
		}
		else
			LOGE("Failed to open device: %s.\n", devnode);
	}
	else if (strcmp(action, "remove") == 0)
	{
		remove_device(devnode);
	}

	udev_device_unref(dev);
}

bool LinuxInputManager::poll(InputTracker &tracker)
{
	while (hotplug_available())
		handle_hotplug();

	int ret;
	epoll_event events[32];
	while ((ret = epoll_wait(queue_fd, events, 32, 0)) > 0)
	{
		for (int i = 0; i < ret; i++)
		{
			if (events[i].events & EPOLLIN)
			{
				struct input_event input_events[32];
				auto &device = *static_cast<Device *>(events[i].data.ptr);

				ssize_t len;
				while ((len = read(device.fd, input_events, 32)) > 0)
				{
					len /= sizeof(input_events[0]);
					for (ssize_t j = 0; j < len; j++)
						(this->*device.callback)(tracker, device, input_events[j]);
				}
			}
		}
	}

	return true;
}

void LinuxInputManager::remove_device(const char *devnode)
{
	auto itr = Util::unstable_remove_if(begin(devices), end(devices), [=](const unique_ptr<Device> &dev) {
		return dev->devnode == devnode;
	});

	for (auto del_itr = itr; del_itr != end(devices); ++del_itr)
	{
		auto &d = *del_itr;
		close(d->fd);
	}

	devices.erase(itr, end(devices));
}

bool LinuxInputManager::open_devices(DeviceType type, InputCallback callback)
{
	const char *type_str = get_device_type_string(type);
	udev_enumerate *enumerate = udev_enumerate_new(udev);
	if (!enumerate)
		return false;

	udev_enumerate_add_match_property(enumerate, type_str, "1");
	udev_enumerate_scan_devices(enumerate);
	udev_list_entry *devs = udev_enumerate_get_list_entry(enumerate);

	for (auto *item = devs; item != nullptr; item = udev_list_entry_get_next(devs))
	{
		auto *name = udev_list_entry_get_name(item);
		auto *dev = udev_device_new_from_syspath(udev, name);
		auto *devnode = udev_device_get_devnode(dev);

		if (devnode)
		{
			int fd = open(devnode, O_RDONLY | O_NONBLOCK);
			if (fd != -1)
			{
				if (!add_device(fd, type, devnode, callback))
				{
					close(fd);
					LOGE("Failed to add device: %s\n", devnode);
				}
				else
					LOGI("Found %s (%s)\n", type_str, devnode);
			}
		}

		udev_device_unref(dev);
	}

	udev_enumerate_unref(enumerate);
	return true;
}

bool LinuxInputManager::init()
{
	udev = udev_new();
	if (!udev)
	{
		LOGE("Failed to create UDEV.\n");
		return false;
	}

	udev_monitor = udev_monitor_new_from_netlink(udev, "udev");
	if (!udev_monitor)
	{
		LOGE("Failed to create UDEV monitor.\n");
		return false;
	}

	udev_monitor_filter_add_match_subsystem_devtype(udev_monitor, "input", nullptr);
	udev_monitor_enable_receiving(udev_monitor);

	queue_fd = epoll_create(32);
	if (queue_fd < 0)
	{
		LOGE("Failed to create epoll FD.\n");
		return false;
	}

	if (!open_devices(DeviceType::Keyboard, &LinuxInputManager::input_handle_keyboard))
	{
		LOGE("Failed to open keyboards.\n");
		return false;
	}

	if (!open_devices(DeviceType::Mouse, &LinuxInputManager::input_handle_mouse))
	{
		LOGE("Failed to open keyboards.\n");
		return false;
	}

	if (!open_devices(DeviceType::Touchpad, &LinuxInputManager::input_handle_touchpad))
	{
		LOGE("Failed to open keyboards.\n");
		return false;
	}

	if (!open_devices(DeviceType::Joystick, &LinuxInputManager::input_handle_joystick))
	{
		LOGE("Failed to open joysticks.\n");
		return false;
	}

	return true;
}

}