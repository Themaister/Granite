#include "vulkan.hpp"
#include "vulkan_symbol_wrapper.h"
#include <stdexcept>
#include <vector>

#ifdef HAVE_GLFW
#include <GLFW/glfw3.h>
#endif

#ifdef HAVE_DYLIB
#include <dlfcn.h>
#endif

using namespace std;

//#undef VULKAN_DEBUG

namespace Vulkan
{
Context::Context(const char **instance_ext, uint32_t instance_ext_count, const char **device_ext,
                 uint32_t device_ext_count)
    : owned_instance(true)
    , owned_device(true)
{
	if (!create_instance(instance_ext, instance_ext_count))
	{
		destroy();
		throw runtime_error("Failed to create Vulkan instance.");
	}

	VkPhysicalDeviceFeatures features = {};
	if (!create_device(VK_NULL_HANDLE, VK_NULL_HANDLE, device_ext, device_ext_count, nullptr, 0, &features))
	{
		destroy();
		throw runtime_error("Failed to create Vulkan device.");
	}
}

bool Context::init_loader(PFN_vkGetInstanceProcAddr addr)
{
	if (!addr)
	{
#ifdef HAVE_DYLIB
		static void *module;
		if (!module)
		{
			module = dlopen("libvulkan.so", RTLD_LOCAL | RTLD_LAZY);
			if (!module)
				return false;
		}

		addr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(module, "vkGetInstanceProcAddr"));
		if (!addr)
			return false;
#else
		return false;
#endif
	}

	vulkan_symbol_wrapper_init(addr);
	return vulkan_symbol_wrapper_load_global_symbols();
}

Context::Context(VkInstance instance, VkPhysicalDevice gpu, VkDevice device, VkQueue queue, uint32_t queue_family)
    : device(device)
    , instance(instance)
    , gpu(gpu)
    , queue(queue)
    , queue_family(queue_family)
    , owned_instance(false)
    , owned_device(false)
{
	vulkan_symbol_wrapper_load_core_instance_symbols(instance);
	vulkan_symbol_wrapper_load_core_device_symbols(device);

	vkGetPhysicalDeviceProperties(gpu, &gpu_props);
	vkGetPhysicalDeviceMemoryProperties(gpu, &mem_props);
}

Context::Context(VkInstance instance, VkPhysicalDevice gpu, VkSurfaceKHR surface,
                 const char **required_device_extensions, unsigned num_required_device_extensions,
                 const char **required_device_layers, unsigned num_required_device_layers,
                 const VkPhysicalDeviceFeatures *required_features)
    : instance(instance)
    , owned_instance(false)
    , owned_device(true)
{
	vulkan_symbol_wrapper_load_core_instance_symbols(instance);
	if (!create_device(gpu, surface, required_device_extensions, num_required_device_extensions, required_device_layers,
	                   num_required_device_layers, required_features))
	{
		destroy();
		throw runtime_error("Failed to create Vulkan device.");
	}
}

void Context::destroy()
{
	if (device != VK_NULL_HANDLE)
		vkDeviceWaitIdle(device);

#ifdef VULKAN_DEBUG
	if (debug_callback)
		vkDestroyDebugReportCallbackEXT(instance, debug_callback, nullptr);
#endif

	if (owned_device && device != VK_NULL_HANDLE)
		vkDestroyDevice(device, nullptr);
	if (owned_instance && instance != VK_NULL_HANDLE)
		vkDestroyInstance(instance, nullptr);
}

Context::~Context()
{
	destroy();
}

const VkApplicationInfo &Context::get_application_info()
{
	static const VkApplicationInfo info = {
		VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr, "paraLLEl PSX", 0, "paraLLEl PSX", 0, VK_MAKE_VERSION(1, 0, 18),
	};
	return info;
}

#ifdef VULKAN_DEBUG
static VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_debug_cb(VkDebugReportFlagsEXT flags,
                                                      VkDebugReportObjectTypeEXT objectType, uint64_t object,
                                                      size_t location, int32_t messageCode, const char *pLayerPrefix,
                                                      const char *pMessage, void *pUserData)
{
	(void)objectType;
	(void)object;
	(void)location;
	(void)messageCode;
	(void)pUserData;

	if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT)
	{
		fprintf(stderr, "[Vulkan]: Error: %s: %s\n", pLayerPrefix, pMessage);
	}
	else if (flags & VK_DEBUG_REPORT_WARNING_BIT_EXT)
	{
		fprintf(stderr, "[Vulkan]: Warning: %s: %s\n", pLayerPrefix, pMessage);
	}
	else if (flags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT)
	{
		//fprintf(stderr, "[Vulkan]: Performance warning: %s: %s\n", pLayerPrefix, pMessage);
	}
	else
	{
		fprintf(stderr, "[Vulkan]: Information: %s: %s\n", pLayerPrefix, pMessage);
	}

	return VK_FALSE;
}
#endif

