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

#include "device.hpp"
#include "semaphore_manager.hpp"
#include "vulkan.hpp"
#include "timer.hpp"
#include "wsi_timing.hpp"
#include <memory>
#include <vector>

namespace Vulkan
{
class WSI;

class WSIPlatform
{
public:
	virtual ~WSIPlatform() = default;

	virtual VkSurfaceKHR create_surface(VkInstance instance, VkPhysicalDevice gpu) = 0;
	virtual std::vector<const char *> get_instance_extensions() = 0;
	virtual std::vector<const char *> get_device_extensions()
	{
		return { "VK_KHR_swapchain" };
	}

	virtual VkFormat get_preferred_format()
	{
		return VK_FORMAT_B8G8R8A8_SRGB;
	}

	bool should_resize()
	{
		return resize;
	}

	void acknowledge_resize()
	{
		resize = false;
	}

	virtual uint32_t get_surface_width() = 0;
	virtual uint32_t get_surface_height() = 0;

	virtual float get_aspect_ratio()
	{
		return float(get_surface_width()) / float(get_surface_height());
	}

	virtual bool alive(Vulkan::WSI &wsi) = 0;
	virtual void poll_input() = 0;
	virtual bool has_external_swapchain()
	{
		return false;
	}

	Util::FrameTimer &get_frame_timer()
	{
		return timer;
	}

	virtual void release_resources()
	{
	}

	virtual void event_device_created(Device *device) = 0;
	virtual void event_device_destroyed() = 0;
	virtual void event_swapchain_created(Device *device, unsigned width, unsigned height,
	                                     float aspect_ratio, size_t num_swapchain_images, VkFormat format) = 0;
	virtual void event_swapchain_destroyed() = 0;
	virtual void event_frame_tick(double frame, double elapsed) = 0;
	virtual void event_swapchain_index(Device *device, unsigned index) = 0;
	virtual void event_display_timing_stutter(uint32_t current_serial, uint32_t observed_serial,
	                                          unsigned dropped_frames) = 0;

	virtual float get_estimated_frame_presentation_duration();

	virtual void set_window_title(const std::string &title);

protected:
	bool resize = false;

private:
	Util::FrameTimer timer;
};

enum class PresentMode
{
	SyncToVBlank,
	Unlocked
};

class WSI
{
public:
	WSI();
	void set_platform(WSIPlatform *platform);
	void set_present_mode(PresentMode mode);
	PresentMode get_present_mode() const
	{
		return present_mode;
	}

	bool init();

	bool init_external_context(std::unique_ptr<Vulkan::Context> context);
	bool init_external_swapchain(std::vector<Vulkan::ImageHandle> external_images);
	void deinit_external();

	~WSI();

	inline Context &get_context()
	{
		return *context;
	}

	inline Device &get_device()
	{
		return *device;
	}

	bool begin_frame();
	bool end_frame();
	void set_external_frame(unsigned index, Vulkan::Semaphore acquire_semaphore, double frame_time);
	Vulkan::Semaphore consume_external_release_semaphore();

	WSIPlatform &get_platform()
	{
		VK_ASSERT(platform);
		return *platform;
	}

	void deinit_surface_and_swapchain();
	void init_surface_and_swapchain(VkSurfaceKHR new_surface);

	float get_estimated_video_latency();
	void set_window_title(const std::string &title);

	double get_smooth_frame_time() const;
	double get_smooth_elapsed_time() const;

	double get_estimated_refresh_interval() const;

	WSITiming &get_timing()
	{
		return timing;
	}

private:
	void update_framebuffer(unsigned width, unsigned height);

	std::unique_ptr<Context> context;
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	std::vector<VkImage> swapchain_images;
	std::unique_ptr<Device> device;

	unsigned width = 0;
	unsigned height = 0;
	float aspect_ratio = 1.0f;
	VkFormat format = VK_FORMAT_UNDEFINED;
	PresentMode current_present_mode = PresentMode::SyncToVBlank;
	PresentMode present_mode = PresentMode::SyncToVBlank;

	enum class SwapchainError
	{
		None,
		NoSurface,
		Error
	};
	SwapchainError init_swapchain(unsigned width, unsigned height);
	bool blocking_init_swapchain(unsigned width, unsigned height);

	uint32_t swapchain_index = 0;
	bool has_acquired_swapchain_index = false;

	WSIPlatform *platform = nullptr;

	std::vector<Vulkan::ImageHandle> external_swapchain_images;

	unsigned external_frame_index = 0;
	Vulkan::Semaphore external_acquire;
	Vulkan::Semaphore external_release;
	bool frame_is_external = false;
	bool using_display_timing = false;
	bool begin_frame_external();
	double external_frame_time = 0.0;

	double smooth_frame_time = 0.0;
	double smooth_elapsed_time = 0.0;

	WSITiming timing;
};
}
