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
	bool init(Granite::ApplicationPlatform *platform, unsigned width, unsigned height);

	~WSI();

	inline Context &get_context()
	{
		return *context;
	}

	inline Device &get_device()
	{
		return device;
	}

	bool begin_frame();
	bool end_frame();

	Granite::ApplicationPlatform &get_platform()
	{
		VK_ASSERT(platform);
		return *platform;
	}

#ifdef ANDROID
	static void set_global_native_window(ANativeWindow *window);
	void runtime_init_native_window(ANativeWindow *window);
	void runtime_term_native_window();
#endif

	void deinit_surface_and_swapchain();
	void init_surface_and_swapchain();

private:
	void update_framebuffer(unsigned width, unsigned height);

	std::unique_ptr<Context> context;
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	std::vector<VkImage> swapchain_images;
	Device device;

	unsigned width = 0;
	unsigned height = 0;
	VkFormat format = VK_FORMAT_UNDEFINED;
	SemaphoreManager semaphore_manager;

	bool init_swapchain(unsigned width, unsigned height);
	uint32_t swapchain_index = 0;
	VkSemaphore release_semaphore;
	bool need_acquire = true;

	Granite::ApplicationPlatform *platform = nullptr;
};
}
