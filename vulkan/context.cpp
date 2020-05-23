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

#include "context.hpp"
#include <vector>
#include <mutex>
#include <algorithm>
#include <string.h>

#ifndef _WIN32
#include <dlfcn.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

//#undef VULKAN_DEBUG

using namespace std;

namespace Vulkan
{
bool Context::init_instance_and_device(const char **instance_ext, uint32_t instance_ext_count, const char **device_ext,
                                       uint32_t device_ext_count, ContextCreationFlags flags)
{
	destroy();

	owned_instance = true;
	owned_device = true;
	if (!create_instance(instance_ext, instance_ext_count))
	{
		destroy();
		LOGE("Failed to create Vulkan instance.\n");
		return false;
	}

	VkPhysicalDeviceFeatures features = {};
	if (!create_device(VK_NULL_HANDLE, VK_NULL_HANDLE, device_ext, device_ext_count, nullptr, 0, &features, flags))
	{
		destroy();
		LOGE("Failed to create Vulkan device.\n");
		return false;
	}

	return true;
}

static mutex loader_init_lock;
static bool loader_init_once;

bool Context::init_loader(PFN_vkGetInstanceProcAddr addr)
{
	lock_guard<mutex> holder(loader_init_lock);
	if (loader_init_once && !addr)
		return true;

	if (!addr)
	{
#ifndef _WIN32
		static void *module;
		if (!module)
		{
			const char *vulkan_path = getenv("GRANITE_VULKAN_LIBRARY");
			if (vulkan_path)
				module = dlopen(vulkan_path, RTLD_LOCAL | RTLD_LAZY);
#ifdef __APPLE__
			if (!module)
				module = dlopen("libvulkan.1.dylib", RTLD_LOCAL | RTLD_LAZY);
#else
			if (!module)
				module = dlopen("libvulkan.so.1", RTLD_LOCAL | RTLD_LAZY);
			if (!module)
				module = dlopen("libvulkan.so", RTLD_LOCAL | RTLD_LAZY);
#endif
			if (!module)
				return false;
		}

		addr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(module, "vkGetInstanceProcAddr"));
		if (!addr)
			return false;
#else
		static HMODULE module;
		if (!module)
		{
			module = LoadLibraryA("vulkan-1.dll");
			if (!module)
				return false;
		}

		// Ugly pointer warning workaround.
		auto ptr = GetProcAddress(module, "vkGetInstanceProcAddr");
		static_assert(sizeof(ptr) == sizeof(addr), "Mismatch pointer type.");
		memcpy(&addr, &ptr, sizeof(ptr));

		if (!addr)
			return false;
#endif
	}

	volkInitializeCustom(addr);
	loader_init_once = true;
	return true;
}

bool Context::init_from_instance_and_device(VkInstance instance_, VkPhysicalDevice gpu_, VkDevice device_, VkQueue queue_, uint32_t queue_family_)
{
	destroy();

	device = device_;
	instance = instance_;
	gpu = gpu_;
	graphics_queue = queue_;
	compute_queue = queue_;
	transfer_queue = queue_;
	graphics_queue_family = queue_family_;
	compute_queue_family = queue_family_;
	transfer_queue_family = queue_family_;
	owned_instance = false;
	owned_device = true;

	volkLoadInstance(instance);
	volkLoadDeviceTable(&device_table, device);
	vkGetPhysicalDeviceProperties(gpu, &gpu_props);
	vkGetPhysicalDeviceMemoryProperties(gpu, &mem_props);
	return true;
}

bool Context::init_device_from_instance(VkInstance instance_, VkPhysicalDevice gpu_, VkSurfaceKHR surface,
                                        const char **required_device_extensions, unsigned num_required_device_extensions,
                                        const char **required_device_layers, unsigned num_required_device_layers,
                                        const VkPhysicalDeviceFeatures *required_features,
                                        ContextCreationFlags flags)
{
	destroy();

	instance = instance_;
	owned_instance = false;
	owned_device = true;

	if (!create_instance(nullptr, 0))
		return false;

	if (!create_device(gpu_, surface, required_device_extensions, num_required_device_extensions, required_device_layers,
	                   num_required_device_layers, required_features, flags))
	{
		destroy();
		LOGE("Failed to create Vulkan device.\n");
		return false;
	}

	return true;
}

