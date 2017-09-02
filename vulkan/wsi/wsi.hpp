/* Copyright (c) 2017 Hans-Kristian Arntzen
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
#include "vulkan_symbol_wrapper.h"
#include <memory>
#include <vector>

namespace Granite
{
class ApplicationPlatform;
}

namespace Vulkan
{

class WSI
{
public:
	WSI();
	bool init(unsigned width, unsigned height);
	void set_platform(Granite::ApplicationPlatform *platform);
	bool init_external(std::unique_ptr<Vulkan::Context> context,
	                   std::vector<Vulkan::ImageHandle> external_images);
	bool reinit_external_swapchain(std::vector<Vulkan::ImageHandle> external_images);
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
	Vulkan::Semaphore get_external_release_semaphore();

	Granite::ApplicationPlatform &get_platform()
	{
		VK_ASSERT(platform);
		return *platform;
	}

	void deinit_surface_and_swapchain();
	void init_surface_and_swapchain(VkSurfaceKHR new_surface);

private:
	void update_framebuffer(unsigned width, unsigned height);

	std::unique_ptr<Context> context;
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	std::vector<VkImage> swapchain_images;
	std::unique_ptr<Device> device;
	std::unique_ptr<SemaphoreManager> semaphore_manager;

	unsigned width = 0;
	unsigned height = 0;
	float aspect_ratio = 1.0f;
	VkFormat format = VK_FORMAT_UNDEFINED;

	bool init_swapchain(unsigned width, unsigned height);
	uint32_t swapchain_index = 0;
	VkSemaphore release_semaphore;
	bool need_acquire = true;

	Granite::ApplicationPlatform *platform = nullptr;

	bool init_external_swapchain(std::vector<Vulkan::ImageHandle> external_images);
	std::vector<Vulkan::ImageHandle> external_swapchain_images;

	unsigned external_frame_index = 0;
	Vulkan::Semaphore external_acquire;
	Vulkan::Semaphore external_release;
	bool frame_is_external = false;
	bool begin_frame_external();
	double external_frame_time = 0.0;
};
}
