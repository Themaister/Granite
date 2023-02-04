/* Copyright (c) 2017-2023 Hans-Kristian Arntzen
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
#include "small_vector.hpp"
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

#if defined(ANDROID) && defined(HAVE_SWAPPY)
#include "swappy/swappyVk.h"
#endif

//#undef VULKAN_DEBUG

namespace Vulkan
{
void Context::set_instance_factory(InstanceFactory *factory)
{
	instance_factory = factory;
}

void Context::set_device_factory(DeviceFactory *factory)
{
	device_factory = factory;
}

void Context::set_application_info(const VkApplicationInfo *app_info)
{
	user_application_info.copy_assign(app_info);
	VK_ASSERT(!app_info || app_info->apiVersion >= VK_API_VERSION_1_1);
}

CopiedApplicationInfo::CopiedApplicationInfo()
{
	set_default_app();
}

void CopiedApplicationInfo::set_default_app()
{
	engine.clear();
	application.clear();
	app = {
		VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr,
		"Granite", 0, "Granite", 0,
		VK_API_VERSION_1_1,
	};
}

void CopiedApplicationInfo::copy_assign(const VkApplicationInfo *info)
{
	if (info)
	{
		app = *info;

		if (info->pApplicationName)
		{
			application = info->pApplicationName;
			app.pApplicationName = application.c_str();
		}
		else
			application.clear();

		if (info->pEngineName)
		{
			engine = info->pEngineName;
			app.pEngineName = engine.c_str();
		}
		else
			engine.clear();
	}
	else
	{
		set_default_app();
	}
}

const VkApplicationInfo &CopiedApplicationInfo::get_application_info() const
{
	return app;
}

bool Context::init_instance(const char * const *instance_ext, uint32_t instance_ext_count, ContextCreationFlags flags)
{
	destroy();

	owned_instance = true;
	if (!create_instance(instance_ext, instance_ext_count, flags))
	{
		destroy();
		LOGE("Failed to create Vulkan instance.\n");
		return false;
	}

	return true;
}

bool Context::init_device(VkPhysicalDevice gpu_, VkSurfaceKHR surface_compat, const char *const *device_ext,
                          uint32_t device_ext_count, ContextCreationFlags flags)
{
	owned_device = true;
	VkPhysicalDeviceFeatures features = {};
	if (!create_device(gpu_, surface_compat, device_ext, device_ext_count, &features, flags))
	{
		destroy();
		LOGE("Failed to create Vulkan device.\n");
		return false;
	}

	return true;
}

bool Context::init_instance_and_device(const char * const *instance_ext, uint32_t instance_ext_count,
                                       const char * const *device_ext, uint32_t device_ext_count,
                                       ContextCreationFlags flags)
{
	if (!init_instance(instance_ext, instance_ext_count, flags))
		return false;
	if (!init_device(VK_NULL_HANDLE, VK_NULL_HANDLE, device_ext, device_ext_count, flags))
		return false;
	return true;
}

static std::mutex loader_init_lock;
static bool loader_init_once;
static PFN_vkGetInstanceProcAddr instance_proc_addr;

PFN_vkGetInstanceProcAddr Context::get_instance_proc_addr()
{
	return instance_proc_addr;
}

bool Context::init_loader(PFN_vkGetInstanceProcAddr addr)
{
	std::lock_guard<std::mutex> holder(loader_init_lock);
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
			if (!module)
				module = dlopen("libMoltenVK.dylib", RTLD_LOCAL | RTLD_LAZY);
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

	instance_proc_addr = addr;
	volkInitializeCustom(addr);
	loader_init_once = true;
	return true;
}

bool Context::init_device_from_instance(VkInstance instance_, VkPhysicalDevice gpu_, VkSurfaceKHR surface,
                                        const char **required_device_extensions, unsigned num_required_device_extensions,
                                        const VkPhysicalDeviceFeatures *required_features,
                                        ContextCreationFlags flags)
{
	destroy();

	instance = instance_;
	owned_instance = false;
	owned_device = true;

	if (!create_instance(nullptr, 0, flags))
		return false;

	if (!create_device(gpu_, surface, required_device_extensions, num_required_device_extensions, required_features, flags))
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
	if (debug_messenger)
		vkDestroyDebugUtilsMessengerEXT(instance, debug_messenger, nullptr);
	debug_messenger = VK_NULL_HANDLE;
#endif

#if defined(ANDROID) && defined(HAVE_SWAPPY)
	if (device != VK_NULL_HANDLE)
		SwappyVk_destroyDevice(device);
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

const VkApplicationInfo &Context::get_application_info() const
{
	return user_application_info.get_application_info();
}

void Context::notify_validation_error(const char *msg)
{
	if (message_callback)
		message_callback(msg);
}

void Context::set_notification_callback(std::function<void(const char *)> func)
{
	message_callback = std::move(func);
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
#endif

bool Context::create_instance(const char * const *instance_ext, uint32_t instance_ext_count, ContextCreationFlags flags)
{
	uint32_t target_instance_version = user_application_info.get_application_info().apiVersion;

	// Target an instance version of at least 1.3 for FFmpeg decode.
	if (flags & CONTEXT_CREATION_ENABLE_VIDEO_DECODE_BIT)
		if (target_instance_version < VK_API_VERSION_1_3)
			target_instance_version = VK_API_VERSION_1_3;

	if (volkGetInstanceVersion() < target_instance_version)
	{
		LOGE("Vulkan loader does not support target Vulkan version.\n");
		return false;
	}

	VkInstanceCreateInfo info = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
	auto app_info = get_application_info();
	if (app_info.apiVersion < target_instance_version)
		app_info.apiVersion = target_instance_version;
	info.pApplicationInfo = &app_info;

	std::vector<const char *> instance_exts;
	std::vector<const char *> instance_layers;
	for (uint32_t i = 0; i < instance_ext_count; i++)
		instance_exts.push_back(instance_ext[i]);

	uint32_t ext_count = 0;
	vkEnumerateInstanceExtensionProperties(nullptr, &ext_count, nullptr);
	std::vector<VkExtensionProperties> queried_extensions(ext_count);
	if (ext_count)
		vkEnumerateInstanceExtensionProperties(nullptr, &ext_count, queried_extensions.data());

	uint32_t layer_count = 0;
	vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
	std::vector<VkLayerProperties> queried_layers(layer_count);
	if (layer_count)
		vkEnumerateInstanceLayerProperties(&layer_count, queried_layers.data());

	LOGI("Layer count: %u\n", layer_count);
	for (auto &layer : queried_layers)
		LOGI("Found layer: %s.\n", layer.layerName);

	const auto has_extension = [&](const char *name) -> bool {
		auto itr = find_if(begin(queried_extensions), end(queried_extensions), [name](const VkExtensionProperties &e) -> bool {
			return strcmp(e.extensionName, name) == 0;
		});
		return itr != end(queried_extensions);
	};

	for (uint32_t i = 0; i < instance_ext_count; i++)
		if (!has_extension(instance_ext[i]))
			return false;

	if (has_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
	{
		instance_exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		ext.supports_debug_utils = true;
	}

	auto itr = std::find_if(instance_ext, instance_ext + instance_ext_count, [](const char *name) {
		return strcmp(name, VK_KHR_SURFACE_EXTENSION_NAME) == 0;
	});
	bool has_surface_extension = itr != (instance_ext + instance_ext_count);

	if (has_surface_extension && has_extension(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME))
	{
		instance_exts.push_back(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
		ext.supports_surface_capabilities2 = true;
	}

	if (ext.supports_surface_capabilities2 && has_extension(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME))
	{
		instance_exts.push_back(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
		ext.supports_surface_maintenance1 = true;
	}

	if ((flags & CONTEXT_CREATION_ENABLE_ADVANCED_WSI_BIT) != 0 &&
	    has_surface_extension &&
	    has_extension(VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME))
	{
		instance_exts.push_back(VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME);
		ext.supports_swapchain_colorspace = true;
	}

#ifdef VULKAN_DEBUG
	const auto has_layer = [&](const char *name) -> bool {
		auto layer_itr = find_if(begin(queried_layers), end(queried_layers), [name](const VkLayerProperties &e) -> bool {
			return strcmp(e.layerName, name) == 0;
		});
		return layer_itr != end(queried_layers);
	};

	VkValidationFeaturesEXT validation_features = { VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT };

	if (getenv("GRANITE_VULKAN_NO_VALIDATION"))
		force_no_validation = true;

	if (!force_no_validation && has_layer("VK_LAYER_KHRONOS_validation"))
	{
		instance_layers.push_back("VK_LAYER_KHRONOS_validation");
		LOGI("Enabling VK_LAYER_KHRONOS_validation.\n");

		uint32_t layer_ext_count = 0;
		vkEnumerateInstanceExtensionProperties("VK_LAYER_KHRONOS_validation", &layer_ext_count, nullptr);
		std::vector<VkExtensionProperties> layer_exts(layer_ext_count);
		vkEnumerateInstanceExtensionProperties("VK_LAYER_KHRONOS_validation", &layer_ext_count, layer_exts.data());

		if (find_if(begin(layer_exts), end(layer_exts), [](const VkExtensionProperties &e) {
			return strcmp(e.extensionName, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME) == 0;
		}) != end(layer_exts))
		{
			instance_exts.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
			static const VkValidationFeatureEnableEXT validation_sync_features[1] = {
				VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
			};
			LOGI("Enabling VK_EXT_validation_features for synchronization validation.\n");
			validation_features.enabledValidationFeatureCount = 1;
			validation_features.pEnabledValidationFeatures = validation_sync_features;
			info.pNext = &validation_features;
		}

		if (!ext.supports_debug_utils &&
		    find_if(begin(layer_exts), end(layer_exts), [](const VkExtensionProperties &e) {
			    return strcmp(e.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0;
		    }) != end(layer_exts))
		{
			instance_exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
			ext.supports_debug_utils = true;
		}
	}
#endif

	info.enabledExtensionCount = instance_exts.size();
	info.ppEnabledExtensionNames = instance_exts.empty() ? nullptr : instance_exts.data();
	info.enabledLayerCount = instance_layers.size();
	info.ppEnabledLayerNames = instance_layers.empty() ? nullptr : instance_layers.data();

	for (auto *ext_name : instance_exts)
		LOGI("Enabling instance extension: %s.\n", ext_name);

	// instance != VK_NULL_HANDLE here is deprecated and somewhat broken.
	// For libretro Vulkan context negotiation v1.
	if (instance == VK_NULL_HANDLE)
	{
		if (instance_factory)
		{
			instance = instance_factory->create_instance(&info);
			if (instance == VK_NULL_HANDLE)
				return false;
		}
		else if (vkCreateInstance(&info, nullptr, &instance) != VK_SUCCESS)
			return false;
	}

	enabled_instance_extensions = std::move(instance_exts);
	ext.instance_extensions = enabled_instance_extensions.data();
	ext.num_instance_extensions = uint32_t(enabled_instance_extensions.size());

	volkLoadInstance(instance);

#if defined(VULKAN_DEBUG)
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

		// For some reason, this segfaults Android, sigh ... We get relevant output in logcat anyways.
		if (vkCreateDebugUtilsMessengerEXT)
			vkCreateDebugUtilsMessengerEXT(instance, &debug_info, nullptr, &debug_messenger);
	}
#endif

	return true;
}

static unsigned device_score(VkPhysicalDevice &gpu)
{
	VkPhysicalDeviceProperties props = {};
	vkGetPhysicalDeviceProperties(gpu, &props);

	if (props.apiVersion < VK_API_VERSION_1_1)
		return 0;

	switch (props.deviceType)
	{
	case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
		return 3;
	case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
		return 2;
	case VK_PHYSICAL_DEVICE_TYPE_CPU:
		return 1;
	default:
		return 0;
	}
}

QueueInfo::QueueInfo()
{
	for (auto &index : family_indices)
		index = VK_QUEUE_FAMILY_IGNORED;
}

bool Context::physical_device_supports_surface(VkPhysicalDevice gpu, VkSurfaceKHR surface)
{
	if (surface == VK_NULL_HANDLE)
		return true;

	uint32_t family_count = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(gpu, &family_count, nullptr);
	Util::SmallVector<VkQueueFamilyProperties> props(family_count);
	vkGetPhysicalDeviceQueueFamilyProperties(gpu, &family_count, props.data());

	for (uint32_t i = 0; i < family_count; i++)
	{
		// A graphics queue candidate must support present for us to select it.
		if ((props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
		{
			VkBool32 supported = VK_FALSE;
			if (vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface, &supported) == VK_SUCCESS && supported)
				return true;
		}
	}

	return false;
}

bool Context::create_device(VkPhysicalDevice gpu_, VkSurfaceKHR surface,
                            const char * const *required_device_extensions, uint32_t num_required_device_extensions,
                            const VkPhysicalDeviceFeatures *required_features,
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

		std::vector<VkPhysicalDevice> gpus(gpu_count);
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

		if (gpu != VK_NULL_HANDLE)
		{
			if (!physical_device_supports_surface(gpu, surface))
			{
				LOGE("Selected physical device which does not support surface.\n");
				gpu = VK_NULL_HANDLE;
			}
		}

		if (gpu == VK_NULL_HANDLE)
		{
			unsigned max_score = 0;
			// Prefer earlier entries in list.
			for (size_t i = gpus.size(); i; i--)
			{
				unsigned score = device_score(gpus[i - 1]);
				if (score >= max_score && physical_device_supports_surface(gpus[i - 1], surface))
				{
					max_score = score;
					gpu = gpus[i - 1];
				}
			}
		}

		if (gpu == VK_NULL_HANDLE)
		{
			LOGE("Found not GPU which supports surface.\n");
			return false;
		}
	}
	else if (!physical_device_supports_surface(gpu, surface))
	{
		LOGE("Selected physical device does not support surface.\n");
		return false;
	}

	uint32_t ext_count = 0;
	vkEnumerateDeviceExtensionProperties(gpu, nullptr, &ext_count, nullptr);
	std::vector<VkExtensionProperties> queried_extensions(ext_count);
	if (ext_count)
		vkEnumerateDeviceExtensionProperties(gpu, nullptr, &ext_count, queried_extensions.data());

	const auto has_extension = [&](const char *name) -> bool {
		auto itr = find_if(begin(queried_extensions), end(queried_extensions), [name](const VkExtensionProperties &e) -> bool {
			return strcmp(e.extensionName, name) == 0;
		});
		return itr != end(queried_extensions);
	};

	for (uint32_t i = 0; i < num_required_device_extensions; i++)
		if (!has_extension(required_device_extensions[i]))
			return false;

	VkPhysicalDeviceProperties2 gpu_props2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
	if (has_extension(VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME))
	{
		ext.supports_driver_properties = true;
		ext.driver_properties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES_KHR };
		gpu_props2.pNext = &ext.driver_properties;
	}

	vkGetPhysicalDeviceProperties2(gpu, &gpu_props2);
	gpu_props = gpu_props2.properties;
	LOGI("Using Vulkan GPU: %s\n", gpu_props.deviceName);

	// FFmpeg integration requires Vulkan 1.3 core for physical device.
	const uint32_t minimum_api_version =
			(flags & CONTEXT_CREATION_ENABLE_VIDEO_DECODE_BIT) ?
			VK_API_VERSION_1_3 : VK_API_VERSION_1_1;

	if (gpu_props.apiVersion < minimum_api_version)
	{
		LOGE("Found no Vulkan GPU which supports Vulkan 1.%u.\n",
		     VK_API_VERSION_MINOR(minimum_api_version));
		return false;
	}

	vkGetPhysicalDeviceMemoryProperties(gpu, &mem_props);

	uint32_t queue_family_count = 0;
	vkGetPhysicalDeviceQueueFamilyProperties2(gpu, &queue_family_count, nullptr);
	Util::SmallVector<VkQueueFamilyProperties2> queue_props(queue_family_count);
	Util::SmallVector<VkQueueFamilyVideoPropertiesKHR> video_queue_props2(queue_family_count);

	if ((flags & CONTEXT_CREATION_ENABLE_VIDEO_DECODE_BIT) != 0 &&
	    has_extension(VK_KHR_VIDEO_QUEUE_EXTENSION_NAME))
	{
		ext.supports_video_queue = true;
	}

	for (uint32_t i = 0; i < queue_family_count; i++)
	{
		queue_props[i].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
		if (ext.supports_video_queue)
		{
			queue_props[i].pNext = &video_queue_props2[i];
			video_queue_props2[i].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_VIDEO_PROPERTIES_KHR;
		}
	}

	Util::SmallVector<uint32_t> queue_offsets(queue_family_count);
	Util::SmallVector<Util::SmallVector<float, QUEUE_INDEX_COUNT>> queue_priorities(queue_family_count);
	vkGetPhysicalDeviceQueueFamilyProperties2(gpu, &queue_family_count, queue_props.data());

	queue_info = {};
	uint32_t queue_indices[QUEUE_INDEX_COUNT] = {};

	const auto find_vacant_queue = [&](uint32_t &family, uint32_t &index,
	                                   VkQueueFlags required, VkQueueFlags ignore_flags,
	                                   float priority) -> bool {
		for (unsigned family_index = 0; family_index < queue_family_count; family_index++)
		{
			if ((queue_props[family_index].queueFamilyProperties.queueFlags & ignore_flags) != 0)
				continue;

			// A graphics queue candidate must support present for us to select it.
			if ((required & VK_QUEUE_GRAPHICS_BIT) != 0 && surface != VK_NULL_HANDLE)
			{
				VkBool32 supported = VK_FALSE;
				if (vkGetPhysicalDeviceSurfaceSupportKHR(gpu, family_index, surface, &supported) != VK_SUCCESS || !supported)
					continue;
			}

			if (queue_props[family_index].queueFamilyProperties.queueCount &&
			    (queue_props[family_index].queueFamilyProperties.queueFlags & required) == required)
			{
				family = family_index;
				queue_props[family_index].queueFamilyProperties.queueCount--;
				index = queue_offsets[family_index]++;
				queue_priorities[family_index].push_back(priority);
				return true;
			}
		}

		return false;
	};

	if (!find_vacant_queue(queue_info.family_indices[QUEUE_INDEX_GRAPHICS],
	                       queue_indices[QUEUE_INDEX_GRAPHICS],
	                       VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT, 0, 0.5f))
	{
		LOGE("Could not find suitable graphics queue.\n");
		return false;
	}

	// XXX: This assumes timestamp valid bits is the same for all queue types.
	queue_info.timestamp_valid_bits =
			queue_props[queue_info.family_indices[QUEUE_INDEX_GRAPHICS]].queueFamilyProperties.timestampValidBits;

	// Driver ends up interleaving GPU work in very bizarre ways, causing horrible GPU
	// bubbles and completely broken pacing. Single queue works around it.
	bool broken_async_queues =
			ext.supports_driver_properties &&
			ext.driver_properties.driverID == VK_DRIVER_ID_SAMSUNG_PROPRIETARY;

	if (broken_async_queues)
		LOGW("Working around broken scheduler for separate compute queues, forcing single GRAPHICS + COMPUTE queue.\n");

	// Prefer another graphics queue since we can do async graphics that way.
	// The compute queue is to be treated as high priority since we also do async graphics on it.
	if (broken_async_queues ||
	    (!find_vacant_queue(queue_info.family_indices[QUEUE_INDEX_COMPUTE], queue_indices[QUEUE_INDEX_COMPUTE],
	                        VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT, 0, 1.0f) &&
	     !find_vacant_queue(queue_info.family_indices[QUEUE_INDEX_COMPUTE], queue_indices[QUEUE_INDEX_COMPUTE],
	                        VK_QUEUE_COMPUTE_BIT, 0, 1.0f)))
	{
		// Fallback to the graphics queue if we must.
		queue_info.family_indices[QUEUE_INDEX_COMPUTE] = queue_info.family_indices[QUEUE_INDEX_GRAPHICS];
		queue_indices[QUEUE_INDEX_COMPUTE] = queue_indices[QUEUE_INDEX_GRAPHICS];
	}

	// For transfer, try to find a queue which only supports transfer, e.g. DMA queue.
	// If not, fallback to a dedicated compute queue.
	// Finally, fallback to same queue as compute.
	if (!find_vacant_queue(queue_info.family_indices[QUEUE_INDEX_TRANSFER], queue_indices[QUEUE_INDEX_TRANSFER],
	                       VK_QUEUE_TRANSFER_BIT, VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT, 0.5f) &&
	    !find_vacant_queue(queue_info.family_indices[QUEUE_INDEX_TRANSFER], queue_indices[QUEUE_INDEX_TRANSFER],
	                       VK_QUEUE_COMPUTE_BIT, VK_QUEUE_GRAPHICS_BIT, 0.5f))
	{
		queue_info.family_indices[QUEUE_INDEX_TRANSFER] = queue_info.family_indices[QUEUE_INDEX_COMPUTE];
		queue_indices[QUEUE_INDEX_TRANSFER] = queue_indices[QUEUE_INDEX_COMPUTE];
	}

	if (ext.supports_video_queue)
	{
		if (!find_vacant_queue(queue_info.family_indices[QUEUE_INDEX_VIDEO_DECODE], queue_indices[QUEUE_INDEX_VIDEO_DECODE],
		                       VK_QUEUE_VIDEO_DECODE_BIT_KHR, 0, 0.5f))
		{
			queue_info.family_indices[QUEUE_INDEX_VIDEO_DECODE] = VK_QUEUE_FAMILY_IGNORED;
			queue_indices[QUEUE_INDEX_VIDEO_DECODE] = UINT32_MAX;
		}
	}

	VkDeviceCreateInfo device_info = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };

	Util::SmallVector<VkDeviceQueueCreateInfo> queue_infos;
	for (uint32_t family_index = 0; family_index < queue_family_count; family_index++)
	{
		if (queue_offsets[family_index] == 0)
			continue;

		VkDeviceQueueCreateInfo info = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
		info.queueFamilyIndex = family_index;
		info.queueCount = queue_offsets[family_index];
		info.pQueuePriorities = queue_priorities[family_index].data();
		queue_infos.push_back(info);
	}
	device_info.pQueueCreateInfos = queue_infos.data();
	device_info.queueCreateInfoCount = uint32_t(queue_infos.size());

	std::vector<const char *> enabled_extensions;

	bool requires_swapchain = false;
	for (uint32_t i = 0; i < num_required_device_extensions; i++)
	{
		enabled_extensions.push_back(required_device_extensions[i]);
		if (strcmp(required_device_extensions[i], VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
			requires_swapchain = true;
		else if (strcmp(required_device_extensions[i], VK_KHR_PRESENT_ID_EXTENSION_NAME) == 0 ||
		         strcmp(required_device_extensions[i], VK_KHR_PRESENT_WAIT_EXTENSION_NAME) == 0 ||
		         strcmp(required_device_extensions[i], VK_EXT_HDR_METADATA_EXTENSION_NAME) == 0 ||
		         strcmp(required_device_extensions[i], VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME) == 0)
		{
			flags |= CONTEXT_CREATION_ENABLE_ADVANCED_WSI_BIT;
		}
		else if (strcmp(required_device_extensions[i], VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME) == 0)
		{
			flags &= ~CONTEXT_CREATION_DISABLE_BINDLESS_BIT;
		}
	}

#if defined(ANDROID) && defined(HAVE_SWAPPY)
	// Enable additional extensions required by SwappyVk.
	std::unique_ptr<char[]> swappy_str_buffer;
	if (requires_swapchain)
	{
		uint32_t required_swappy_extension_count = 0;

		// I'm really not sure why the API just didn't return static const char * strings here,
		// but oh well.
		SwappyVk_determineDeviceExtensions(gpu, uint32_t(queried_extensions.size()),
		                                   queried_extensions.data(),
		                                   &required_swappy_extension_count,
		                                   nullptr);
		swappy_str_buffer.reset(new char[required_swappy_extension_count * (VK_MAX_EXTENSION_NAME_SIZE + 1)]);

		std::vector<char *> extension_buffer;
		extension_buffer.reserve(required_swappy_extension_count);
		for (uint32_t i = 0; i < required_swappy_extension_count; i++)
			extension_buffer.push_back(swappy_str_buffer.get() + i * (VK_MAX_EXTENSION_NAME_SIZE + 1));
		SwappyVk_determineDeviceExtensions(gpu, uint32_t(queried_extensions.size()),
		                                   queried_extensions.data(),
		                                   &required_swappy_extension_count,
		                                   extension_buffer.data());

		for (auto *required_ext : extension_buffer)
			enabled_extensions.push_back(required_ext);
	}
#endif

	if (has_extension(VK_KHR_SAMPLER_MIRROR_CLAMP_TO_EDGE_EXTENSION_NAME))
	{
		ext.supports_mirror_clamp_to_edge = true;
		enabled_extensions.push_back(VK_KHR_SAMPLER_MIRROR_CLAMP_TO_EDGE_EXTENSION_NAME);
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

	if (
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

	if (has_extension(VK_KHR_DRAW_INDIRECT_COUNT_EXTENSION_NAME))
	{
		ext.supports_draw_indirect_count = true;
		enabled_extensions.push_back(VK_KHR_DRAW_INDIRECT_COUNT_EXTENSION_NAME);
	}

	if (has_extension(VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME))
	{
		ext.supports_calibrated_timestamps = true;
		enabled_extensions.push_back(VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME);
	}

	if (has_extension(VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME))
	{
		enabled_extensions.push_back(VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME);
		ext.supports_conservative_rasterization = true;
	}

	if (has_extension(VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME))
	{
		enabled_extensions.push_back(VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME);
		ext.supports_image_format_list = true;
	}

	if (has_extension(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME))
	{
		enabled_extensions.push_back(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);
		ext.supports_shader_float_control = true;
	}

	if (has_extension(VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME))
	{
		ext.supports_create_renderpass2 = true;
		enabled_extensions.push_back(VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME);
	}
	else
	{
		LOGE("VK_KHR_create_renderpass2 is not supported.\n");
		return false;
	}

	if (has_extension(VK_EXT_TOOLING_INFO_EXTENSION_NAME))
		ext.supports_tooling_info = true;

	if (ext.supports_video_queue)
	{
		enabled_extensions.push_back(VK_KHR_VIDEO_QUEUE_EXTENSION_NAME);

		if ((flags & CONTEXT_CREATION_ENABLE_VIDEO_DECODE_BIT) != 0 &&
		    has_extension(VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME))
		{
			enabled_extensions.push_back(VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME);
			ext.supports_video_decode_queue = true;

			if ((flags & CONTEXT_CREATION_ENABLE_VIDEO_H264_BIT) != 0 &&
			    has_extension(VK_KHR_VIDEO_DECODE_H264_EXTENSION_NAME))
			{
				enabled_extensions.push_back(VK_KHR_VIDEO_DECODE_H264_EXTENSION_NAME);

				if (queue_info.family_indices[QUEUE_INDEX_VIDEO_DECODE] != VK_QUEUE_FAMILY_IGNORED)
				{
					ext.supports_video_decode_h264 =
							(video_queue_props2[queue_info.family_indices[QUEUE_INDEX_VIDEO_DECODE]].videoCodecOperations &
							 VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR) != 0;
				}
			}

			if ((flags & CONTEXT_CREATION_ENABLE_VIDEO_H265_BIT) != 0 &&
			    has_extension(VK_KHR_VIDEO_DECODE_H265_EXTENSION_NAME))
			{
				enabled_extensions.push_back(VK_KHR_VIDEO_DECODE_H265_EXTENSION_NAME);

				if (queue_info.family_indices[QUEUE_INDEX_VIDEO_DECODE] != VK_QUEUE_FAMILY_IGNORED)
				{
					ext.supports_video_decode_h265 =
							(video_queue_props2[queue_info.family_indices[QUEUE_INDEX_VIDEO_DECODE]].videoCodecOperations &
							 VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR) != 0;
				}
			}
		}
	}

	pdf2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };

	ext.multiview_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES };
	ext.sampler_ycbcr_conversion_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES };
	ext.shader_draw_parameters_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES };

	ext.storage_8bit_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES_KHR };
	ext.storage_16bit_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES_KHR };
	ext.float16_int8_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT16_INT8_FEATURES_KHR };
	ext.ubo_std430_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFORM_BUFFER_STANDARD_LAYOUT_FEATURES_KHR };
	ext.timeline_semaphore_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES_KHR };
	ext.sync2_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR };
	ext.present_id_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR };
	ext.present_wait_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR };
	ext.performance_query_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PERFORMANCE_QUERY_FEATURES_KHR };
	ext.swapchain_maintenance1_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT };

	ext.subgroup_size_control_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES_EXT };
	ext.host_query_reset_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES_EXT };
	ext.demote_to_helper_invocation_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES_EXT };
	ext.scalar_block_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES_EXT };
	ext.descriptor_indexing_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT };
	ext.memory_priority_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PRIORITY_FEATURES_EXT };
	ext.astc_decode_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ASTC_DECODE_FEATURES_EXT };
	ext.astc_hdr_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXTURE_COMPRESSION_ASTC_HDR_FEATURES_EXT };
	ext.pipeline_creation_cache_control_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_CREATION_CACHE_CONTROL_FEATURES_EXT };

	ext.compute_shader_derivative_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_NV };

	void **ppNext = &pdf2.pNext;

	*ppNext = &ext.multiview_features;
	ppNext = &ext.multiview_features.pNext;
	*ppNext = &ext.sampler_ycbcr_conversion_features;
	ppNext = &ext.sampler_ycbcr_conversion_features.pNext;
	*ppNext = &ext.shader_draw_parameters_features;
	ppNext = &ext.shader_draw_parameters_features.pNext;

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

	if (has_extension(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME))
	{
		enabled_extensions.push_back(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
		*ppNext = &ext.timeline_semaphore_features;
		ppNext = &ext.timeline_semaphore_features.pNext;
	}

	if ((flags & CONTEXT_CREATION_DISABLE_BINDLESS_BIT) == 0 && has_extension(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME))
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

	if (has_extension(VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME))
	{
		enabled_extensions.push_back(VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME);
		*ppNext = &ext.memory_priority_features;
		ppNext = &ext.memory_priority_features.pNext;
	}

	if (has_extension(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME))
	{
		enabled_extensions.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
		ext.supports_memory_budget = true;
	}

	if (has_extension(VK_EXT_ASTC_DECODE_MODE_EXTENSION_NAME))
	{
		ext.supports_astc_decode_mode = true;
		enabled_extensions.push_back(VK_EXT_ASTC_DECODE_MODE_EXTENSION_NAME);
		*ppNext = &ext.astc_decode_features;
		ppNext = &ext.astc_decode_features.pNext;
	}

	if (has_extension(VK_EXT_TEXTURE_COMPRESSION_ASTC_HDR_EXTENSION_NAME))
	{
		enabled_extensions.push_back(VK_EXT_TEXTURE_COMPRESSION_ASTC_HDR_EXTENSION_NAME);
		*ppNext = &ext.astc_hdr_features;
		ppNext = &ext.astc_hdr_features.pNext;
	}

	if (has_extension(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME))
	{
		ext.supports_sync2 = true;
		enabled_extensions.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
		*ppNext = &ext.sync2_features;
		ppNext = &ext.sync2_features.pNext;
	}

	if (has_extension(VK_EXT_PIPELINE_CREATION_CACHE_CONTROL_EXTENSION_NAME))
	{
		ext.supports_pipeline_creation_cache_control = true;
		enabled_extensions.push_back(VK_EXT_PIPELINE_CREATION_CACHE_CONTROL_EXTENSION_NAME);
		*ppNext = &ext.pipeline_creation_cache_control_features;
		ppNext = &ext.pipeline_creation_cache_control_features.pNext;
	}

	if (has_extension(VK_KHR_FORMAT_FEATURE_FLAGS_2_EXTENSION_NAME))
	{
		ext.supports_format_feature_flags2 = true;
		enabled_extensions.push_back(VK_KHR_FORMAT_FEATURE_FLAGS_2_EXTENSION_NAME);
	}

	if ((flags & CONTEXT_CREATION_ENABLE_ADVANCED_WSI_BIT) != 0 && requires_swapchain)
	{
		bool broken_present_wait = ext.driver_properties.driverID == VK_DRIVER_ID_NVIDIA_PROPRIETARY &&
		                           VK_VERSION_MAJOR(gpu_props.driverVersion) == 525;

		if (broken_present_wait)
		{
			LOGW("Disabling present_wait due to broken driver.\n");
		}
		else
		{
			if (has_extension(VK_KHR_PRESENT_ID_EXTENSION_NAME))
			{
				enabled_extensions.push_back(VK_KHR_PRESENT_ID_EXTENSION_NAME);
				*ppNext = &ext.present_id_features;
				ppNext = &ext.present_id_features.pNext;
			}

			if (has_extension(VK_KHR_PRESENT_WAIT_EXTENSION_NAME))
			{
				enabled_extensions.push_back(VK_KHR_PRESENT_WAIT_EXTENSION_NAME);
				*ppNext = &ext.present_wait_features;
				ppNext = &ext.present_wait_features.pNext;
			}
		}

		if (ext.supports_surface_maintenance1 && has_extension(VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME))
		{
			enabled_extensions.push_back(VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);
			*ppNext = &ext.swapchain_maintenance1_features;
			ppNext = &ext.swapchain_maintenance1_features.pNext;
		}

		if (ext.supports_swapchain_colorspace && has_extension(VK_EXT_HDR_METADATA_EXTENSION_NAME))
		{
			ext.supports_hdr_metadata = true;
			enabled_extensions.push_back(VK_EXT_HDR_METADATA_EXTENSION_NAME);
		}
	}

	vkGetPhysicalDeviceFeatures2(gpu, &pdf2);

	// Enable device features we might care about.
	{
		VkPhysicalDeviceFeatures enabled_features = *required_features;
		if (pdf2.features.textureCompressionETC2)
			enabled_features.textureCompressionETC2 = VK_TRUE;
		if (pdf2.features.textureCompressionBC)
			enabled_features.textureCompressionBC = VK_TRUE;
		if (pdf2.features.textureCompressionASTC_LDR)
			enabled_features.textureCompressionASTC_LDR = VK_TRUE;
		if (pdf2.features.fullDrawIndexUint32)
			enabled_features.fullDrawIndexUint32 = VK_TRUE;
		if (pdf2.features.imageCubeArray)
			enabled_features.imageCubeArray = VK_TRUE;
		if (pdf2.features.fillModeNonSolid)
			enabled_features.fillModeNonSolid = VK_TRUE;
		if (pdf2.features.independentBlend)
			enabled_features.independentBlend = VK_TRUE;
		if (pdf2.features.sampleRateShading)
			enabled_features.sampleRateShading = VK_TRUE;
		if (pdf2.features.fragmentStoresAndAtomics)
			enabled_features.fragmentStoresAndAtomics = VK_TRUE;
		if (pdf2.features.shaderStorageImageExtendedFormats)
			enabled_features.shaderStorageImageExtendedFormats = VK_TRUE;
		if (pdf2.features.shaderStorageImageMultisample)
			enabled_features.shaderStorageImageMultisample = VK_TRUE;
		if (pdf2.features.largePoints)
			enabled_features.largePoints = VK_TRUE;
		if (pdf2.features.shaderInt16)
			enabled_features.shaderInt16 = VK_TRUE;
		if (pdf2.features.shaderInt64)
			enabled_features.shaderInt64 = VK_TRUE;
		if (pdf2.features.shaderStorageImageWriteWithoutFormat)
			enabled_features.shaderStorageImageWriteWithoutFormat = VK_TRUE;
		if (pdf2.features.shaderStorageImageReadWithoutFormat)
			enabled_features.shaderStorageImageReadWithoutFormat = VK_TRUE;

		if (pdf2.features.shaderSampledImageArrayDynamicIndexing)
			enabled_features.shaderSampledImageArrayDynamicIndexing = VK_TRUE;
		if (pdf2.features.shaderUniformBufferArrayDynamicIndexing)
			enabled_features.shaderUniformBufferArrayDynamicIndexing = VK_TRUE;
		if (pdf2.features.shaderStorageBufferArrayDynamicIndexing)
			enabled_features.shaderStorageBufferArrayDynamicIndexing = VK_TRUE;
		if (pdf2.features.shaderStorageImageArrayDynamicIndexing)
			enabled_features.shaderStorageImageArrayDynamicIndexing = VK_TRUE;
		if (pdf2.features.shaderImageGatherExtended)
			enabled_features.shaderImageGatherExtended = VK_TRUE;

		if (pdf2.features.samplerAnisotropy)
			enabled_features.samplerAnisotropy = VK_TRUE;

		pdf2.features = enabled_features;
		ext.enabled_features = enabled_features;
	}

	device_info.pNext = &pdf2;

	if (ext.supports_external && has_extension(VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME))
	{
		ext.supports_external_memory_host = true;
		enabled_extensions.push_back(VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME);
	}

	// Only need GetPhysicalDeviceProperties2 for Vulkan 1.1-only code, so don't bother getting KHR variant.
	VkPhysicalDeviceProperties2 props = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
	ext.subgroup_properties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES };
	ext.multiview_properties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES };

	ext.host_memory_properties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT };
	ext.subgroup_size_control_properties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES_EXT };
	ext.descriptor_indexing_properties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES_EXT };
	ext.conservative_rasterization_properties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONSERVATIVE_RASTERIZATION_PROPERTIES_EXT };
	ext.float_control_properties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES_KHR };
	ext.id_properties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES };

	ppNext = &props.pNext;

	*ppNext = &ext.subgroup_properties;
	ppNext = &ext.subgroup_properties.pNext;
	*ppNext = &ext.multiview_properties;
	ppNext = &ext.multiview_properties.pNext;

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

	if (has_extension(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME))
	{
		*ppNext = &ext.descriptor_indexing_properties;
		ppNext = &ext.descriptor_indexing_properties.pNext;
	}

	if (ext.supports_conservative_rasterization)
	{
		*ppNext = &ext.conservative_rasterization_properties;
		ppNext = &ext.conservative_rasterization_properties.pNext;
	}

	if (ext.supports_shader_float_control)
	{
		*ppNext = &ext.float_control_properties;
		ppNext = &ext.float_control_properties.pNext;
	}

	if (ext.supports_external)
	{
		*ppNext = &ext.id_properties;
		ppNext = &ext.id_properties.pNext;
	}

	vkGetPhysicalDeviceProperties2(gpu, &props);

	device_info.enabledExtensionCount = enabled_extensions.size();
	device_info.ppEnabledExtensionNames = enabled_extensions.empty() ? nullptr : enabled_extensions.data();

	for (auto *enabled_extension : enabled_extensions)
		LOGI("Enabling device extension: %s.\n", enabled_extension);

	if (device_factory)
	{
		device = device_factory->create_device(gpu, &device_info);
		if (device == VK_NULL_HANDLE)
			return false;
	}
	else if (vkCreateDevice(gpu, &device_info, nullptr, &device) != VK_SUCCESS)
		return false;

	enabled_device_extensions = std::move(enabled_extensions);
	ext.device_extensions = enabled_device_extensions.data();
	ext.num_device_extensions = uint32_t(enabled_device_extensions.size());
	ext.pdf2 = &pdf2;

#ifdef GRANITE_VULKAN_FOSSILIZE
	feature_filter.init(user_application_info.get_application_info().apiVersion,
	                    enabled_device_extensions.data(),
	                    device_info.enabledExtensionCount,
	                    &pdf2, &props);
	feature_filter.set_device_query_interface(this);
#endif

	volkLoadDeviceTable(&device_table, device);

	for (int i = 0; i < QUEUE_INDEX_COUNT; i++)
	{
		if (queue_info.family_indices[i] != VK_QUEUE_FAMILY_IGNORED)
		{
			device_table.vkGetDeviceQueue(device, queue_info.family_indices[i], queue_indices[i],
			                              &queue_info.queues[i]);

			queue_info.counts[i] = queue_offsets[queue_info.family_indices[i]];

#if defined(ANDROID) && defined(HAVE_SWAPPY)
			SwappyVk_setQueueFamilyIndex(device, queue_info.queues[i], queue_info.family_indices[i]);
#endif
		}
		else
		{
			queue_info.queues[i] = VK_NULL_HANDLE;
		}
	}

#ifdef VULKAN_DEBUG
	static const char *family_names[QUEUE_INDEX_COUNT] = { "Graphics", "Compute", "Transfer", "Video decode" };
	for (int i = 0; i < QUEUE_INDEX_COUNT; i++)
		if (queue_info.family_indices[i] != VK_QUEUE_FAMILY_IGNORED)
			LOGI("%s queue: family %u, index %u.\n", family_names[i], queue_info.family_indices[i], queue_indices[i]);
#endif

	check_descriptor_indexing_features();

	return true;
}

void Context::check_descriptor_indexing_features()
{
	auto &f = ext.descriptor_indexing_features;
	if (f.descriptorBindingSampledImageUpdateAfterBind &&
	    f.descriptorBindingPartiallyBound &&
		f.descriptorBindingVariableDescriptorCount &&
	    f.runtimeDescriptorArray &&
	    f.shaderSampledImageArrayNonUniformIndexing)
	{
		ext.supports_descriptor_indexing = true;
	}
}

#ifdef GRANITE_VULKAN_FOSSILIZE
bool Context::format_is_supported(VkFormat format, VkFormatFeatureFlags features)
{
	if (gpu == VK_NULL_HANDLE)
		return false;

	VkFormatProperties props;
	vkGetPhysicalDeviceFormatProperties(gpu, format, &props);
	auto supported = props.bufferFeatures | props.linearTilingFeatures | props.optimalTilingFeatures;
	return (supported & features) == features;
}

bool Context::descriptor_set_layout_is_supported(const VkDescriptorSetLayoutCreateInfo *set_layout)
{
	if (device == VK_NULL_HANDLE)
		return false;

	VkDescriptorSetLayoutSupport support = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_SUPPORT };
	vkGetDescriptorSetLayoutSupport(device, set_layout, &support);
	return support.supported == VK_TRUE;
}
#endif
}