void Context::destroy()
{
	if (device != VK_NULL_HANDLE)
		device_table.vkDeviceWaitIdle(device);

#ifdef VULKAN_DEBUG
	if (debug_callback)
		vkDestroyDebugReportCallbackEXT(instance, debug_callback, nullptr);
	if (debug_messenger)
		vkDestroyDebugUtilsMessengerEXT(instance, debug_messenger, nullptr);
	debug_callback = VK_NULL_HANDLE;
	debug_messenger = VK_NULL_HANDLE;
#endif

	if (owned_device && device != VK_NULL_HANDLE)
		device_table.vkDestroyDevice(device, nullptr);
	if (owned_instance && instance != VK_NULL_HANDLE)
		vkDestroyInstance(instance, nullptr);
}

Context::~Context()
{
	destroy();
}

const VkApplicationInfo &Context::get_application_info(bool supports_vulkan_11)
{
	static const VkApplicationInfo info_11 = {
		VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr, "Granite", 0, "Granite", 0, VK_API_VERSION_1_1,
	};

	static const VkApplicationInfo info = {
		VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr, "Granite", 0, "Granite", 0, VK_MAKE_VERSION(1, 0, 57),
	};
	return supports_vulkan_11 ? info_11 : info;
}

void Context::notify_validation_error(const char *msg)
{
	if (message_callback)
		message_callback(msg);
}

void Context::set_notification_callback(function<void(const char *)> func)
{
	message_callback = move(func);
}

#ifdef VULKAN_DEBUG
static VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_messenger_cb(
		VkDebugUtilsMessageSeverityFlagBitsEXT           messageSeverity,
		VkDebugUtilsMessageTypeFlagsEXT                  messageType,
		const VkDebugUtilsMessengerCallbackDataEXT*      pCallbackData,
		void *pUserData)
{
	auto *context = static_cast<Context *>(pUserData);

	switch (messageSeverity)
	{
	case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
		if (messageType == VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)
		{
			LOGE("[Vulkan]: Validation Error: %s\n", pCallbackData->pMessage);
			context->notify_validation_error(pCallbackData->pMessage);
		}
		else
			LOGE("[Vulkan]: Other Error: %s\n", pCallbackData->pMessage);
		break;

	case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
		if (messageType == VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)
			LOGW("[Vulkan]: Validation Warning: %s\n", pCallbackData->pMessage);
		else
			LOGW("[Vulkan]: Other Warning: %s\n", pCallbackData->pMessage);
		break;

#if 0
	case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
	case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
		if (messageType == VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)
			LOGI("[Vulkan]: Validation Info: %s\n", pCallbackData->pMessage);
		else
			LOGI("[Vulkan]: Other Info: %s\n", pCallbackData->pMessage);
		break;
#endif

	default:
		return VK_FALSE;
	}

	bool log_object_names = false;
	for (uint32_t i = 0; i < pCallbackData->objectCount; i++)
	{
		auto *name = pCallbackData->pObjects[i].pObjectName;
		if (name)
		{
			log_object_names = true;
			break;
		}
	}

	if (log_object_names)
	{
		for (uint32_t i = 0; i < pCallbackData->objectCount; i++)
		{
			auto *name = pCallbackData->pObjects[i].pObjectName;
			LOGI("  Object #%u: %s\n", i, name ? name : "N/A");
		}
	}

	return VK_FALSE;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_debug_cb(VkDebugReportFlagsEXT flags,
                                                      VkDebugReportObjectTypeEXT, uint64_t,
                                                      size_t, int32_t messageCode, const char *pLayerPrefix,
                                                      const char *pMessage, void *pUserData)
{
	auto *context = static_cast<Context *>(pUserData);

	// False positives about lack of srcAccessMask/dstAccessMask.
	if (strcmp(pLayerPrefix, "DS") == 0 && messageCode == 10)
		return VK_FALSE;

	// Demote to a warning, it's a false positive almost all the time for Granite.
	if (strcmp(pLayerPrefix, "DS") == 0 && messageCode == 6)
		flags = VK_DEBUG_REPORT_DEBUG_BIT_EXT;

	if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT)
	{
		LOGE("[Vulkan]: Error: %s: %s\n", pLayerPrefix, pMessage);
		context->notify_validation_error(pMessage);
	}
	else if (flags & VK_DEBUG_REPORT_WARNING_BIT_EXT)
	{
		LOGW("[Vulkan]: Warning: %s: %s\n", pLayerPrefix, pMessage);
	}
	else if (flags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT)
	{
		//LOGW("[Vulkan]: Performance warning: %s: %s\n", pLayerPrefix, pMessage);
	}
	else
	{
		LOGI("[Vulkan]: Information: %s: %s\n", pLayerPrefix, pMessage);
	}

	return VK_FALSE;
}
#endif

