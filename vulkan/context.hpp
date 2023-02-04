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

#pragma once

#include "vulkan_headers.hpp"
#include "logging.hpp"
#include "vulkan_common.hpp"
#include <memory>
#include <functional>

#ifdef GRANITE_VULKAN_FOSSILIZE
#include "cli/fossilize_feature_filter.hpp"
#endif

namespace Util
{
class TimelineTraceFile;
}

namespace Granite
{
class Filesystem;
class ThreadGroup;
class AssetManager;
}

namespace Vulkan
{
struct DeviceFeatures
{
	bool supports_debug_utils = false;
	bool supports_mirror_clamp_to_edge = false;
	bool supports_nv_device_diagnostic_checkpoints = false;
	bool supports_external_memory_host = false;
	bool supports_surface_capabilities2 = false;
	bool supports_full_screen_exclusive = false;
	bool supports_descriptor_indexing = false;
	bool supports_conservative_rasterization = false;
	bool supports_draw_indirect_count = false;
	bool supports_driver_properties = false;
	bool supports_calibrated_timestamps = false;
	bool supports_memory_budget = false;
	bool supports_astc_decode_mode = false;
	bool supports_sync2 = false;
	bool supports_create_renderpass2 = false;
	bool supports_video_queue = false;
	bool supports_video_decode_queue = false;
	bool supports_video_decode_h264 = false;
	bool supports_video_decode_h265 = false;
	bool supports_pipeline_creation_cache_control = false;
	bool supports_format_feature_flags2 = false;
	bool supports_external = false;
	bool supports_image_format_list = false;
	bool supports_shader_float_control = false;
	bool supports_tooling_info = false;
	bool supports_hdr_metadata = false;
	bool supports_swapchain_colorspace = false;
	bool supports_surface_maintenance1 = false;

	// Vulkan 1.1 core
	VkPhysicalDeviceFeatures enabled_features = {};
	VkPhysicalDeviceMultiviewFeatures multiview_features = {};
	VkPhysicalDeviceShaderDrawParametersFeatures shader_draw_parameters_features = {};
	VkPhysicalDeviceSamplerYcbcrConversionFeatures sampler_ycbcr_conversion_features = {};
	VkPhysicalDeviceMultiviewProperties multiview_properties = {};
	VkPhysicalDeviceSubgroupProperties subgroup_properties = {};

	// KHR
	VkPhysicalDeviceTimelineSemaphoreFeaturesKHR timeline_semaphore_features = {};
	VkPhysicalDevicePerformanceQueryFeaturesKHR performance_query_features = {};
	VkPhysicalDeviceDriverPropertiesKHR driver_properties = {};
	VkPhysicalDeviceSynchronization2FeaturesKHR sync2_features = {};
	VkPhysicalDevicePresentIdFeaturesKHR present_id_features = {};
	VkPhysicalDevicePresentWaitFeaturesKHR present_wait_features = {};
	VkPhysicalDevice8BitStorageFeaturesKHR storage_8bit_features = {};
	VkPhysicalDevice16BitStorageFeaturesKHR storage_16bit_features = {};
	VkPhysicalDeviceFloat16Int8FeaturesKHR float16_int8_features = {};
	VkPhysicalDeviceFloatControlsPropertiesKHR float_control_properties = {};
	VkPhysicalDeviceIDProperties id_properties = {};

	// EXT
	VkPhysicalDeviceExternalMemoryHostPropertiesEXT host_memory_properties = {};
	VkPhysicalDeviceSubgroupSizeControlFeaturesEXT subgroup_size_control_features = {};
	VkPhysicalDeviceSubgroupSizeControlPropertiesEXT subgroup_size_control_properties = {};
	VkPhysicalDeviceHostQueryResetFeaturesEXT host_query_reset_features = {};
	VkPhysicalDeviceShaderDemoteToHelperInvocationFeaturesEXT demote_to_helper_invocation_features = {};
	VkPhysicalDeviceScalarBlockLayoutFeaturesEXT scalar_block_features = {};
	VkPhysicalDeviceUniformBufferStandardLayoutFeaturesKHR ubo_std430_features = {};
	VkPhysicalDeviceDescriptorIndexingFeaturesEXT descriptor_indexing_features = {};
	VkPhysicalDeviceDescriptorIndexingPropertiesEXT descriptor_indexing_properties = {};
	VkPhysicalDeviceConservativeRasterizationPropertiesEXT conservative_rasterization_properties = {};
	VkPhysicalDeviceMemoryPriorityFeaturesEXT memory_priority_features = {};
	VkPhysicalDeviceASTCDecodeFeaturesEXT astc_decode_features = {};
	VkPhysicalDeviceTextureCompressionASTCHDRFeaturesEXT astc_hdr_features = {};
	VkPhysicalDevicePipelineCreationCacheControlFeaturesEXT pipeline_creation_cache_control_features = {};
	VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT swapchain_maintenance1_features = {};

