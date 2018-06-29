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

#include "application.hpp"
#include "application_events.hpp"
#include "vulkan.hpp"
#include <string.h>
#include <signal.h>

using namespace std;
using namespace Vulkan;

namespace Granite
{
struct WSIPlatformDisplay;
static WSIPlatformDisplay *global_display;
static void signal_handler(int);

static bool vulkan_update_display_mode(unsigned *width, unsigned *height, const VkDisplayModePropertiesKHR *mode,
                                       unsigned desired_width, unsigned desired_height)
{
	unsigned visible_width = mode->parameters.visibleRegion.width;
	unsigned visible_height = mode->parameters.visibleRegion.height;

	if (!desired_width || !desired_height)
	{
		/* Strategy here is to pick something which is largest resolution. */
		unsigned area = visible_width * visible_height;
		if (area > (*width) * (*height))
		{
			*width = visible_width;
			*height = visible_height;
			return true;
		}
		else
			return false;
	}
	else
	{
		/* For particular resolutions, find the closest. */
		int delta_x = int(desired_width) - int(visible_width);
		int delta_y = int(desired_height) - int(visible_height);
		int old_delta_x = int(desired_width) - int(*width);
		int old_delta_y = int(desired_height) - int(*height);

		int dist = delta_x * delta_x + delta_y * delta_y;
		int old_dist = old_delta_x * old_delta_x + old_delta_y * old_delta_y;
		if (dist < old_dist)
		{
			*width = visible_width;
			*height = visible_height;
			return true;
		}
		else
			return false;
	}
}

struct WSIPlatformDisplay : Vulkan::WSIPlatform
{
public:
	WSIPlatformDisplay(unsigned width, unsigned height)
		: width(width), height(height)
	{
		if (!Context::init_loader(nullptr))
			throw runtime_error("Failed to initialize Vulkan loader.");

		EventManager::get_global().dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
		EventManager::get_global().enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Stopped);
		EventManager::get_global().dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
		EventManager::get_global().enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Paused);
		EventManager::get_global().dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
		EventManager::get_global().enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Running);

		global_display = this;
		struct sigaction sa;
		memset(&sa, 0, sizeof(sa));
		sigemptyset(&sa.sa_mask);
		sa.sa_handler = signal_handler;
		sigaction(SIGINT, &sa, nullptr);
		sigaction(SIGTERM, &sa, nullptr);
	}

	~WSIPlatformDisplay()
	{
		EventManager::get_global().dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
		EventManager::get_global().enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Paused);
		EventManager::get_global().dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
		EventManager::get_global().enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Stopped);
	}

	bool alive(Vulkan::WSI &) override
	{
		return is_alive;
	}

	void poll_input() override
	{
		get_input_tracker().dispatch_current_state(get_frame_timer().get_frame_time());
	}

	vector<const char *> get_instance_extensions() override
	{
#ifdef KHR_DISPLAY_ACQUIRE_XLIB
		return { "VK_KHR_surface", "VK_KHR_display", "VK_EXT_acquire_xlib_display" };
#else
		return { "VK_KHR_surface", "VK_KHR_display" };
#endif
	}

	VkSurfaceKHR create_surface(VkInstance instance, VkPhysicalDevice gpu) override
	{
		VkSurfaceKHR surface = VK_NULL_HANDLE;

		uint32_t display_count;
		vkGetPhysicalDeviceDisplayPropertiesKHR(gpu, &display_count, nullptr);
		vector<VkDisplayPropertiesKHR> displays(display_count);
		vkGetPhysicalDeviceDisplayPropertiesKHR(gpu, &display_count, displays.data());

		uint32_t plane_count;
		vkGetPhysicalDeviceDisplayPlanePropertiesKHR(gpu, &plane_count, nullptr);
		vector<VkDisplayPlanePropertiesKHR> planes(plane_count);
		vkGetPhysicalDeviceDisplayPlanePropertiesKHR(gpu, &plane_count, planes.data());

#ifdef KHR_DISPLAY_ACQUIRE_XLIB
		VkDisplayKHR best_display = VK_NULL_HANDLE;
#endif
		VkDisplayModeKHR best_mode = VK_NULL_HANDLE;
		uint32_t best_plane = UINT32_MAX;

		const char *desired_display = getenv("GRANITE_DISPLAY_NAME");

		unsigned actual_width = 0;
		unsigned actual_height = 0;
		VkDisplayPlaneAlphaFlagBitsKHR alpha_mode = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;

		for (unsigned dpy = 0; dpy < display_count; dpy++)
		{
			VkDisplayKHR display = displays[dpy].display;
			best_mode = VK_NULL_HANDLE;
			best_plane = UINT32_MAX;

			if (desired_display && strstr(displays[dpy].displayName, desired_display) != displays[dpy].displayName)
				continue;

			uint32_t mode_count;
			vkGetDisplayModePropertiesKHR(gpu, display, &mode_count, nullptr);
			vector<VkDisplayModePropertiesKHR> modes(mode_count);
			vkGetDisplayModePropertiesKHR(gpu, display, &mode_count, modes.data());

			for (unsigned i = 0; i < mode_count; i++)
			{
				const VkDisplayModePropertiesKHR &mode = modes[i];
				if (vulkan_update_display_mode(&actual_width, &actual_height, &mode, 0, 0))
					best_mode = mode.displayMode;
			}

			if (best_mode == VK_NULL_HANDLE)
				continue;

			for (unsigned i = 0; i < plane_count; i++)
			{
				uint32_t supported_count = 0;
				VkDisplayPlaneCapabilitiesKHR plane_caps;
				vkGetDisplayPlaneSupportedDisplaysKHR(gpu, i, &supported_count, nullptr);

				if (!supported_count)
					continue;

				vector<VkDisplayKHR> supported(supported_count);
				vkGetDisplayPlaneSupportedDisplaysKHR(gpu, i, &supported_count, supported.data());

				unsigned j;
				for (j = 0; j < supported_count; j++)
				{
					if (supported[j] == display)
					{
						if (best_plane == UINT32_MAX)
							best_plane = j;
						break;
					}
				}

				if (j == supported_count)
					continue;

				if (planes[i].currentDisplay == VK_NULL_HANDLE || planes[i].currentDisplay == display)
					best_plane = j;
				else
					continue;

				vkGetDisplayPlaneCapabilitiesKHR(gpu, best_mode, i, &plane_caps);

				if (plane_caps.supportedAlpha & VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR)
				{
					best_plane = j;
					alpha_mode = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
#ifdef KHR_DISPLAY_ACQUIRE_XLIB
					best_display = display;
#endif
					goto out;
				}
			}
		}
out:

		if (best_mode == VK_NULL_HANDLE)
			return VK_NULL_HANDLE;
		if (best_plane == UINT32_MAX)
			return VK_NULL_HANDLE;

		VkDisplaySurfaceCreateInfoKHR create_info = { VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR };
		create_info.displayMode = best_mode;
		create_info.planeIndex = best_plane;
		create_info.planeStackIndex = planes[best_plane].currentStackIndex;
		create_info.transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
		create_info.globalAlpha = 1.0f;
		create_info.alphaMode = alpha_mode;
		create_info.imageExtent.width = actual_width;
		create_info.imageExtent.height = actual_height;
		this->width = actual_width;
		this->height = actual_height;

#ifdef KHR_DISPLAY_ACQUIRE_XLIB
		dpy = XOpenDisplay(nullptr);
		if (dpy)
		{
			if (vkAcquireXlibDisplayEXT(gpu, dpy, best_display) != VK_SUCCESS)
				LOGE("Failed to acquire Xlib display. Surface creation may fail.\n");
		}
#endif

		if (vkCreateDisplayPlaneSurfaceKHR(instance, &create_info, NULL, &surface) != VK_SUCCESS)
			return VK_NULL_HANDLE;
		return surface;
	}

	uint32_t get_surface_width() override
	{
		return width;
	}

	uint32_t get_surface_height() override
	{
		return height;
	}

	void notify_resize(unsigned width, unsigned height)
	{
		resize = true;
		this->width = width;
		this->height = height;
	}

	void signal_die()
	{
		is_alive = false;
	}

private:
	unsigned width = 0;
	unsigned height = 0;
#ifdef KHR_DISPLAY_ACQUIRE_XLIB
	Display *dpy = nullptr;
#endif
	bool is_alive = true;
};

static void signal_handler(int)
{
	LOGI("SIGINT or SIGTERM received.\n");
	global_display->signal_die();
}

void application_dummy()
{
}
}

int main(int argc, char *argv[])
{
	auto app = unique_ptr<Granite::Application>(Granite::application_create(argc, argv));
	if (app)
	{
		if (!app->init_wsi(make_unique<Granite::WSIPlatformDisplay>(1280, 720)))
			return 1;

		while (app->poll())
			app->run_frame();
		return 0;
	}
	else
		return 1;
}