bool Context::create_instance(const char **instance_ext, uint32_t instance_ext_count)
{
	ext.supports_vulkan_11_instance = volkGetInstanceVersion() >= VK_API_VERSION_1_1;

	VkInstanceCreateInfo info = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
	info.pApplicationInfo = &get_application_info(ext.supports_vulkan_11_instance);

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

	if (has_extension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
	{
		ext.supports_physical_device_properties2 = true;
		instance_exts.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
	}

	if (ext.supports_physical_device_properties2 &&
	    has_extension(VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME) &&
	    has_extension(VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME))
	{
		instance_exts.push_back(VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME);
		instance_exts.push_back(VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME);
		ext.supports_external = true;
	}

	if (has_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
	{
		instance_exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		ext.supports_debug_utils = true;
	}

	auto itr = find_if(instance_ext, instance_ext + instance_ext_count, [](const char *name) {
		return strcmp(name, VK_KHR_SURFACE_EXTENSION_NAME) == 0;
	});
	bool has_surface_extension = itr != (instance_ext + instance_ext_count);

	if (has_surface_extension && has_extension(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME))
	{
		instance_exts.push_back(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
		ext.supports_surface_capabilities2 = true;
	}

#ifdef VULKAN_DEBUG
	const auto has_layer = [&](const char *name) -> bool {
		auto layer_itr = find_if(begin(queried_layers), end(queried_layers), [name](const VkLayerProperties &e) -> bool {
			return strcmp(e.layerName, name) == 0;
		});
		return layer_itr != end(queried_layers);
	};

	if (!ext.supports_debug_utils && has_extension(VK_EXT_DEBUG_REPORT_EXTENSION_NAME))
		instance_exts.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);

	if (getenv("GRANITE_VULKAN_NO_VALIDATION"))
		force_no_validation = true;

	if (!force_no_validation && has_layer("VK_LAYER_KHRONOS_validation"))
	{
		instance_layers.push_back("VK_LAYER_KHRONOS_validation");
		LOGI("Enabling VK_LAYER_KHRONOS_validation.\n");
	}
	else if (!force_no_validation && has_layer("VK_LAYER_LUNARG_standard_validation"))
	{
		instance_layers.push_back("VK_LAYER_LUNARG_standard_validation");
		LOGI("Enabling VK_LAYER_LUNARG_standard_validation.\n");
	}
#endif

	info.enabledExtensionCount = instance_exts.size();
	info.ppEnabledExtensionNames = instance_exts.empty() ? nullptr : instance_exts.data();
	info.enabledLayerCount = instance_layers.size();
	info.ppEnabledLayerNames = instance_layers.empty() ? nullptr : instance_layers.data();

	for (auto *ext_name : instance_exts)
		LOGI("Enabling instance extension: %s.\n", ext_name);

	if (instance == VK_NULL_HANDLE)
		if (vkCreateInstance(&info, nullptr, &instance) != VK_SUCCESS)
			return false;

	volkLoadInstance(instance);

#ifdef VULKAN_DEBUG
	if (ext.supports_debug_utils)
	{
		VkDebugUtilsMessengerCreateInfoEXT debug_info = { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
		debug_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
		                             VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
		                             VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
		                             VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
		debug_info.pfnUserCallback = vulkan_messenger_cb;
		debug_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
		                         VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT |
		                         VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
		debug_info.pUserData = this;

		vkCreateDebugUtilsMessengerEXT(instance, &debug_info, nullptr, &debug_messenger);
	}
	else if (has_extension(VK_EXT_DEBUG_REPORT_EXTENSION_NAME))
	{
		VkDebugReportCallbackCreateInfoEXT debug_info = { VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT };
		debug_info.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT |
		                   VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
		debug_info.pfnCallback = vulkan_debug_cb;
		debug_info.pUserData = this;
		vkCreateDebugReportCallbackEXT(instance, &debug_info, nullptr, &debug_callback);
	}
#endif

	return true;
}

bool Context::create_device(VkPhysicalDevice gpu_, VkSurfaceKHR surface, const char **required_device_extensions,
                            unsigned num_required_device_extensions, const char **required_device_layers,
                            unsigned num_required_device_layers, const VkPhysicalDeviceFeatures *required_features,
                            ContextCreationFlags flags)
{
	gpu = gpu_;
	if (gpu == VK_NULL_HANDLE)
	{
		uint32_t gpu_count = 0;
		if (vkEnumeratePhysicalDevices(instance, &gpu_count, nullptr) != VK_SUCCESS)
			return false;

		if (gpu_count == 0)
			return false;

		vector<VkPhysicalDevice> gpus(gpu_count);
		if (vkEnumeratePhysicalDevices(instance, &gpu_count, gpus.data()) != VK_SUCCESS)
			return false;

		for (auto &g : gpus)
		{
			VkPhysicalDeviceProperties props;
			vkGetPhysicalDeviceProperties(g, &props);
			LOGI("Found Vulkan GPU: %s\n", props.deviceName);
			LOGI("    API: %u.%u.%u\n",
			     VK_VERSION_MAJOR(props.apiVersion),
			     VK_VERSION_MINOR(props.apiVersion),
			     VK_VERSION_PATCH(props.apiVersion));
			LOGI("    Driver: %u.%u.%u\n",
			     VK_VERSION_MAJOR(props.driverVersion),
			     VK_VERSION_MINOR(props.driverVersion),
			     VK_VERSION_PATCH(props.driverVersion));
		}

		const char *gpu_index = getenv("GRANITE_VULKAN_DEVICE_INDEX");
		if (gpu_index)
		{
			unsigned index = strtoul(gpu_index, nullptr, 0);
			if (index < gpu_count)
				gpu = gpus[index];
		}

		if (gpu == VK_NULL_HANDLE)
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

	vkGetPhysicalDeviceProperties(gpu, &gpu_props);
	vkGetPhysicalDeviceMemoryProperties(gpu, &mem_props);

	LOGI("Selected Vulkan GPU: %s\n", gpu_props.deviceName);

	if (gpu_props.apiVersion >= VK_API_VERSION_1_1)
	{
		ext.supports_vulkan_11_device = ext.supports_vulkan_11_instance;
		LOGI("GPU supports Vulkan 1.1.\n");
	}
	else if (gpu_props.apiVersion >= VK_API_VERSION_1_0)
	{
		ext.supports_vulkan_11_device = false;
		LOGI("GPU supports Vulkan 1.0.\n");
	}

	uint32_t queue_count;
	vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queue_count, nullptr);
	vector<VkQueueFamilyProperties> queue_props(queue_count);
	vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queue_count, queue_props.data());

	for (unsigned i = 0; i < queue_count; i++)
	{
		VkBool32 supported = surface == VK_NULL_HANDLE;
		if (surface != VK_NULL_HANDLE)
			vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface, &supported);

		static const VkQueueFlags required = VK_QUEUE_COMPUTE_BIT | VK_QUEUE_GRAPHICS_BIT;
		if (supported && ((queue_props[i].queueFlags & required) == required))
		{
			graphics_queue_family = i;

			// XXX: This assumes timestamp valid bits is the same for all queue types.
			timestamp_valid_bits = queue_props[i].timestampValidBits;
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

	static const float graphics_queue_prio = 0.5f;
	static const float compute_queue_prio = 1.0f;
	static const float transfer_queue_prio = 1.0f;
	float prio[3] = { graphics_queue_prio, compute_queue_prio, transfer_queue_prio };

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
		queue_info[queue_family_count].pQueuePriorities = prio + 1;
		queue_family_count++;
	}

	if (transfer_queue_family != graphics_queue_family && transfer_queue_family != compute_queue_family)
	{
		queue_info[queue_family_count].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queue_info[queue_family_count].queueFamilyIndex = transfer_queue_family;
		queue_info[queue_family_count].queueCount = 1;
		queue_info[queue_family_count].pQueuePriorities = prio + 2;
		queue_family_count++;
	}

	device_info.queueCreateInfoCount = queue_family_count;

	vector<const char *> enabled_extensions;
	vector<const char *> enabled_layers;

	for (uint32_t i = 0; i < num_required_device_extensions; i++)
		enabled_extensions.push_back(required_device_extensions[i]);
	for (uint32_t i = 0; i < num_required_device_layers; i++)
		enabled_layers.push_back(required_device_layers[i]);

	if (has_extension(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME))
	{
		ext.supports_get_memory_requirements2 = true;
		enabled_extensions.push_back(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);
	}

	if (ext.supports_get_memory_requirements2 && has_extension(VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME))
	{
		ext.supports_dedicated = true;
		enabled_extensions.push_back(VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME);
	}

	if (has_extension(VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME))
	{
		ext.supports_image_format_list = true;
		enabled_extensions.push_back(VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME);
	}

	if (has_extension(VK_EXT_DEBUG_MARKER_EXTENSION_NAME))
	{
		ext.supports_debug_marker = true;
		enabled_extensions.push_back(VK_EXT_DEBUG_MARKER_EXTENSION_NAME);
	}

	if (has_extension(VK_KHR_SAMPLER_MIRROR_CLAMP_TO_EDGE_EXTENSION_NAME))
	{
		ext.supports_mirror_clamp_to_edge = true;
		enabled_extensions.push_back(VK_KHR_SAMPLER_MIRROR_CLAMP_TO_EDGE_EXTENSION_NAME);
	}

	if (has_extension(VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME))
	{
		ext.supports_google_display_timing = true;
		enabled_extensions.push_back(VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME);
	}

#ifdef _WIN32
	if (ext.supports_surface_capabilities2 && has_extension(VK_EXT_FULL_SCREEN_EXCLUSIVE_EXTENSION_NAME))
	{
		ext.supports_full_screen_exclusive = true;
		enabled_extensions.push_back(VK_EXT_FULL_SCREEN_EXCLUSIVE_EXTENSION_NAME);
	}
#endif

#ifdef VULKAN_DEBUG
	if (has_extension(VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME))
	{
		ext.supports_nv_device_diagnostic_checkpoints = true;
		enabled_extensions.push_back(VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME);
	}
#endif

	if (ext.supports_external && ext.supports_dedicated &&
	    has_extension(VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME) &&
	    has_extension(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME) &&
#ifdef _WIN32
	    has_extension(VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME) &&
	    has_extension(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME)
#else
	    has_extension(VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME) &&
	    has_extension(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME)
#endif
		)
	{
		ext.supports_external = true;
		enabled_extensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME);
		enabled_extensions.push_back(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
#ifdef _WIN32
		enabled_extensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME);
		enabled_extensions.push_back(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME);
#else
		enabled_extensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME);
		enabled_extensions.push_back(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
#endif
	}
	else
		ext.supports_external = false;

	if (has_extension(VK_KHR_DESCRIPTOR_UPDATE_TEMPLATE_EXTENSION_NAME))
	{
		enabled_extensions.push_back(VK_KHR_DESCRIPTOR_UPDATE_TEMPLATE_EXTENSION_NAME);
		ext.supports_update_template = true;
	}

	if (has_extension(VK_KHR_MAINTENANCE1_EXTENSION_NAME))
	{
		enabled_extensions.push_back(VK_KHR_MAINTENANCE1_EXTENSION_NAME);
		ext.supports_maintenance_1 = true;
	}

	if (has_extension(VK_KHR_MAINTENANCE2_EXTENSION_NAME))
	{
		enabled_extensions.push_back(VK_KHR_MAINTENANCE2_EXTENSION_NAME);
		ext.supports_maintenance_2 = true;
	}

	if (has_extension(VK_KHR_MAINTENANCE3_EXTENSION_NAME))
	{
		enabled_extensions.push_back(VK_KHR_MAINTENANCE3_EXTENSION_NAME);
		ext.supports_maintenance_3 = true;
	}

	if (has_extension(VK_KHR_BIND_MEMORY_2_EXTENSION_NAME))
	{
		ext.supports_bind_memory2 = true;
		enabled_extensions.push_back(VK_KHR_BIND_MEMORY_2_EXTENSION_NAME);
	}

	if (has_extension(VK_KHR_DRAW_INDIRECT_COUNT_EXTENSION_NAME))
	{
		ext.supports_draw_indirect_count = true;
		enabled_extensions.push_back(VK_KHR_DRAW_INDIRECT_COUNT_EXTENSION_NAME);
	}

	if (has_extension(VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME))
	{
		ext.supports_draw_parameters = true;
		enabled_extensions.push_back(VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME);
	}

	if (has_extension(VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME))
	{
		ext.supports_calibrated_timestamps = true;
		enabled_extensions.push_back(VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME);
	}

	if (has_extension(VK_KHR_STORAGE_BUFFER_STORAGE_CLASS_EXTENSION_NAME))
		enabled_extensions.push_back(VK_KHR_STORAGE_BUFFER_STORAGE_CLASS_EXTENSION_NAME);

	if (has_extension(VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME))
	{
		enabled_extensions.push_back(VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME);
		ext.supports_conservative_rasterization = true;
	}

	VkPhysicalDeviceFeatures2KHR features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2_KHR };
	ext.storage_8bit_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES_KHR };
	ext.storage_16bit_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES_KHR };
	ext.float16_int8_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT16_INT8_FEATURES_KHR };
	ext.multiview_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES_KHR };
	ext.imageless_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGELESS_FRAMEBUFFER_FEATURES_KHR };
	ext.subgroup_size_control_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES_EXT };
	ext.compute_shader_derivative_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_NV };
	ext.host_query_reset_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES_EXT };
	ext.demote_to_helper_invocation_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES_EXT };
	ext.scalar_block_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES_EXT };
	ext.ubo_std430_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFORM_BUFFER_STANDARD_LAYOUT_FEATURES_KHR };
	ext.timeline_semaphore_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES_KHR };
	ext.descriptor_indexing_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT };
	ext.performance_query_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PERFORMANCE_QUERY_FEATURES_KHR };
	ext.sampler_ycbcr_conversion_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES_KHR };
	void **ppNext = &features.pNext;

	bool has_pdf2 = ext.supports_physical_device_properties2 ||
	                (ext.supports_vulkan_11_instance && ext.supports_vulkan_11_device);

	if (has_pdf2)
	{
		if (has_extension(VK_KHR_8BIT_STORAGE_EXTENSION_NAME))
		{
			enabled_extensions.push_back(VK_KHR_8BIT_STORAGE_EXTENSION_NAME);
			*ppNext = &ext.storage_8bit_features;
			ppNext = &ext.storage_8bit_features.pNext;
		}

		if (has_extension(VK_KHR_16BIT_STORAGE_EXTENSION_NAME))
		{
			enabled_extensions.push_back(VK_KHR_16BIT_STORAGE_EXTENSION_NAME);
			*ppNext = &ext.storage_16bit_features;
			ppNext = &ext.storage_16bit_features.pNext;
		}

		if (has_extension(VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME))
		{
			enabled_extensions.push_back(VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME);
			*ppNext = &ext.float16_int8_features;
			ppNext = &ext.float16_int8_features.pNext;
		}

		if (has_extension(VK_KHR_MULTIVIEW_EXTENSION_NAME))
		{
			enabled_extensions.push_back(VK_KHR_MULTIVIEW_EXTENSION_NAME);
			*ppNext = &ext.multiview_features;
			ppNext = &ext.multiview_features.pNext;
		}

		if (has_extension(VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME))
		{
			enabled_extensions.push_back(VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME);
			*ppNext = &ext.subgroup_size_control_features;
			ppNext = &ext.subgroup_size_control_features.pNext;
		}

		if (has_extension(VK_NV_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME))
		{
			enabled_extensions.push_back(VK_NV_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME);
			*ppNext = &ext.compute_shader_derivative_features;
			ppNext = &ext.compute_shader_derivative_features.pNext;
		}

		if (has_extension(VK_EXT_HOST_QUERY_RESET_EXTENSION_NAME))
		{
			enabled_extensions.push_back(VK_EXT_HOST_QUERY_RESET_EXTENSION_NAME);
			*ppNext = &ext.host_query_reset_features;
			ppNext = &ext.host_query_reset_features.pNext;
		}

		if (has_extension(VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME))
		{
			enabled_extensions.push_back(VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME);
			*ppNext = &ext.demote_to_helper_invocation_features;
			ppNext = &ext.demote_to_helper_invocation_features.pNext;
		}

		if (has_extension(VK_EXT_SCALAR_BLOCK_LAYOUT_EXTENSION_NAME))
		{
			enabled_extensions.push_back(VK_EXT_SCALAR_BLOCK_LAYOUT_EXTENSION_NAME);
			*ppNext = &ext.scalar_block_features;
			ppNext = &ext.scalar_block_features.pNext;
		}

		if (has_extension(VK_KHR_UNIFORM_BUFFER_STANDARD_LAYOUT_EXTENSION_NAME))
		{
			enabled_extensions.push_back(VK_KHR_UNIFORM_BUFFER_STANDARD_LAYOUT_EXTENSION_NAME);
			*ppNext = &ext.ubo_std430_features;
			ppNext = &ext.ubo_std430_features.pNext;
		}

#ifdef VULKAN_DEBUG
		bool use_timeline_semaphore = force_no_validation;
		if (const char *use_timeline = getenv("GRANITE_VULKAN_FORCE_TIMELINE_SEMAPHORE"))
		{
			if (strtol(use_timeline, nullptr, 0) != 0)
				use_timeline_semaphore = true;
		}
#else
		constexpr bool use_timeline_semaphore = true;
#endif
		if (use_timeline_semaphore && has_extension(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME))
		{
			enabled_extensions.push_back(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
			*ppNext = &ext.timeline_semaphore_features;
			ppNext = &ext.timeline_semaphore_features.pNext;
		}

		if ((flags & CONTEXT_CREATION_DISABLE_BINDLESS_BIT) == 0 &&
		    ext.supports_maintenance_3 &&
		    has_extension(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME))
		{
			enabled_extensions.push_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
			*ppNext = &ext.descriptor_indexing_features;
			ppNext = &ext.descriptor_indexing_features.pNext;
		}

		if (has_extension(VK_KHR_PERFORMANCE_QUERY_EXTENSION_NAME))
		{
			enabled_extensions.push_back(VK_KHR_PERFORMANCE_QUERY_EXTENSION_NAME);
			*ppNext = &ext.performance_query_features;
			ppNext = &ext.performance_query_features.pNext;
		}

		if (ext.supports_bind_memory2 &&
		    ext.supports_get_memory_requirements2 &&
		    has_extension(VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME))
		{
			enabled_extensions.push_back(VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME);
			*ppNext = &ext.sampler_ycbcr_conversion_features;
			ppNext = &ext.sampler_ycbcr_conversion_features.pNext;
		}

#if 0
		if (has_extension(VK_KHR_IMAGELESS_FRAMEBUFFER_EXTENSION_NAME))
		{
			enabled_extensions.push_back(VK_KHR_IMAGELESS_FRAMEBUFFER_EXTENSION_NAME);
			*ppNext = &ext.imageless_features;
			ppNext = &ext.imageless_features.pNext;
		}
#endif
	}

	if (ext.supports_vulkan_11_device && ext.supports_vulkan_11_instance)
		vkGetPhysicalDeviceFeatures2(gpu, &features);
	else if (ext.supports_physical_device_properties2)
		vkGetPhysicalDeviceFeatures2KHR(gpu, &features);
	else
		vkGetPhysicalDeviceFeatures(gpu, &features.features);

	// Enable device features we might care about.
	{
		VkPhysicalDeviceFeatures enabled_features = *required_features;
		if (features.features.textureCompressionETC2)
			enabled_features.textureCompressionETC2 = VK_TRUE;
		if (features.features.textureCompressionBC)
			enabled_features.textureCompressionBC = VK_TRUE;
		if (features.features.textureCompressionASTC_LDR)
			enabled_features.textureCompressionASTC_LDR = VK_TRUE;
		if (features.features.fullDrawIndexUint32)
			enabled_features.fullDrawIndexUint32 = VK_TRUE;
		if (features.features.imageCubeArray)
			enabled_features.imageCubeArray = VK_TRUE;
		if (features.features.fillModeNonSolid)
			enabled_features.fillModeNonSolid = VK_TRUE;
		if (features.features.independentBlend)
			enabled_features.independentBlend = VK_TRUE;
		if (features.features.sampleRateShading)
			enabled_features.sampleRateShading = VK_TRUE;
		if (features.features.fragmentStoresAndAtomics)
			enabled_features.fragmentStoresAndAtomics = VK_TRUE;
		if (features.features.shaderStorageImageExtendedFormats)
			enabled_features.shaderStorageImageExtendedFormats = VK_TRUE;
		if (features.features.shaderStorageImageMultisample)
			enabled_features.shaderStorageImageMultisample = VK_TRUE;
		if (features.features.largePoints)
			enabled_features.largePoints = VK_TRUE;
		if (features.features.shaderInt16)
			enabled_features.shaderInt16 = VK_TRUE;
		if (features.features.shaderInt64)
			enabled_features.shaderInt64 = VK_TRUE;

		if (features.features.shaderSampledImageArrayDynamicIndexing)
			enabled_features.shaderSampledImageArrayDynamicIndexing = VK_TRUE;
		if (features.features.shaderUniformBufferArrayDynamicIndexing)
			enabled_features.shaderUniformBufferArrayDynamicIndexing = VK_TRUE;
		if (features.features.shaderStorageBufferArrayDynamicIndexing)
			enabled_features.shaderStorageBufferArrayDynamicIndexing = VK_TRUE;
		if (features.features.shaderStorageImageArrayDynamicIndexing)
			enabled_features.shaderStorageImageArrayDynamicIndexing = VK_TRUE;

		features.features = enabled_features;
		ext.enabled_features = enabled_features;
	}

	if (ext.supports_physical_device_properties2)
		device_info.pNext = &features;
	else
		device_info.pEnabledFeatures = &features.features;

#ifdef VULKAN_DEBUG
	if (!force_no_validation && has_layer("VK_LAYER_KHRONOS_validation"))
		enabled_layers.push_back("VK_LAYER_KHRONOS_validation");
	else if (!force_no_validation && has_layer("VK_LAYER_LUNARG_standard_validation"))
		enabled_layers.push_back("VK_LAYER_LUNARG_standard_validation");
#endif

	if (ext.supports_external && has_extension(VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME))
	{
		ext.supports_external_memory_host = true;
		enabled_extensions.push_back(VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME);
	}

	// Only need GetPhysicalDeviceProperties2 for Vulkan 1.1-only code, so don't bother getting KHR variant.
	ext.subgroup_properties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES };
	ext.host_memory_properties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT };
	ext.subgroup_size_control_properties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES_EXT };
	ext.descriptor_indexing_properties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES_EXT };
	ext.conservative_rasterization_properties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONSERVATIVE_RASTERIZATION_PROPERTIES_EXT };
	ext.driver_properties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES_KHR };
	VkPhysicalDeviceProperties2 props = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
	ppNext = &props.pNext;

	*ppNext = &ext.subgroup_properties;
	ppNext = &ext.subgroup_properties.pNext;

	if (ext.supports_external_memory_host)
	{
		*ppNext = &ext.host_memory_properties;
		ppNext = &ext.host_memory_properties.pNext;
	}

	if (has_extension(VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME))
	{
		*ppNext = &ext.subgroup_size_control_properties;
		ppNext = &ext.subgroup_size_control_properties.pNext;
	}

	if (ext.supports_maintenance_3 && has_extension(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME))
	{
		*ppNext = &ext.descriptor_indexing_properties;
		ppNext = &ext.descriptor_indexing_properties.pNext;
	}

	if (ext.supports_conservative_rasterization)
	{
		*ppNext = &ext.conservative_rasterization_properties;
		ppNext = &ext.conservative_rasterization_properties.pNext;
	}

	if (ext.supports_vulkan_11_instance && ext.supports_vulkan_11_device &&
	    has_extension(VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME))
	{
		enabled_extensions.push_back(VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME);
		ext.supports_driver_properties = true;
		*ppNext = &ext.driver_properties;
		ppNext = &ext.driver_properties.pNext;
	}

	if (ext.supports_vulkan_11_instance && ext.supports_vulkan_11_device)
		vkGetPhysicalDeviceProperties2(gpu, &props);

	device_info.enabledExtensionCount = enabled_extensions.size();
	device_info.ppEnabledExtensionNames = enabled_extensions.empty() ? nullptr : enabled_extensions.data();
	device_info.enabledLayerCount = enabled_layers.size();
	device_info.ppEnabledLayerNames = enabled_layers.empty() ? nullptr : enabled_layers.data();

	for (auto *enabled_extension : enabled_extensions)
		LOGI("Enabling device extension: %s.\n", enabled_extension);

	if (vkCreateDevice(gpu, &device_info, nullptr, &device) != VK_SUCCESS)
		return false;

	volkLoadDeviceTable(&device_table, device);
	device_table.vkGetDeviceQueue(device, graphics_queue_family, graphics_queue_index, &graphics_queue);
	device_table.vkGetDeviceQueue(device, compute_queue_family, compute_queue_index, &compute_queue);
	device_table.vkGetDeviceQueue(device, transfer_queue_family, transfer_queue_index, &transfer_queue);

	check_descriptor_indexing_features();

	return true;
}

void Context::check_descriptor_indexing_features()
{
	auto &f = ext.descriptor_indexing_features;
	if (f.descriptorBindingSampledImageUpdateAfterBind &&
	    f.descriptorBindingPartiallyBound &&
	    f.runtimeDescriptorArray &&
	    f.shaderSampledImageArrayNonUniformIndexing)
	{
		ext.supports_descriptor_indexing = true;
	}
}
}
