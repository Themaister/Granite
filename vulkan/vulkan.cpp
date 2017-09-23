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

#include "vulkan.hpp"
#include "vulkan_symbol_wrapper.h"
#include <stdexcept>
#include <vector>
#include <algorithm>
#include "vulkan_events.hpp"
#include <string.h>

#ifdef HAVE_GLFW
#include <GLFW/glfw3.h>
#endif

#ifdef HAVE_DYLIB
#include <dlfcn.h>
#endif

using namespace std;

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
			module = dlopen("libvulkan.so.1", RTLD_LOCAL | RTLD_LAZY);
			if (!module)
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
    , graphics_queue(queue)
    , compute_queue(queue)
    , transfer_queue(queue)
    , graphics_queue_family(queue_family)
    , compute_queue_family(queue_family)
    , transfer_queue_family(queue_family)
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
		VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr, "Granite", 0, "Granite", 0, VK_MAKE_VERSION(1, 0, 57),
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

	// False positives about lack of srcAccessMask/dstAccessMask.
	if (strcmp(pLayerPrefix, "DS") == 0 && messageCode == 10)
		return VK_FALSE;

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

	uint32_t ext_count = 0;
	vkEnumerateInstanceExtensionProperties(nullptr, &ext_count, nullptr);
	vector<VkExtensionProperties> queried_extensions(ext_count);
	if (ext_count)
		vkEnumerateInstanceExtensionProperties(nullptr, &ext_count, queried_extensions.data());

	uint32_t layer_count = 0;
	vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
	vector<VkLayerProperties> queried_layers(layer_count);
	if (layer_count)
		vkEnumerateInstanceLayerProperties(&layer_count, queried_layers.data());

	const auto has_extension = [&](const char *name) -> bool {
		auto itr = find_if(begin(queried_extensions), end(queried_extensions), [name](const VkExtensionProperties &e) -> bool {
			return strcmp(e.extensionName, name) == 0;
		});
		return itr != end(queried_extensions);
	};

	for (uint32_t i = 0; i < instance_ext_count; i++)
		if (!has_extension(instance_ext[i]))
			return false;

	if (has_extension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME) &&
	    has_extension(VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME) &&
	    has_extension(VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME))
	{
		instance_exts.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
		instance_exts.push_back(VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME);
		instance_exts.push_back(VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME);
		supports_external = true;
	}

#ifdef VULKAN_DEBUG
	const auto has_layer = [&](const char *name) -> bool {
		auto itr = find_if(begin(queried_layers), end(queried_layers), [name](const VkLayerProperties &e) -> bool {
			return strcmp(e.layerName, name) == 0;
		});
		return itr != end(queried_layers);
	};

	if (has_extension(VK_EXT_DEBUG_REPORT_EXTENSION_NAME))
		instance_exts.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
	if (has_layer("VK_LAYER_LUNARG_standard_validation"))
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
	if (has_extension(VK_EXT_DEBUG_REPORT_EXTENSION_NAME))
	{
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
	}
