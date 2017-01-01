#pragma once

#include "util.hpp"
#include "vulkan_symbol_wrapper.h"
#include <memory>
#include <stdexcept>

#define STRINGIFY(x) #x

#define V(x)                                                                                           \
	do                                                                                                 \
	{                                                                                                  \
		VkResult err = x;                                                                              \
		if (err != VK_SUCCESS && err != VK_INCOMPLETE)                                                 \
			throw std::runtime_error("Vulkan call failed at " __FILE__ ":" STRINGIFY(__LINE__) ".\n"); \
	} while (0)

#define LOG(...)                      \
	do                                \
	{                                 \
		fprintf(stderr, __VA_ARGS__); \
	} while (0)

#ifdef VULKAN_DEBUG
#define VK_ASSERT(x)                                             \
	do                                                           \
	{                                                            \
		if (!(x))                                                \
		{                                                        \
			LOG("Vulkan error at %s:%d.\n", __FILE__, __LINE__); \
			std::terminate();                                    \
		}                                                        \
	} while (0)
#else
#define VK_ASSERT(x)
#endif

namespace Vulkan
{
struct NoCopyNoMove
{
	NoCopyNoMove() = default;
	NoCopyNoMove(const NoCopyNoMove &) = delete;
	void operator=(const NoCopyNoMove &) = delete;
};
}

namespace Vulkan
{
class Context
{
public:
	Context(const char **instance_ext, uint32_t instance_ext_count, const char **device_ext, uint32_t device_ext_count);
	Context(VkInstance instance, VkPhysicalDevice gpu, VkDevice device, VkQueue queue, uint32_t queue_family);
	Context(VkInstance instance, VkPhysicalDevice gpu, VkSurfaceKHR surface, const char **required_device_extensions,
	        unsigned num_required_device_extensions, const char **required_device_layers,
	        unsigned num_required_device_layers, const VkPhysicalDeviceFeatures *required_features);

	Context(const Context &) = delete;
	void operator=(const Context &) = delete;
	static bool init_loader(PFN_vkGetInstanceProcAddr addr);

	~Context();

	VkInstance get_instance() const
	{
		return instance;
	}

	VkPhysicalDevice get_gpu() const
	{
		return gpu;
	}

	VkDevice get_device() const
	{
		return device;
	}

	VkQueue get_queue() const
	{
		return queue;
	}

	const VkPhysicalDeviceProperties &get_gpu_props() const
	{
		return gpu_props;
	}

	const VkPhysicalDeviceMemoryProperties &get_mem_props() const
	{
		return mem_props;
	}

	uint32_t get_queue_family() const
	{
		return queue_family;
	}

	void release_instance()
	{
		owned_instance = false;
	}

	void release_device()
	{
		owned_device = false;
	}

	static const VkApplicationInfo &get_application_info();

private:
	VkDevice device = VK_NULL_HANDLE;
	VkInstance instance = VK_NULL_HANDLE;
	VkPhysicalDevice gpu = VK_NULL_HANDLE;

	VkPhysicalDeviceProperties gpu_props;
	VkPhysicalDeviceMemoryProperties mem_props;

	VkQueue queue = VK_NULL_HANDLE;
	uint32_t queue_family = VK_QUEUE_FAMILY_IGNORED;

	bool create_instance(const char **instance_ext, uint32_t instance_ext_count);
	bool create_device(VkPhysicalDevice gpu, VkSurfaceKHR surface, const char **required_device_extensions,
	                   unsigned num_required_device_extensions, const char **required_device_layers,
	                   unsigned num_required_device_layers, const VkPhysicalDeviceFeatures *required_features);

	bool owned_instance = false;
	bool owned_device = false;

#ifdef VULKAN_DEBUG
	VkDebugReportCallbackEXT debug_callback = VK_NULL_HANDLE;
#endif

	void destroy();
};
}