	// Vendor
	VkPhysicalDeviceComputeShaderDerivativesFeaturesNV compute_shader_derivative_features = {};

	// References Vulkan::Context.
	const VkPhysicalDeviceFeatures2 *pdf2 = nullptr;
	const char * const * instance_extensions = nullptr;
	uint32_t num_instance_extensions = 0;
	const char * const * device_extensions = nullptr;
	uint32_t num_device_extensions = 0;
};

enum VendorID
{
	VENDOR_ID_AMD = 0x1002,
	VENDOR_ID_NVIDIA = 0x10de,
	VENDOR_ID_INTEL = 0x8086,
	VENDOR_ID_ARM = 0x13b5,
	VENDOR_ID_QCOM = 0x5143
};

enum ContextCreationFlagBits
{
	CONTEXT_CREATION_DISABLE_BINDLESS_BIT = 1 << 0,
	CONTEXT_CREATION_ENABLE_ADVANCED_WSI_BIT = 1 << 1,
	CONTEXT_CREATION_ENABLE_VIDEO_DECODE_BIT = 1 << 2,
	CONTEXT_CREATION_ENABLE_VIDEO_H264_BIT = 1 << 3,
	CONTEXT_CREATION_ENABLE_VIDEO_H265_BIT = 1 << 4
};
using ContextCreationFlags = uint32_t;

struct QueueInfo
{
	QueueInfo();
	VkQueue queues[QUEUE_INDEX_COUNT] = {};
	uint32_t family_indices[QUEUE_INDEX_COUNT];
	uint32_t counts[QUEUE_INDEX_COUNT] = {};
	uint32_t timestamp_valid_bits = 0;
};

struct InstanceFactory
{
	virtual ~InstanceFactory() = default;
	virtual VkInstance create_instance(const VkInstanceCreateInfo *info) = 0;
};

struct DeviceFactory
{
	virtual ~DeviceFactory() = default;
	virtual VkDevice create_device(VkPhysicalDevice gpu, const VkDeviceCreateInfo *info) = 0;
};

class CopiedApplicationInfo
{
public:
	CopiedApplicationInfo();
	const VkApplicationInfo &get_application_info() const;
	void copy_assign(const VkApplicationInfo *info);

private:
	std::string application;
	std::string engine;
	VkApplicationInfo app = {
		VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr, "Granite", 0, "Granite", 0, VK_API_VERSION_1_1,
	};