bool Context::create_instance(const char **instance_ext, uint32_t instance_ext_count)
{
	VkInstanceCreateInfo info = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
	info.pApplicationInfo = &get_application_info();

	vector<const char *> instance_exts;
	vector<const char *> instance_layers;
	for (uint32_t i = 0; i < instance_ext_count; i++)
		instance_exts.push_back(instance_ext[i]);

#ifdef VULKAN_DEBUG
	instance_exts.push_back("VK_EXT_debug_report");
	instance_layers.push_back("VK_LAYER_LUNARG_standard_validation");
#endif

	info.enabledExtensionCount = instance_exts.size();
	info.ppEnabledExtensionNames = instance_exts.empty() ? nullptr : instance_exts.data();
	info.enabledLayerCount = instance_layers.size();
	info.ppEnabledLayerNames = instance_layers.empty() ? nullptr : instance_layers.data();

	if (vkCreateInstance(&info, nullptr, &instance) != VK_SUCCESS)
		return false;

	vulkan_symbol_wrapper_load_core_instance_symbols(instance);

#ifdef VULKAN_DEBUG
	VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_EXTENSION_SYMBOL(instance, vkCreateDebugReportCallbackEXT);
	VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_EXTENSION_SYMBOL(instance, vkDebugReportMessageEXT);
	VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_EXTENSION_SYMBOL(instance, vkDestroyDebugReportCallbackEXT);

	{
		VkDebugReportCallbackCreateInfoEXT info = { VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT };
		info.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT |
		             VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
		info.pfnCallback = vulkan_debug_cb;
		vkCreateDebugReportCallbackEXT(instance, &info, NULL, &debug_callback);
	}
#endif
	return true;
}

bool Context::create_device(VkPhysicalDevice gpu, VkSurfaceKHR surface, const char **required_device_extensions,
                            unsigned num_required_device_extensions, const char **required_device_layers,
                            unsigned num_required_device_layers, const VkPhysicalDeviceFeatures *required_features)
{
	if (gpu == VK_NULL_HANDLE)
	{
		uint32_t gpu_count = 0;
		V(vkEnumeratePhysicalDevices(instance, &gpu_count, nullptr));
		vector<VkPhysicalDevice> gpus(gpu_count);
		V(vkEnumeratePhysicalDevices(instance, &gpu_count, gpus.data()));
		gpu = gpus.front();
	}

	this->gpu = gpu;
	vkGetPhysicalDeviceProperties(gpu, &gpu_props);
	vkGetPhysicalDeviceMemoryProperties(gpu, &mem_props);

	uint32_t queue_count;
	vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queue_count, nullptr);
	vector<VkQueueFamilyProperties> queue_props(queue_count);
	vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queue_count, queue_props.data());

	if (surface != VK_NULL_HANDLE)
	{
		VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_EXTENSION_SYMBOL(instance, vkGetPhysicalDeviceSurfaceSupportKHR);
	}

	bool found_queue = false;
	for (unsigned i = 0; i < queue_count; i++)
	{
		VkBool32 supported = surface == VK_NULL_HANDLE;
#ifdef HAVE_GLFW
		supported = glfwGetPhysicalDevicePresentationSupport(instance, gpu, i);
#else
		if (surface != VK_NULL_HANDLE)
			vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface, &supported);
#endif

		VkQueueFlags required = VK_QUEUE_COMPUTE_BIT | VK_QUEUE_GRAPHICS_BIT;
		if (supported && ((queue_props[i].queueFlags & required) == required))
		{
			found_queue = true;
			queue_family = i;
			break;
		}
	}

	if (!found_queue)
		return false;

	static const float prio = 1.0f;
	VkDeviceQueueCreateInfo queue_info = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };

	VkDeviceCreateInfo device_info = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
	device_info.pQueueCreateInfos = &queue_info;

	queue_info.queueFamilyIndex = queue_family;
	queue_info.queueCount = 1;
	queue_info.pQueuePriorities = &prio;
	device_info.queueCreateInfoCount = 1;

	// Should query for these, but no big deal for now.
	device_info.ppEnabledExtensionNames = required_device_extensions;
	device_info.enabledExtensionCount = num_required_device_extensions;
	device_info.ppEnabledLayerNames = required_device_layers;
	device_info.enabledLayerCount = num_required_device_layers;
	device_info.pEnabledFeatures = required_features;

#ifdef VULKAN_DEBUG
	static const char *device_layers[] = { "VK_LAYER_LUNARG_standard_validation" };
	device_info.enabledLayerCount = 1;
	device_info.ppEnabledLayerNames = device_layers;
#endif

	if (vkCreateDevice(gpu, &device_info, nullptr, &device) != VK_SUCCESS)
		return false;

	vulkan_symbol_wrapper_load_core_device_symbols(device);
	vkGetDeviceQueue(device, queue_family, 0, &queue);
	return true;
}
}