#endif

	if (supports_external)
	{
		VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_EXTENSION_SYMBOL(instance, vkGetPhysicalDeviceFeatures2KHR);
		VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_EXTENSION_SYMBOL(instance, vkGetPhysicalDeviceProperties2KHR);
		VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_EXTENSION_SYMBOL(instance, vkGetPhysicalDeviceFormatProperties2KHR);
		VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_EXTENSION_SYMBOL(instance, vkGetPhysicalDeviceImageFormatProperties2KHR);
		VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_EXTENSION_SYMBOL(instance, vkGetPhysicalDeviceSparseImageFormatProperties2KHR);
		VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_EXTENSION_SYMBOL(instance, vkGetPhysicalDeviceQueueFamilyProperties2KHR);
		VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_EXTENSION_SYMBOL(instance, vkGetPhysicalDeviceMemoryProperties2KHR);
		VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_EXTENSION_SYMBOL(instance, vkGetPhysicalDeviceExternalFencePropertiesKHR);
		VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_EXTENSION_SYMBOL(instance, vkGetPhysicalDeviceExternalSemaphorePropertiesKHR);
		VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_EXTENSION_SYMBOL(instance, vkGetPhysicalDeviceExternalBufferPropertiesKHR);
	}

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

		for (auto &gpu : gpus)
		{
			VkPhysicalDeviceProperties props;
			vkGetPhysicalDeviceProperties(gpu, &props);
			LOGI("Found Vulkan GPU: %s\n", props.deviceName);
		}

		gpu = gpus.front();
	}

	uint32_t ext_count = 0;
	vkEnumerateDeviceExtensionProperties(gpu, nullptr, &ext_count, nullptr);
	vector<VkExtensionProperties> queried_extensions(ext_count);
	if (ext_count)
		vkEnumerateDeviceExtensionProperties(gpu, nullptr, &ext_count, queried_extensions.data());

	uint32_t layer_count = 0;
	vkEnumerateDeviceLayerProperties(gpu, &layer_count, nullptr);
	vector<VkLayerProperties> queried_layers(layer_count);
	if (layer_count)
		vkEnumerateDeviceLayerProperties(gpu, &layer_count, queried_layers.data());

	const auto has_extension = [&](const char *name) -> bool {
		auto itr = find_if(begin(queried_extensions), end(queried_extensions), [name](const VkExtensionProperties &e) -> bool {
			return strcmp(e.extensionName, name) == 0;
		});
		return itr != end(queried_extensions);
	};

	const auto has_layer = [&](const char *name) -> bool {
		auto itr = find_if(begin(queried_layers), end(queried_layers), [name](const VkLayerProperties &e) -> bool {
			return strcmp(e.layerName, name) == 0;
		});
		return itr != end(queried_layers);
	};

	for (uint32_t i = 0; i < num_required_device_extensions; i++)
		if (!has_extension(required_device_extensions[i]))
			return false;

	for (uint32_t i = 0; i < num_required_device_layers; i++)
		if (!has_layer(required_device_layers[i]))
			return false;

	this->gpu = gpu;
	vkGetPhysicalDeviceProperties(gpu, &gpu_props);
	vkGetPhysicalDeviceMemoryProperties(gpu, &mem_props);

	LOGI("Selected Vulkan GPU: %s\n", gpu_props.deviceName);

	uint32_t queue_count;
	vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queue_count, nullptr);
	vector<VkQueueFamilyProperties> queue_props(queue_count);
	vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queue_count, queue_props.data());

	if (surface != VK_NULL_HANDLE)
	{
		VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_EXTENSION_SYMBOL(instance, vkGetPhysicalDeviceSurfaceSupportKHR);
	}

	for (unsigned i = 0; i < queue_count; i++)
	{
#ifdef HAVE_GLFW
		VkBool32 supported = glfwGetPhysicalDevicePresentationSupport(instance, gpu, i);
#else
		VkBool32 supported = surface == VK_NULL_HANDLE;
		if (surface != VK_NULL_HANDLE)
			vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface, &supported);
#endif

		static const VkQueueFlags required = VK_QUEUE_COMPUTE_BIT | VK_QUEUE_GRAPHICS_BIT;
		if (supported && ((queue_props[i].queueFlags & required) == required))
		{
			graphics_queue_family = i;
			break;
		}
	}

	for (unsigned i = 0; i < queue_count; i++)
	{
		static const VkQueueFlags required = VK_QUEUE_COMPUTE_BIT;
		if (i != graphics_queue_family && (queue_props[i].queueFlags & required) == required)
		{
			compute_queue_family = i;
			break;
		}
	}

	for (unsigned i = 0; i < queue_count; i++)
	{
		static const VkQueueFlags required = VK_QUEUE_TRANSFER_BIT;
		if (i != graphics_queue_family && i != compute_queue_family && (queue_props[i].queueFlags & required) == required)
		{
			transfer_queue_family = i;
			break;
		}
	}

	if (transfer_queue_family == VK_QUEUE_FAMILY_IGNORED)
	{
		for (unsigned i = 0; i < queue_count; i++)
		{
			static const VkQueueFlags required = VK_QUEUE_TRANSFER_BIT;
			if (i != graphics_queue_family && (queue_props[i].queueFlags & required) == required)
			{
				transfer_queue_family = i;
				break;
			}
		}
	}

	if (graphics_queue_family == VK_QUEUE_FAMILY_IGNORED)
		return false;

	unsigned universal_queue_index = 1;
	uint32_t graphics_queue_index = 0;
	uint32_t compute_queue_index = 0;
	uint32_t transfer_queue_index = 0;

	if (compute_queue_family == VK_QUEUE_FAMILY_IGNORED)
	{
		compute_queue_family = graphics_queue_family;
		compute_queue_index = std::min(queue_props[graphics_queue_family].queueCount - 1, universal_queue_index);
		universal_queue_index++;
	}

	if (transfer_queue_family == VK_QUEUE_FAMILY_IGNORED)
	{
		transfer_queue_family = graphics_queue_family;
		transfer_queue_index = std::min(queue_props[graphics_queue_family].queueCount - 1, universal_queue_index);
		universal_queue_index++;
	}
	else if (transfer_queue_family == compute_queue_family)
		transfer_queue_index = std::min(queue_props[compute_queue_family].queueCount - 1, 1u);

	static const float prio[3] = { 1.0f, 1.0f, 1.0f };

	unsigned queue_family_count = 0;
	VkDeviceQueueCreateInfo queue_info[3] = {};

	VkDeviceCreateInfo device_info = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
	device_info.pQueueCreateInfos = queue_info;

	queue_info[queue_family_count].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queue_info[queue_family_count].queueFamilyIndex = graphics_queue_family;
	queue_info[queue_family_count].queueCount = std::min(universal_queue_index,
	                                                     queue_props[graphics_queue_family].queueCount);
	queue_info[queue_family_count].pQueuePriorities = prio;
	queue_family_count++;

	if (compute_queue_family != graphics_queue_family)
	{
		queue_info[queue_family_count].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queue_info[queue_family_count].queueFamilyIndex = compute_queue_family;
		queue_info[queue_family_count].queueCount = std::min(transfer_queue_family == compute_queue_family ? 2u : 1u,
		                                                     queue_props[compute_queue_family].queueCount);
		queue_info[queue_family_count].pQueuePriorities = prio;
		queue_family_count++;
	}

	if (transfer_queue_family != graphics_queue_family && transfer_queue_family != compute_queue_family)
	{
		queue_info[queue_family_count].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queue_info[queue_family_count].queueFamilyIndex = transfer_queue_family;
		queue_info[queue_family_count].queueCount = 1;
		queue_info[queue_family_count].pQueuePriorities = prio;
		queue_family_count++;
	}

	device_info.queueCreateInfoCount = queue_family_count;

	vector<const char *> enabled_extensions;
	vector<const char *> enabled_layers;

	for (uint32_t i = 0; i < num_required_device_extensions; i++)
		enabled_extensions.push_back(required_device_extensions[i]);
	for (uint32_t i = 0; i < num_required_device_layers; i++)
		enabled_layers.push_back(required_device_layers[i]);

	if (has_extension(VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME) &&
	    has_extension(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME))
	{
		supports_dedicated = true;
		enabled_extensions.push_back(VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME);
		enabled_extensions.push_back(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);
	}

