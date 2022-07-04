/* Copyright (c) 2017-2022 Hans-Kristian Arntzen
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
#include "vulkan_headers.hpp"
#include "timer.hpp"
#include "wsi_timing.hpp"
#include <vector>
#include <thread>
#include <chrono>

namespace Vulkan
{
class WSI;

class WSIPlatform
{
public:
	virtual ~WSIPlatform() = default;

	virtual VkSurfaceKHR create_surface(VkInstance instance, VkPhysicalDevice gpu) = 0;
	// This is virtual so that application can hold ownership over the surface handle, for e.g. Qt interop.
	virtual void destroy_surface(VkInstance instance, VkSurfaceKHR surface);
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

	virtual void notify_current_swapchain_dimensions(unsigned width, unsigned height)
	{
		resize = false;
		current_swapchain_width = width;
		current_swapchain_height = height;
		swapchain_dimension_update_timestamp++;
	}

	virtual uint32_t get_surface_width() = 0;
	virtual uint32_t get_surface_height() = 0;

	virtual float get_aspect_ratio()
	{
		return float(get_surface_width()) / float(get_surface_height());
	}

	virtual bool alive(WSI &wsi) = 0;
	virtual void poll_input() = 0;
	virtual bool has_external_swapchain()
	{
		return false;
	}

	virtual void block_until_wsi_forward_progress(WSI &wsi)
	{
		get_frame_timer().enter_idle();
		while (!resize && alive(wsi))
		{
			poll_input();
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		get_frame_timer().leave_idle();
	}

	Util::FrameTimer &get_frame_timer()
	{
		return timer;
	}

	virtual void release_resources()
	{
	}

	virtual void event_device_created(Device *device);
	virtual void event_device_destroyed();
	virtual void event_swapchain_created(Device *device, unsigned width, unsigned height,
	                                     float aspect_ratio, size_t num_swapchain_images, VkFormat format, VkSurfaceTransformFlagBitsKHR pre_rotate);
	virtual void event_swapchain_destroyed();
	virtual void event_frame_tick(double frame, double elapsed);
	virtual void event_swapchain_index(Device *device, unsigned index);
	virtual void event_display_timing_stutter(uint32_t current_serial, uint32_t observed_serial,
	                                          unsigned dropped_frames);

	virtual float get_estimated_frame_presentation_duration();

	virtual void set_window_title(const std::string &title);

	virtual uintptr_t get_fullscreen_monitor();

	virtual const VkApplicationInfo *get_application_info();

protected:
	unsigned current_swapchain_width = 0;
	unsigned current_swapchain_height = 0;
	uint64_t swapchain_dimension_update_timestamp = 0;
	bool resize = false;

private:
	Util::FrameTimer timer;
};

enum class PresentMode
{
	SyncToVBlank, // Force FIFO
	UnlockedMaybeTear, // MAILBOX or IMMEDIATE
	UnlockedForceTearing, // Force IMMEDIATE
	UnlockedNoTearing // Force MAILBOX
};

class WSI
{
public:
	WSI();
	void set_platform(WSIPlatform *platform);
	void set_present_mode(PresentMode mode);
	void set_backbuffer_srgb(bool enable);
	void set_support_prerotate(bool enable);
	void set_extra_usage_flags(VkImageUsageFlags usage);
	VkSurfaceTransformFlagBitsKHR get_current_prerotate() const;

	PresentMode get_present_mode() const
	{
		return present_mode;
	}

	bool get_backbuffer_srgb() const
	{
		return srgb_backbuffer_enable;
	}

	// First, we need a Util::IntrinsivePtr<Vulkan::Context>.
	// This holds the instance and device.

	// The simple approach. WSI internally creates the context with instance + device.
	// Required information about extensions etc, is pulled from the platform.
	bool init_context_from_platform(unsigned num_thread_indices, const Context::SystemHandles &system_handles);

	// If you have your own VkInstance and/or VkDevice, you must create your own Vulkan::Context with
	// the appropriate init() call. Based on the platform you use, you must make sure to enable the
	// required extensions.
	bool init_from_existing_context(ContextHandle context);

	// Then we initialize the Vulkan::Device. Either lets WSI create its own device or reuse an existing handle.
	// A device provided here must have been bound to the context.
	bool init_device();
	bool init_device(DeviceHandle device);

	// Called after we have a device and context.
	// Either we can use a swapchain based on VkSurfaceKHR, or we can supply our own images
	// to create a virtual swapchain.
	// init_surface_swapchain() is called once.
	// Here we create the surface and perform creation of the first swapchain.
	bool init_surface_swapchain();
	bool init_external_swapchain(std::vector<ImageHandle> external_images);

	// Calls init_context_from_platform -> init_device -> init_surface_swapchain in succession.
	bool init_simple(unsigned num_thread_indices, const Context::SystemHandles &system_handles);

	~WSI();

	inline Context &get_context()
	{
		return *context;
	}

	inline Device &get_device()
	{
		return *device;
	}

	// Acquires a frame from swapchain, also calls poll_input() after acquire
	// since acquire tends to block.
	bool begin_frame();
	// Presents and iterates frame context.
	// Present is skipped if swapchain resource was not touched.
	// The normal app loop is something like begin_frame() -> submit work -> end_frame().
	bool end_frame();

	// For external swapchains we don't have a normal acquire -> present cycle.
	// - set_external_frame()
	//   - index replaces the acquire next image index.
	//   - acquire_semaphore replaces semaphore from acquire next image.
	//   - frame_time controls the frame time passed down.
	// - begin_frame()
	// - submit work
	// - end_frame()
	// - consume_external_release_semaphore()
	//   - Returns the release semaphore that can passed to the equivalent of QueuePresentKHR.
	void set_external_frame(unsigned index, Semaphore acquire_semaphore, double frame_time);
	Semaphore consume_external_release_semaphore();

	// Equivalent to calling destructor.
	void teardown();

	WSIPlatform &get_platform()
	{
		VK_ASSERT(platform);
		return *platform;
	}

	// For Android. Used in response to APP_CMD_{INIT,TERM}_WINDOW once
	// we have a proper swapchain going.
	// We have to completely drain swapchain before the window is terminated on Android.
	void deinit_surface_and_swapchain();
	void reinit_surface_and_swapchain(VkSurfaceKHR new_surface);

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

	ContextHandle context;
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	std::vector<VkImage> swapchain_images;
	std::vector<Semaphore> release_semaphores;
	DeviceHandle device;
	const VolkDeviceTable *table = nullptr;

	unsigned swapchain_width = 0;
	unsigned swapchain_height = 0;
	float swapchain_aspect_ratio = 1.0f;
	VkFormat swapchain_format = VK_FORMAT_UNDEFINED;
	PresentMode current_present_mode = PresentMode::SyncToVBlank;
	PresentMode present_mode = PresentMode::SyncToVBlank;
	VkImageUsageFlags current_extra_usage = 0;
	VkImageUsageFlags extra_usage = 0;
	bool swapchain_is_suboptimal = false;

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

	std::vector<ImageHandle> external_swapchain_images;

	unsigned external_frame_index = 0;
	Semaphore external_acquire;
	Semaphore external_release;
	bool frame_is_external = false;
	bool using_display_timing = false;
	bool srgb_backbuffer_enable = true;
	bool current_srgb_backbuffer_enable = true;
	bool support_prerotate = false;
	VkSurfaceTransformFlagBitsKHR swapchain_current_prerotate = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;

	bool begin_frame_external();
	double external_frame_time = 0.0;

	double smooth_frame_time = 0.0;
	double smooth_elapsed_time = 0.0;

	uint64_t present_id = 0;
	uint64_t present_last_id = 0;
	unsigned present_frame_latency = 0;

	WSITiming timing;

	void tear_down_swapchain();
	void drain_swapchain();

	VkSurfaceFormatKHR find_suitable_present_format(const std::vector<VkSurfaceFormatKHR> &formats) const;
};
}
