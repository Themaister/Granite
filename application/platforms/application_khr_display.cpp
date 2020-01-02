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

#ifdef HAVE_LINUX_INPUT
#include "input_linux.hpp"
#endif

#include "application.hpp"
#include "application_events.hpp"
#include "application_wsi.hpp"
#include "vulkan_headers.hpp"
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

struct WSIPlatformDisplay : Granite::GraniteWSIPlatform
{
public:
	bool init(unsigned width_, unsigned height_)
	{
		width = width_;
		height = height_;

		if (!Context::init_loader(nullptr))
		{
			LOGE("Failed to initialize Vulkan loader.\n");
			return false;
		}

		auto *em = Global::event_manager();
		if (em)
		{
			em->dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
			em->enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Stopped);
			em->dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
			em->enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Paused);
			em->dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
			em->enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Running);
		}

		global_display = this;
		struct sigaction sa;
		memset(&sa, 0, sizeof(sa));
		sigemptyset(&sa.sa_mask);
		sa.sa_handler = signal_handler;
		sa.sa_flags = SA_RESTART | SA_RESETHAND;
		sigaction(SIGINT, &sa, nullptr);
		sigaction(SIGTERM, &sa, nullptr);

#ifdef HAVE_LINUX_INPUT
		if (!input_manager.init(
				LINUX_INPUT_MANAGER_JOYPAD_BIT |
				LINUX_INPUT_MANAGER_KEYBOARD_BIT |
				LINUX_INPUT_MANAGER_MOUSE_BIT |
				LINUX_INPUT_MANAGER_TOUCHPAD_BIT,
				&get_input_tracker()))
		{
			LOGI("Failed to initialize input manager.\n");
		}
#endif
		return true;
	}

	~WSIPlatformDisplay()
	{
		auto *em = Global::event_manager();
		if (em)
		{
			em->dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
			em->enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Paused);
			em->dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
			em->enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Stopped);
		}
	}

	bool alive(Vulkan::WSI &) override
	{
		return is_alive;
	}

	void poll_input() override
	{
#ifdef HAVE_LINUX_INPUT
		input_manager.poll();
#endif
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

		get_input_tracker().set_relative_mouse_rect(0.0, 0.0, double(this->width), double(this->height));
		get_input_tracker().mouse_enter(0.5 * this->width, 0.5 * this->height);
		get_input_tracker().set_relative_mouse_speed(0.35, 0.35);
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

	void notify_resize(unsigned width_, unsigned height_)
	{
		resize = true;
		width = width_;
		height = height_;
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

#ifdef HAVE_LINUX_INPUT
	LinuxInputManager input_manager;
#endif
};

static void signal_handler(int)
{
	LOGI("SIGINT or SIGTERM received.\n");
	global_display->signal_die();
}
}

namespace Granite
{
int application_main(Application *(*create_application)(int, char **), int argc, char *argv[])
{
	Global::init();
	auto app = unique_ptr<Granite::Application>(create_application(argc, argv));
	if (app)
	{
		auto platform = make_unique<Granite::WSIPlatformDisplay>();
		if (!platform->init(1280, 720))
			return 1;
		if (!app->init_wsi(move(platform)))
			return 1;

		Granite::Global::start_audio_system();
		while (app->poll())
			app->run_frame();
		Granite::Global::stop_audio_system();
		app.reset();
		Granite::Global::deinit();
		return 0;
	}
	else
		return 1;
}
}