	void set_default_app();
};

class Context
	: public Util::IntrusivePtrEnabled<Context, std::default_delete<Context>, HandleCounter>
#ifdef GRANITE_VULKAN_FOSSILIZE
	, public Fossilize::DeviceQueryInterface
#endif
{
public:
	// If these interface are set, factory->create() calls are used instead of global vkCreateInstance and vkCreateDevice.
	// For deeper API interop scenarios.
	void set_instance_factory(InstanceFactory *factory);
	void set_device_factory(DeviceFactory *factory);

	// Call before initializing instances. app_info may be freed after returning.
	// API_VERSION must be at least 1.1.
	// By default, a Vulkan 1.1 instance is created.
	void set_application_info(const VkApplicationInfo *app_info);

	// Recommended interface.
	// InstanceFactory can be used to override enabled instance layers and extensions.
	// For simple WSI use, it is enough to just enable VK_KHR_surface and the platform.
	bool init_instance(const char * const *instance_ext, uint32_t instance_ext_count,
	                   ContextCreationFlags flags = 0);
	// DeviceFactory can be used to override enabled device extensions.
	// For simple WSI use, it is enough to just enable VK_KHR_swapchain.
	bool init_device(VkPhysicalDevice gpu, VkSurfaceKHR surface_compat,
	                 const char * const *device_ext, uint32_t device_ext_count,
	                 ContextCreationFlags flags = 0);

	// Simplified initialization which calls init_instance and init_device in succession with NULL GPU and surface.
	// Provided for compat with older code.
	bool init_instance_and_device(const char * const *instance_ext, uint32_t instance_ext_count,
	                              const char * const *device_ext, uint32_t device_ext_count,
	                              ContextCreationFlags flags = 0);

	// Deprecated. For libretro Vulkan context negotiation v1.
	// Use InstanceFactory and DeviceFactory for more advanced scenarios in v2.
	bool init_device_from_instance(VkInstance instance, VkPhysicalDevice gpu, VkSurfaceKHR surface,
	                               const char **required_device_extensions,
	                               unsigned num_required_device_extensions,
	                               const VkPhysicalDeviceFeatures *required_features,
	                               ContextCreationFlags flags = 0);

	Context() = default;
	Context(const Context &) = delete;
	void operator=(const Context &) = delete;
	static bool init_loader(PFN_vkGetInstanceProcAddr addr);
	static PFN_vkGetInstanceProcAddr get_instance_proc_addr();

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

	const QueueInfo &get_queue_info() const
	{
		return queue_info;
	}

	const VkPhysicalDeviceProperties &get_gpu_props() const
	{
		return gpu_props;
	}

	const VkPhysicalDeviceMemoryProperties &get_mem_props() const
	{
		return mem_props;
	}

	void release_instance()
	{
		owned_instance = false;
	}

	void release_device()
	{
		owned_device = false;
	}

	const DeviceFeatures &get_enabled_device_features() const
	{
		return ext;
	}

	const VkApplicationInfo &get_application_info() const;

	void notify_validation_error(const char *msg);
	void set_notification_callback(std::function<void (const char *)> func);

	void set_num_thread_indices(unsigned indices)
	{
		num_thread_indices = indices;
	}

	unsigned get_num_thread_indices() const
	{
		return num_thread_indices;
	}

	const VolkDeviceTable &get_device_table() const
	{
		return device_table;
	}

	struct SystemHandles
	{
		Util::TimelineTraceFile *timeline_trace_file = nullptr;
		Granite::Filesystem *filesystem = nullptr;
		Granite::ThreadGroup *thread_group = nullptr;
		Granite::AssetManager *asset_manager = nullptr;
	};

	void set_system_handles(const SystemHandles &handles_)
	{
		handles = handles_;
	}

	const SystemHandles &get_system_handles() const
	{
		return handles;
	}

#ifdef GRANITE_VULKAN_FOSSILIZE
	const Fossilize::FeatureFilter &get_feature_filter() const
	{
		return feature_filter;
	}
#endif

	const VkPhysicalDeviceFeatures2 &get_physical_device_features() const
	{
		return pdf2;
	}

private:
	InstanceFactory *instance_factory = nullptr;
	DeviceFactory *device_factory = nullptr;
	VkDevice device = VK_NULL_HANDLE;
	VkInstance instance = VK_NULL_HANDLE;
	VkPhysicalDevice gpu = VK_NULL_HANDLE;
	VolkDeviceTable device_table = {};
	SystemHandles handles;

	VkPhysicalDeviceProperties gpu_props = {};
	VkPhysicalDeviceMemoryProperties mem_props = {};

	CopiedApplicationInfo user_application_info;

	QueueInfo queue_info;
	unsigned num_thread_indices = 1;

	bool create_instance(const char * const *instance_ext, uint32_t instance_ext_count, ContextCreationFlags flags);
	bool create_device(VkPhysicalDevice gpu, VkSurfaceKHR surface,
	                   const char * const *required_device_extensions, uint32_t num_required_device_extensions,
	                   const VkPhysicalDeviceFeatures *required_features, ContextCreationFlags flags);

	bool owned_instance = false;
	bool owned_device = false;
	DeviceFeatures ext;
	VkPhysicalDeviceFeatures2 pdf2;
	std::vector<const char *> enabled_device_extensions;
	std::vector<const char *> enabled_instance_extensions;

#ifdef VULKAN_DEBUG
	VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
	bool force_no_validation = false;
#endif
	std::function<void (const char *)> message_callback;

	void destroy();
	void check_descriptor_indexing_features();

	static bool physical_device_supports_surface(VkPhysicalDevice gpu, VkSurfaceKHR surface);

#ifdef GRANITE_VULKAN_FOSSILIZE
	Fossilize::FeatureFilter feature_filter;
	bool format_is_supported(VkFormat format, VkFormatFeatureFlags features) override;
	bool descriptor_set_layout_is_supported(const VkDescriptorSetLayoutCreateInfo *set_layout) override;
#endif
};

using ContextHandle = Util::IntrusivePtr<Context>;
}
