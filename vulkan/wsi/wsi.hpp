#pragma once

#include "device.hpp"
#include "semaphore_manager.hpp"
#include "vulkan.hpp"
#include "vulkan_symbol_wrapper.h"

#if defined(HAVE_GLFW)
#include <GLFW/glfw3.h>
#endif

#include <memory>
#include <vector>

namespace Vulkan
{

class WSI
{
public:
	bool init(unsigned width, unsigned height);
	~WSI();

	bool alive();
	void update_framebuffer(unsigned width, unsigned height);

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

private:
	std::unique_ptr<Context> context;
#if defined(HAVE_GLFW)
	GLFWwindow *window = nullptr;
#endif
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
};
}