#ifdef _WIN32
	supports_external = false;
#else
	if (supports_external && supports_dedicated &&
	    has_extension(VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME) &&
	    has_extension(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME) &&
	    has_extension(VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME) &&
	    has_extension(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME))
	{
		supports_external = true;
		enabled_extensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME);
		enabled_extensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME);
		enabled_extensions.push_back(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
		enabled_extensions.push_back(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
	}
	else
		supports_external = false;
#endif

	VkPhysicalDeviceFeatures enabled_features = *required_features;
	{
		VkPhysicalDeviceFeatures features;
		vkGetPhysicalDeviceFeatures(gpu, &features);
		if (features.textureCompressionETC2)
			enabled_features.textureCompressionETC2 = VK_TRUE;
		if (features.textureCompressionBC)
			enabled_features.textureCompressionBC = VK_TRUE;
		if (features.textureCompressionASTC_LDR)
			enabled_features.textureCompressionASTC_LDR = VK_TRUE;
		if (features.fullDrawIndexUint32)
			enabled_features.fullDrawIndexUint32 = VK_TRUE;
		if (features.imageCubeArray)
			enabled_features.imageCubeArray = VK_TRUE;
		if (features.fillModeNonSolid)
			enabled_features.fillModeNonSolid = VK_TRUE;
		if (features.independentBlend)
			enabled_features.independentBlend = VK_TRUE;
		if (features.sampleRateShading)
			enabled_features.sampleRateShading = VK_TRUE;
	}

	device_info.pEnabledFeatures = &enabled_features;

#ifdef VULKAN_DEBUG
	if (has_layer("VK_LAYER_LUNARG_standard_validation"))
		enabled_layers.push_back("VK_LAYER_LUNARG_standard_validation");
#endif

	device_info.enabledExtensionCount = enabled_extensions.size();
	device_info.ppEnabledExtensionNames = enabled_extensions.empty() ? nullptr : enabled_extensions.data();
	device_info.enabledLayerCount = enabled_layers.size();
	device_info.ppEnabledLayerNames = enabled_layers.empty() ? nullptr : enabled_layers.data();

	if (vkCreateDevice(gpu, &device_info, nullptr, &device) != VK_SUCCESS)
		return false;

	vulkan_symbol_wrapper_load_core_device_symbols(device);
	vkGetDeviceQueue(device, graphics_queue_family, graphics_queue_index, &graphics_queue);
	vkGetDeviceQueue(device, compute_queue_family, compute_queue_index, &compute_queue);
	vkGetDeviceQueue(device, transfer_queue_family, transfer_queue_index, &transfer_queue);

	if (supports_dedicated)
		VULKAN_SYMBOL_WRAPPER_LOAD_DEVICE_EXTENSION_SYMBOL(device, vkGetImageMemoryRequirements2KHR);

#ifndef _WIN32
	if (supports_external)
	{
		VULKAN_SYMBOL_WRAPPER_LOAD_DEVICE_EXTENSION_SYMBOL(device, vkImportSemaphoreFdKHR);
		VULKAN_SYMBOL_WRAPPER_LOAD_DEVICE_EXTENSION_SYMBOL(device, vkGetSemaphoreFdKHR);
		VULKAN_SYMBOL_WRAPPER_LOAD_DEVICE_EXTENSION_SYMBOL(device, vkGetMemoryFdKHR);
		VULKAN_SYMBOL_WRAPPER_LOAD_DEVICE_EXTENSION_SYMBOL(device, vkGetMemoryFdPropertiesKHR);
	}
#endif

	return true;
}
}
