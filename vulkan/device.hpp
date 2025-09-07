/* Copyright (c) 2017-2024 Hans-Kristian Arntzen
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

#include "buffer.hpp"
#include "command_buffer.hpp"
#include "command_pool.hpp"
#include "fence.hpp"
#include "fence_manager.hpp"
#include "image.hpp"
#include "memory_allocator.hpp"
#include "render_pass.hpp"
#include "sampler.hpp"
#include "semaphore.hpp"
#include "semaphore_manager.hpp"
#include "event_manager.hpp"
#include "shader.hpp"
#include "context.hpp"
#include "query_pool.hpp"
#include "buffer_pool.hpp"
#include "indirect_layout.hpp"
#include "pipeline_cache.hpp"
#include <memory>
#include <vector>
#include <functional>
#include <unordered_map>
#include <stdio.h>

#ifdef GRANITE_VULKAN_SYSTEM_HANDLES
#include "shader_manager.hpp"
#include "resource_manager.hpp"
#endif

#include <atomic>
#include <mutex>
#include <condition_variable>

#ifdef GRANITE_VULKAN_FOSSILIZE
#include "fossilize.hpp"
#endif

#include "quirks.hpp"
#include "small_vector.hpp"

namespace Util
{
class TimelineTraceFile;
}

namespace Granite
{
struct TaskGroup;
}

namespace Vulkan
{
enum class SwapchainRenderPass
{
	ColorOnly,
	Depth,
	DepthStencil
};

struct HostReference
{
	const void *data;
	size_t size;
};

struct InitialImageBuffer
{
	// Either buffer or host is used. Ideally host is used so that host image copy can be used for uploads.
	BufferHandle buffer;
	HostReference host;
	Util::SmallVector<VkBufferImageCopy, 32> blits;
};

struct HandlePool
{
	VulkanObjectPool<Buffer> buffers;
	VulkanObjectPool<Image> images;
	VulkanObjectPool<LinearHostImage> linear_images;
	VulkanObjectPool<ImageView> image_views;
	VulkanObjectPool<BufferView> buffer_views;
	VulkanObjectPool<Sampler> samplers;
	VulkanObjectPool<FenceHolder> fences;
	VulkanObjectPool<SemaphoreHolder> semaphores;
	VulkanObjectPool<EventHolder> events;
	VulkanObjectPool<QueryPoolResult> query;
	VulkanObjectPool<CommandBuffer> command_buffers;
	VulkanObjectPool<BindlessDescriptorPool> bindless_descriptor_pool;
	VulkanObjectPool<DeviceAllocationOwner> allocations;
};

class DebugChannelInterface
{
public:
	union Word
	{
		uint32_t u32;
		int32_t s32;
		float f32;
	};
	virtual void message(const std::string &tag, uint32_t code, uint32_t x, uint32_t y, uint32_t z,
	                     uint32_t word_count, const Word *words) = 0;
};

namespace Helper
{
struct WaitSemaphores
{
	Util::SmallVector<VkSemaphoreSubmitInfo> binary_waits;
	Util::SmallVector<VkSemaphoreSubmitInfo> timeline_waits;
};

class BatchComposer
{
public:
	enum { MaxSubmissions = 8 };

	explicit BatchComposer(uint64_t present_id_nv);
	void add_wait_submissions(WaitSemaphores &sem);
	void add_wait_semaphore(SemaphoreHolder &sem, VkPipelineStageFlags2 stage);
	void add_wait_semaphore(VkSemaphore sem, VkPipelineStageFlags2 stage);
	void add_signal_semaphore(VkSemaphore sem, VkPipelineStageFlags2 stage, uint64_t count);
	void add_command_buffer(VkCommandBuffer cmd);

	void begin_batch();
	Util::SmallVector<VkSubmitInfo2, MaxSubmissions> &bake(int profiling_iteration = -1);

private:
	Util::SmallVector<VkSubmitInfo2, MaxSubmissions> submits;
	VkPerformanceQuerySubmitInfoKHR profiling_infos[Helper::BatchComposer::MaxSubmissions];

	Util::SmallVector<VkLatencySubmissionPresentIdNV> present_ids_nv;
	Util::SmallVector<VkSemaphoreSubmitInfo> waits[MaxSubmissions];
	Util::SmallVector<VkSemaphoreSubmitInfo> signals[MaxSubmissions];
	Util::SmallVector<VkCommandBufferSubmitInfo> cmds[MaxSubmissions];

	uint64_t present_id_nv = 0;
	unsigned submit_index = 0;
};
}

class Device
	: public Util::IntrusivePtrEnabled<Device, std::default_delete<Device>, HandleCounter>
#ifdef GRANITE_VULKAN_FOSSILIZE
	, public Fossilize::StateCreatorInterface
#endif
{
public:
	// Device-based objects which need to poke at internal data structures when their lifetimes end.
	// Don't want to expose a lot of internal guts to make this work.
	friend class QueryPool;
	friend struct QueryPoolResultDeleter;
	friend class EventHolder;
	friend struct EventHolderDeleter;
	friend class SemaphoreHolder;
	friend struct SemaphoreHolderDeleter;
	friend class FenceHolder;
	friend struct FenceHolderDeleter;
	friend class Sampler;
	friend struct SamplerDeleter;
	friend class ImmutableSampler;
	friend class ImmutableYcbcrConversion;
	friend class Buffer;
	friend struct BufferDeleter;
	friend class BufferView;
	friend struct BufferViewDeleter;
	friend class ImageView;
	friend struct ImageViewDeleter;
	friend class Image;
	friend struct ImageDeleter;
	friend struct LinearHostImageDeleter;
	friend class CommandBuffer;
	friend struct CommandBufferDeleter;
	friend class BindlessDescriptorPool;
	friend struct BindlessDescriptorPoolDeleter;
	friend class Program;
	friend class WSI;
	friend class Cookie;
	friend class Framebuffer;
	friend class PipelineLayout;
	friend class FramebufferAllocator;
	friend class RenderPass;
	friend class Texture;
	friend class DescriptorSetAllocator;
	friend class Shader;
	friend class ImageResourceHolder;
	friend class DeviceAllocationOwner;
	friend struct DeviceAllocationDeleter;

	Device();
	~Device();

	// No move-copy.
	void operator=(Device &&) = delete;
	Device(Device &&) = delete;

	// Only called by main thread, during setup phase.
	void set_context(const Context &context);

	// This is asynchronous in nature. See query_initialization_progress().
	// Kicks off Fossilize and shader manager caching.
	void begin_shader_caches();
	// For debug or trivial applications, blocks until all shader cache work is done.
	void wait_shader_caches();

	void init_swapchain(const std::vector<VkImage> &swapchain_images, unsigned width, unsigned height, VkFormat format,
	                    VkSurfaceTransformFlagBitsKHR transform, VkImageUsageFlags usage, VkImageLayout layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
	void set_swapchain_queue_family_support(uint32_t queue_family_support);
	bool can_touch_swapchain_in_command_buffer(CommandBuffer::Type type) const;
	void init_external_swapchain(const std::vector<ImageHandle> &swapchain_images);
	void init_frame_contexts(unsigned count);
	const VolkDeviceTable &get_device_table() const;

	// Profiling
	bool init_performance_counters(CommandBuffer::Type type, const std::vector<std::string> &names);
	bool acquire_profiling();
	void release_profiling();
	void query_available_performance_counters(CommandBuffer::Type type,
	                                          uint32_t *count,
	                                          const VkPerformanceCounterKHR **counters,
	                                          const VkPerformanceCounterDescriptionKHR **desc);

	ImageView &get_swapchain_view();
	ImageView &get_swapchain_view(unsigned index);
	unsigned get_num_swapchain_images() const;
	unsigned get_num_frame_contexts() const;
	unsigned get_swapchain_index() const;
	unsigned get_current_frame_context() const;

	size_t get_pipeline_cache_size();
	bool get_pipeline_cache_data(uint8_t *data, size_t size);
	// If persistent_mapping is true, the data pointer lifetime is live as long as the device is.
	// Useful for read-only file mmap.
	bool init_pipeline_cache(const uint8_t *data, size_t size, bool persistent_mapping = false);

	// Frame-pushing interface.
	void next_frame_context();
	bool next_frame_context_is_non_blocking();

	// Normally, the main thread ensures forward progress of the frame context
	// so that async tasks don't have to care about it,
	// but in the case where async threads are continuously pumping Vulkan work
	// in the background, they need to reclaim memory if WSI goes to sleep for a long period of time.
	void next_frame_context_in_async_thread();
	void set_enable_async_thread_frame_context(bool enable);

	void wait_idle();
	void end_frame_context();

	// RenderDoc integration API for app-guided captures.
	static bool init_renderdoc_capture();
	// Calls next_frame_context() and begins a renderdoc capture.
	void begin_renderdoc_capture();
	// Calls next_frame_context() and ends the renderdoc capture.
	void end_renderdoc_capture();

	// Set names for objects for debuggers and profilers.
	void set_name(const Buffer &buffer, const char *name);
	void set_name(const Image &image, const char *name);
	void set_name(const CommandBuffer &cmd, const char *name);
	// Generic version.
	void set_name(uint64_t object, VkObjectType type, const char *name);

	// Submission interface, may be called from any thread at any time.
	void flush_frame();
	CommandBufferHandle request_command_buffer(CommandBuffer::Type type = CommandBuffer::Type::Generic);
	CommandBufferHandle request_command_buffer_for_thread(unsigned thread_index, CommandBuffer::Type type = CommandBuffer::Type::Generic);

	CommandBufferHandle request_profiled_command_buffer(CommandBuffer::Type type = CommandBuffer::Type::Generic);
	CommandBufferHandle request_profiled_command_buffer_for_thread(unsigned thread_index, CommandBuffer::Type type = CommandBuffer::Type::Generic);

	void submit(CommandBufferHandle &cmd, Fence *fence = nullptr,
	            unsigned semaphore_count = 0, Semaphore *semaphore = nullptr);

	void submit_empty(CommandBuffer::Type type,
	                  Fence *fence = nullptr,
	                  SemaphoreHolder *semaphore = nullptr);
	// Mark that there have been work submitted in this frame context outside our control
	// that accesses resources Vulkan::Device owns.
	void submit_external(CommandBuffer::Type type);
	void submit_discard(CommandBufferHandle &cmd);
	QueueIndices get_physical_queue_type(CommandBuffer::Type queue_type) const;
	void register_time_interval(std::string tid, QueryPoolHandle start_ts, QueryPoolHandle end_ts,
	                            const std::string &tag);

	// Request shaders and programs. These objects are owned by the Device.
	Shader *request_shader(const uint32_t *code, size_t size, const ResourceLayout *layout = nullptr);
	Shader *request_shader_by_hash(Util::Hash hash);
	Program *request_program(const uint32_t *task_data, size_t task_size,
	                         const uint32_t *mesh_data, size_t mesh_size,
	                         const uint32_t *fragment_data, size_t fragment_size,
	                         const ResourceLayout *task_layout = nullptr,
	                         const ResourceLayout *mesh_layout = nullptr,
	                         const ResourceLayout *fragment_layout = nullptr);
	Program *request_program(const uint32_t *vertex_data, size_t vertex_size,
	                         const uint32_t *fragment_data, size_t fragment_size,
	                         const ResourceLayout *vertex_layout = nullptr,
	                         const ResourceLayout *fragment_layout = nullptr);
	Program *request_program(const uint32_t *compute_data, size_t compute_size,
	                         const ResourceLayout *layout = nullptr);
	Program *request_program(Shader *task, Shader *mesh, Shader *fragment, const ImmutableSamplerBank *sampler_bank = nullptr);
	Program *request_program(Shader *vertex, Shader *fragment, const ImmutableSamplerBank *sampler_bank = nullptr);
	Program *request_program(Shader *compute, const ImmutableSamplerBank *sampler_bank = nullptr);
	const IndirectLayout *request_indirect_layout(const PipelineLayout *layout, const IndirectLayoutToken *tokens,
	                                              uint32_t num_tokens, uint32_t stride);

	const ImmutableYcbcrConversion *request_immutable_ycbcr_conversion(const VkSamplerYcbcrConversionCreateInfo &info);
	const ImmutableSampler *request_immutable_sampler(const SamplerCreateInfo &info, const ImmutableYcbcrConversion *ycbcr);

	// Map and unmap buffer objects.
	void *map_host_buffer(const Buffer &buffer, MemoryAccessFlags access);
	void unmap_host_buffer(const Buffer &buffer, MemoryAccessFlags access);
	void *map_host_buffer(const Buffer &buffer, MemoryAccessFlags access, VkDeviceSize offset, VkDeviceSize length);
	void unmap_host_buffer(const Buffer &buffer, MemoryAccessFlags access, VkDeviceSize offset, VkDeviceSize length);

	void *map_linear_host_image(const LinearHostImage &image, MemoryAccessFlags access);
	void unmap_linear_host_image_and_sync(const LinearHostImage &image, MemoryAccessFlags access);

	// Create buffers and images.
	BufferHandle create_buffer(const BufferCreateInfo &info, const void *initial = nullptr);
	BufferHandle create_imported_host_buffer(const BufferCreateInfo &info, VkExternalMemoryHandleTypeFlagBits type, void *host_buffer);
	ImageHandle create_image(const ImageCreateInfo &info, const ImageInitialData *initial = nullptr);
	ImageHandle create_image_from_staging_buffer(const ImageCreateInfo &info, const InitialImageBuffer *buffer);
	LinearHostImageHandle create_linear_host_image(const LinearHostImageCreateInfo &info);
	// Does not create any default image views. Only wraps the VkImage
	// as a non-owned handle for purposes of API interop.
	ImageHandle wrap_image(const ImageCreateInfo &info, VkImage img);
	DeviceAllocationOwnerHandle take_device_allocation_ownership(Image &image);
	DeviceAllocationOwnerHandle allocate_memory(const MemoryAllocateInfo &info);

	// Create staging buffers for images.

	// This is deprecated and considered slow path.
	// If number of subresources is 1, the fast path can be taken.
	InitialImageBuffer create_image_staging_buffer(const ImageCreateInfo &info, const ImageInitialData *initial);

	// Only takes a reference to the layout.
	// Ideal path when uploading resources since it's compatible with host image copy, etc.
	InitialImageBuffer create_image_staging_buffer(const TextureFormatLayout &layout);

	// Create image view, buffer views and samplers.
	ImageViewHandle create_image_view(const ImageViewCreateInfo &view_info);
	BufferViewHandle create_buffer_view(const BufferViewCreateInfo &view_info);
	SamplerHandle create_sampler(const SamplerCreateInfo &info);

	BindlessDescriptorPoolHandle create_bindless_descriptor_pool(BindlessResourceType type,
	                                                             unsigned num_sets, unsigned num_descriptors);

	// Render pass helpers.
	bool image_format_is_supported(VkFormat format, VkFormatFeatureFlags2KHR required, VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL) const;
	void get_format_properties(VkFormat format, VkFormatProperties3KHR *properties) const;
	bool get_image_format_properties(VkFormat format, VkImageType type, VkImageTiling tiling,
	                                 VkImageUsageFlags usage, VkImageCreateFlags flags,
	                                 const void *pNext,
	                                 VkImageFormatProperties2 *properties2) const;

	VkFormat get_default_depth_stencil_format() const;
	VkFormat get_default_depth_format() const;
	ImageHandle get_transient_attachment(unsigned width, unsigned height, VkFormat format,
	                                     unsigned index = 0, unsigned samples = 1, unsigned layers = 1);
	RenderPassInfo get_swapchain_render_pass(SwapchainRenderPass style);

	// Semaphore API:
	// Semaphores in Granite are abstracted to support both binary and timeline semaphores
	// internally.
	// In practice this means that semaphores behave like single-use binary semaphores,
	// with one signal and one wait.
	// A single semaphore handle is not reused for multiple submissions, and they must be recycled through
	// the device. The intended use is device.submit(&sem), device.add_wait_semaphore(sem); dispose(sem);
	// For timeline semaphores, the semaphore is just a proxy object which
	// holds the internally owned VkSemaphore + timeline value and is otherwise lightweight.
	//
	// However, there are various use cases where we explicitly need semaphore objects:
	// - Interoperate with other code that only accepts VkSemaphore.
	// - Interoperate with external objects. We need to know whether to use binary or timeline.
	//   For timelines, we need to know which handle type to use (OPAQUE or ID3D12Fence).
	//   Binary external semaphore is always opaque with TEMPORARY semantics.

	void add_wait_semaphore(CommandBuffer::Type type, Semaphore semaphore, VkPipelineStageFlags2 stages, bool flush);

	// If transfer_ownership is set, Semaphore owns the VkSemaphore. Otherwise, application must
	// free the semaphore when GPU usage of it is complete.
	Semaphore request_semaphore(VkSemaphoreTypeKHR type, VkSemaphore handle = VK_NULL_HANDLE, bool transfer_ownership = false);

	// Requests a binary or timeline semaphore that can be used to import/export.
	// These semaphores cannot be used directly by add_wait_semaphore() and submit_empty().
	// See request_timeline_semaphore_as_binary() for how to use timelines.
	Semaphore request_semaphore_external(VkSemaphoreTypeKHR type,
	                                     VkExternalSemaphoreHandleTypeFlagBits handle_type);

	// The created semaphore does not hold ownership of the VkSemaphore object.
	// This is used when we want to wait on or signal an external timeline semaphore at a specific timeline value.
	// We must collapse the timeline to a "binary" semaphore before we can call submit_empty or add_wait_semaphore().
	Semaphore request_timeline_semaphore_as_binary(const SemaphoreHolder &holder, uint64_t value);

	// A proxy semaphore which lets us grab a semaphore handle before we signal it.
	// Move assignment can be used to move a payload.
	// Mostly useful to deal better with render graph implementation.
	// For time being however, we'll support moving the payload over to the proxy object.
	Semaphore request_proxy_semaphore();

	// For compat with existing code that uses this entry point.
	inline Semaphore request_legacy_semaphore() { return request_semaphore(VK_SEMAPHORE_TYPE_BINARY_KHR); }

	inline VkDevice get_device() const
	{
		return device;
	}

	inline VkPhysicalDevice get_physical_device() const
	{
		return gpu;
	}

	inline VkInstance get_instance() const
	{
		return instance;
	}

	inline const VkPhysicalDeviceMemoryProperties &get_memory_properties() const
	{
		return mem_props;
	}

	inline const VkPhysicalDeviceProperties &get_gpu_properties() const
	{
		return gpu_props;
	}

	void get_memory_budget(HeapBudget *budget);

	const Sampler &get_stock_sampler(StockSampler sampler) const;

#ifdef GRANITE_VULKAN_SYSTEM_HANDLES
	// To obtain ShaderManager, ShaderModules must be observed to be complete
	// in query_initialization_progress().
	ShaderManager &get_shader_manager();
	ResourceManager &get_resource_manager();
	Granite::FileMappingHandle persistent_pipeline_cache;
#endif

	// Useful for loading screens or otherwise figuring out
	// when we can start rendering in a stable state.
	enum class InitializationStage
	{
		CacheMaintenance,
		// When this is done, shader modules and the shader manager have been populated.
		// At this stage it is safe to use shaders in a configuration where we
		// don't have SPIRV-Cross and/or shaderc to do on the fly compilation.
		// For shipping configurations. We can still compile pipelines, but it may stutter.
		ShaderModules,
		// When this is done, pipelines should never stutter if Fossilize knows about the pipeline.
		Pipelines
	};

	// 0 -> not started
	// [1, 99] rough percentage of completion
	// >= 100 done
	unsigned query_initialization_progress(InitializationStage status) const;

	// For some platforms, the device and queue might be shared, possibly across threads, so need some mechanism to
	// lock the global device and queue.
	void set_queue_lock(std::function<void ()> lock_callback,
	                    std::function<void ()> unlock_callback);

	// Alternative form, when we have to provide lock callbacks to external APIs.
	void external_queue_lock();
	void external_queue_unlock();

	const ImplementationWorkarounds &get_workarounds() const
	{
		return workarounds;
	}

	const DeviceFeatures &get_device_features() const
	{
		return ext;
	}

	bool consumes_debug_markers() const
	{
		return debug_marker_sensitive;
	}

	bool swapchain_touched() const;

	double convert_device_timestamp_delta(uint64_t start_ticks, uint64_t end_ticks) const;
	// Writes a timestamp on host side, which is calibrated to the GPU timebase.
	QueryPoolHandle write_calibrated_timestamp();

	// A split version of VkEvent handling which lets us record a wait command before signal is recorded.
	PipelineEvent begin_signal_event();

	const Context::SystemHandles &get_system_handles() const
	{
		return system_handles;
	}

	void configure_default_geometry_samplers(float max_aniso, float lod_bias);

	bool supports_subgroup_size_log2(bool subgroup_full_group,
	                                 uint8_t subgroup_minimum_size_log2,
	                                 uint8_t subgroup_maximum_size_log2,
	                                 VkShaderStageFlagBits stage = VK_SHADER_STAGE_COMPUTE_BIT) const;

	const QueueInfo &get_queue_info() const;

	void timestamp_log_reset();
	void timestamp_log(const TimestampIntervalReportCallback &cb) const;

private:
	VkInstance instance = VK_NULL_HANDLE;
	VkPhysicalDevice gpu = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	const VolkDeviceTable *table = nullptr;
	const Context *ctx = nullptr;
	QueueInfo queue_info;
	unsigned num_thread_indices = 1;

	std::atomic_uint64_t cookie;

	uint64_t allocate_cookie();
	void bake_program(Program &program, const ImmutableSamplerBank *sampler_bank);
	void merge_combined_resource_layout(CombinedResourceLayout &layout, const Program &program);

	void request_vertex_block(BufferBlock &block, VkDeviceSize size);
	void request_index_block(BufferBlock &block, VkDeviceSize size);
	void request_uniform_block(BufferBlock &block, VkDeviceSize size);
	void request_staging_block(BufferBlock &block, VkDeviceSize size);

	QueryPoolHandle write_timestamp(VkCommandBuffer cmd, VkPipelineStageFlags2 stage);

	void set_acquire_semaphore(unsigned index, Semaphore acquire);
	void set_present_id(VkSwapchainKHR low_latency_swapchain, uint64_t present_id);
	Semaphore consume_release_semaphore();
	VkQueue get_current_present_queue() const;
	CommandBuffer::Type get_current_present_queue_type() const;

	const PipelineLayout *request_pipeline_layout(const CombinedResourceLayout &layout,
	                                              const ImmutableSamplerBank *immutable_samplers);
	DescriptorSetAllocator *request_descriptor_set_allocator(const DescriptorSetLayout &layout,
	                                                         const uint32_t *stages_for_sets,
	                                                         const ImmutableSampler * const *immutable_samplers);
	const Framebuffer &request_framebuffer(const RenderPassInfo &info);
	const RenderPass &request_render_pass(const RenderPassInfo &info, bool compatible);

	VkPhysicalDeviceMemoryProperties mem_props;
	VkPhysicalDeviceProperties gpu_props;

	DeviceFeatures ext;
	bool debug_marker_sensitive = false;
	void init_stock_samplers();
	void init_stock_sampler(StockSampler sampler, float max_aniso, float lod_bias);
	void init_timeline_semaphores();
	void deinit_timeline_semaphores();

	uint64_t update_wrapped_device_timestamp(uint64_t ts);
	int64_t convert_timestamp_to_absolute_nsec(const QueryPoolResult &handle);
	Context::SystemHandles system_handles;

	QueryPoolHandle write_timestamp_nolock(VkCommandBuffer cmd, VkPipelineStageFlags2 stage);
	QueryPoolHandle write_calibrated_timestamp_nolock();
	void register_time_interval_nolock(std::string tid, QueryPoolHandle start_ts, QueryPoolHandle end_ts,
	                                   const std::string &tag);

	// Make sure this is deleted last.
	HandlePool handle_pool;

	// Calibrated timestamps.
	void init_calibrated_timestamps();
	void recalibrate_timestamps_fallback();
	void recalibrate_timestamps();
	bool resample_calibrated_timestamps();
	VkTimeDomainEXT calibrated_time_domain = VK_TIME_DOMAIN_DEVICE_EXT;
	int64_t calibrated_timestamp_device = 0;
	int64_t calibrated_timestamp_host = 0;
	int64_t calibrated_timestamp_device_accum = 0;
	unsigned timestamp_calibration_counter = 0;
	Vulkan::QueryPoolHandle frame_context_begin_ts;

	struct Managers
	{
		DeviceAllocator memory;
		FenceManager fence;
		SemaphoreManager semaphore;
		EventManager event;
		BufferPool vbo, ibo, ubo, staging;
		TimestampIntervalManager timestamps;
		DescriptorBufferAllocator descriptor_buffer;
	};
	Managers managers;

	struct
	{
		std::mutex memory_lock;
		std::mutex lock;
		std::condition_variable cond;
		Util::RWSpinLock read_only_cache;
		unsigned counter = 0;
		bool async_frame_context = false;
	} lock;

	struct PerFrame
	{
		PerFrame(Device *device, unsigned index);
		~PerFrame();
		void operator=(const PerFrame &) = delete;
		PerFrame(const PerFrame &) = delete;

		bool wait(uint64_t timeout);
		void begin();
		void trim_command_pools();

		Device &device;
		unsigned frame_index;
		const VolkDeviceTable &table;
		Managers &managers;

		std::vector<CommandPool> cmd_pools[QUEUE_INDEX_COUNT];
		VkSemaphore timeline_semaphores[QUEUE_INDEX_COUNT] = {};
		uint64_t timeline_fences[QUEUE_INDEX_COUNT] = {};

		QueryPool query_pool;

		std::vector<BufferBlock> vbo_blocks;
		std::vector<BufferBlock> ibo_blocks;
		std::vector<BufferBlock> ubo_blocks;
		std::vector<BufferBlock> staging_blocks;

		std::vector<VkFence> wait_and_recycle_fences;

		std::vector<DeviceAllocation> allocations;
		std::vector<VkFramebuffer> destroyed_framebuffers;
		std::vector<VkSampler> destroyed_samplers;
		std::vector<VkImageView> destroyed_image_views;
		std::vector<VkBufferView> destroyed_buffer_views;
		std::vector<VkImage> destroyed_images;
		std::vector<VkBuffer> destroyed_buffers;
		std::vector<VkDescriptorPool> destroyed_descriptor_pools;
		Util::SmallVector<CommandBufferHandle> submissions[QUEUE_INDEX_COUNT];
		std::vector<VkSemaphore> recycled_semaphores;
		std::vector<VkEvent> recycled_events;
		std::vector<VkSemaphore> destroyed_semaphores;
		std::vector<VkSemaphore> consumed_semaphores;
		std::vector<VkIndirectExecutionSetEXT> destroyed_execution_sets;
		std::vector<DescriptorBufferAllocation> descriptor_buffer_allocs;
		std::vector<CachedDescriptorPayload> cached_descriptor_payloads;

		struct DebugChannel
		{
			DebugChannelInterface *iface;
			std::string tag;
			BufferHandle buffer;
		};
		std::vector<DebugChannel> debug_channels;

		struct TimestampIntervalHandles
		{
			std::string tid;
			QueryPoolHandle start_ts;
			QueryPoolHandle end_ts;
			TimestampInterval *timestamp_tag;
		};
		std::vector<TimestampIntervalHandles> timestamp_intervals;

		bool in_destructor = false;
	};
	// The per frame structure must be destroyed after
	// the hashmap data structures below, so it must be declared before.
	std::vector<std::unique_ptr<PerFrame>> per_frame;

	struct
	{
		Semaphore acquire;
		Semaphore release;
		std::vector<ImageHandle> swapchain;
		VkQueue present_queue = VK_NULL_HANDLE;
		Vulkan::CommandBuffer::Type present_queue_type = {};
		uint32_t queue_family_support_mask = 0;
		unsigned index = 0;
		bool consumed = false;

		struct
		{
			uint64_t present_id;
			bool need_submit_begin_marker;
			VkSwapchainKHR swapchain;
		} low_latency = {};
	} wsi;
	bool can_touch_swapchain_in_command_buffer(QueueIndices physical_type) const;

	struct QueueData
	{
		Util::SmallVector<Semaphore> wait_semaphores;
		Util::SmallVector<VkPipelineStageFlags2> wait_stages;
		bool need_fence = false;

		VkSemaphore timeline_semaphore = VK_NULL_HANDLE;
		uint64_t current_timeline = 0;
		PerformanceQueryPool performance_query_pool;
		uint32_t implicit_sync_to_queues = 0;
		uint32_t has_incoming_queue_dependencies = 0;
	} queue_data[QUEUE_INDEX_COUNT];

	struct InternalFence
	{
		VkFence fence;
		VkSemaphore timeline;
		uint64_t value;
	};

	void submit_queue(QueueIndices physical_type, InternalFence *fence,
	                  SemaphoreHolder *external_semaphore = nullptr,
	                  unsigned semaphore_count = 0,
	                  Semaphore *semaphore = nullptr,
	                  int profiled_iteration = -1);

	PerFrame &frame()
	{
		VK_ASSERT(frame_context_index < per_frame.size());
		VK_ASSERT(per_frame[frame_context_index]);
		return *per_frame[frame_context_index];
	}

	const PerFrame &frame() const
	{
		VK_ASSERT(frame_context_index < per_frame.size());
		VK_ASSERT(per_frame[frame_context_index]);
		return *per_frame[frame_context_index];
	}

	unsigned frame_context_index = 0;

	uint32_t find_memory_type(BufferDomain domain, uint32_t mask) const;
	uint32_t find_memory_type(ImageDomain domain, uint32_t mask) const;
	uint32_t find_memory_type(uint32_t required, uint32_t mask) const;
	bool memory_type_is_device_optimal(uint32_t type) const;
	bool memory_type_is_host_visible(uint32_t type) const;

	const ImmutableSampler *samplers[static_cast<unsigned>(StockSampler::Count)] = {};

	VulkanCache<PipelineLayout> pipeline_layouts;
	VulkanCache<DescriptorSetAllocator> descriptor_set_allocators;
	VulkanCache<RenderPass> render_passes;
	VulkanCache<Shader> shaders;
	VulkanCache<Program> programs;
	VulkanCache<ImmutableSampler> immutable_samplers;
	VulkanCache<ImmutableYcbcrConversion> immutable_ycbcr_conversions;
	VulkanCache<IndirectLayout> indirect_layouts;

	FramebufferAllocator framebuffer_allocator;
	TransientAttachmentAllocator transient_allocator;
	VkPipelineCache legacy_pipeline_cache = VK_NULL_HANDLE;
	PipelineCache pipeline_binary_cache;

	void init_pipeline_cache();
	void flush_pipeline_cache();

	PerformanceQueryPool &get_performance_query_pool(QueueIndices physical_type);
	PipelineEvent request_pipeline_event();

	std::function<void ()> queue_lock_callback;
	std::function<void ()> queue_unlock_callback;
	void flush_frame_nolock(QueueIndices physical_type);
	void submit_empty_inner(QueueIndices type, InternalFence *fence,
	                        SemaphoreHolder *external_semaphore,
	                        unsigned semaphore_count,
	                        Semaphore *semaphore);

	void collect_wait_semaphores(QueueData &data, Helper::WaitSemaphores &semaphores);
	void emit_queue_signals(Helper::BatchComposer &composer,
	                        SemaphoreHolder *external_semaphore,
	                        VkSemaphore sem, uint64_t timeline, InternalFence *fence,
	                        unsigned semaphore_count, Semaphore *semaphores);
	void emit_implicit_sync_to_queues(QueueIndices physical_type);
	VkResult submit_batches(Helper::BatchComposer &composer, VkQueue queue, VkFence fence,
	                        int profiling_iteration = -1);
	VkResult queue_submit(VkQueue queue, uint32_t count, const VkSubmitInfo2 *submits, VkFence fence);

	void destroy_buffer(VkBuffer buffer);
	void destroy_image(VkImage image);
	void destroy_image_view(VkImageView view);
	void destroy_buffer_view(VkBufferView view);
	void destroy_sampler(VkSampler sampler);
	void destroy_framebuffer(VkFramebuffer framebuffer);
	void destroy_semaphore(VkSemaphore semaphore);
	void consume_semaphore(VkSemaphore semaphore);
	void recycle_semaphore(VkSemaphore semaphore);
	void destroy_event(VkEvent event);
	void free_memory(const DeviceAllocation &alloc);
	void reset_fence(VkFence fence, bool observed_wait);
	void destroy_descriptor_pool(VkDescriptorPool desc_pool);
	void destroy_indirect_execution_set(VkIndirectExecutionSetEXT exec_set);
	void free_descriptor_buffer_allocation(const DescriptorBufferAllocation &alloc);
	void free_cached_descriptor_payload(const CachedDescriptorPayload &payload);

	void destroy_buffer_nolock(VkBuffer buffer);
	void destroy_image_nolock(VkImage image);
	void destroy_image_view_nolock(VkImageView view);
	void destroy_buffer_view_nolock(VkBufferView view);
	void destroy_sampler_nolock(VkSampler sampler);
	void destroy_framebuffer_nolock(VkFramebuffer framebuffer);
	void destroy_semaphore_nolock(VkSemaphore semaphore);
	void consume_semaphore_nolock(VkSemaphore semaphore);
	void recycle_semaphore_nolock(VkSemaphore semaphore);
	void destroy_event_nolock(VkEvent event);
	void free_memory_nolock(const DeviceAllocation &alloc);
	void destroy_descriptor_pool_nolock(VkDescriptorPool desc_pool);
	void reset_fence_nolock(VkFence fence, bool observed_wait);
	void destroy_indirect_execution_set_nolock(VkIndirectExecutionSetEXT exec_set);
	void free_descriptor_buffer_allocation_nolock(const DescriptorBufferAllocation &alloc);
	void free_cached_descriptor_payload_nolock(const CachedDescriptorPayload &payload);

	void flush_frame_nolock();
	CommandBufferHandle request_command_buffer_nolock(unsigned thread_index, CommandBuffer::Type type, bool profiled);
	void submit_discard_nolock(CommandBufferHandle &cmd);
	void submit_and_sync_to_queues(CommandBufferHandle &cmd, uint32_t sync_to_queues);
	void submit_nolock(CommandBufferHandle cmd, Fence *fence,
	                   unsigned semaphore_count, Semaphore *semaphore);
	void submit_empty_nolock(QueueIndices physical_type, Fence *fence,
	                         SemaphoreHolder *semaphore, int profiling_iteration);
	void add_wait_semaphore_nolock(QueueIndices type, Semaphore semaphore,
	                               VkPipelineStageFlags2 stages, bool flush);

	void request_vertex_block_nolock(BufferBlock &block, VkDeviceSize size);
	void request_index_block_nolock(BufferBlock &block, VkDeviceSize size);
	void request_uniform_block_nolock(BufferBlock &block, VkDeviceSize size);
	void request_staging_block_nolock(BufferBlock &block, VkDeviceSize size);

	CommandBufferHandle request_secondary_command_buffer_for_thread(unsigned thread_index,
	                                                                const Framebuffer *framebuffer,
	                                                                unsigned subpass,
	                                                                CommandBuffer::Type type = CommandBuffer::Type::Generic);
	void add_frame_counter_nolock();
	void decrement_frame_counter_nolock();
	void submit_secondary(CommandBuffer &primary, CommandBuffer &secondary);
	void wait_idle_nolock();
	void end_frame_nolock();

	void add_debug_channel_buffer(DebugChannelInterface *iface, std::string tag, BufferHandle buffer);
	void parse_debug_channel(const PerFrame::DebugChannel &channel);

	Fence request_legacy_fence();

#ifdef GRANITE_VULKAN_SYSTEM_HANDLES
	ShaderManager shader_manager;
	ResourceManager resource_manager;
	void init_shader_manager_cache();
	void flush_shader_manager_cache();
#endif

#ifdef GRANITE_VULKAN_FOSSILIZE
	bool enqueue_create_sampler(Fossilize::Hash hash, const VkSamplerCreateInfo *create_info, VkSampler *sampler) override;
	bool enqueue_create_descriptor_set_layout(Fossilize::Hash hash, const VkDescriptorSetLayoutCreateInfo *create_info, VkDescriptorSetLayout *layout) override;
	bool enqueue_create_pipeline_layout(Fossilize::Hash hash, const VkPipelineLayoutCreateInfo *create_info, VkPipelineLayout *layout) override;
	bool enqueue_create_shader_module(Fossilize::Hash hash, const VkShaderModuleCreateInfo *create_info, VkShaderModule *module) override;
	bool enqueue_create_render_pass(Fossilize::Hash hash, const VkRenderPassCreateInfo *create_info, VkRenderPass *render_pass) override;
	bool enqueue_create_render_pass2(Fossilize::Hash hash, const VkRenderPassCreateInfo2 *create_info, VkRenderPass *render_pass) override;
	bool enqueue_create_compute_pipeline(Fossilize::Hash hash, const VkComputePipelineCreateInfo *create_info, VkPipeline *pipeline) override;
	bool enqueue_create_graphics_pipeline(Fossilize::Hash hash, const VkGraphicsPipelineCreateInfo *create_info, VkPipeline *pipeline) override;
	bool enqueue_create_raytracing_pipeline(Fossilize::Hash hash, const VkRayTracingPipelineCreateInfoKHR *create_info, VkPipeline *pipeline) override;
	bool fossilize_replay_graphics_pipeline(Fossilize::Hash hash, VkGraphicsPipelineCreateInfo &info);
	bool fossilize_replay_compute_pipeline(Fossilize::Hash hash, VkComputePipelineCreateInfo &info);

	void replay_tag_simple(Fossilize::ResourceTag tag);

	void register_graphics_pipeline(Fossilize::Hash hash, const VkGraphicsPipelineCreateInfo &info);
	void register_compute_pipeline(Fossilize::Hash hash, const VkComputePipelineCreateInfo &info);
	void register_render_pass(VkRenderPass render_pass, Fossilize::Hash hash, const VkRenderPassCreateInfo2KHR &info);
	void register_descriptor_set_layout(VkDescriptorSetLayout layout, Fossilize::Hash hash, const VkDescriptorSetLayoutCreateInfo &info);
	void register_pipeline_layout(VkPipelineLayout layout, Fossilize::Hash hash, const VkPipelineLayoutCreateInfo &info);
	void register_shader_module(VkShaderModule module, Fossilize::Hash hash, const VkShaderModuleCreateInfo &info);
	void register_sampler(VkSampler sampler, Fossilize::Hash hash, const VkSamplerCreateInfo &info);
	void register_sampler_ycbcr_conversion(VkSamplerYcbcrConversion ycbcr, const VkSamplerYcbcrConversionCreateInfo &info);

	struct RecorderState;
	std::unique_ptr<RecorderState> recorder_state;

	struct ReplayerState;
	std::unique_ptr<ReplayerState> replayer_state;

	void promote_write_cache_to_readonly() const;
	void promote_readonly_db_from_assets() const;

	void init_pipeline_state(const Fossilize::FeatureFilter &filter,
	                         const VkPhysicalDeviceFeatures2 &pdf2,
	                         const VkApplicationInfo &application_info);
	void flush_pipeline_state();
	void block_until_shader_module_ready();
	void block_until_pipeline_ready();
#endif

	ImplementationWorkarounds workarounds;
	void init_workarounds();

	void fill_buffer_sharing_indices(VkBufferCreateInfo &create_info, uint32_t *sharing_indices);

	bool allocate_image_memory(DeviceAllocation *allocation, const ImageCreateInfo &info,
	                           VkImage image, VkImageTiling tiling, VkImageUsageFlags usage);

	void promote_read_write_caches_to_read_only();
};

// A fairly complex helper used for async queue readbacks.
// Typically used for things like headless backend which emulates WSI through readbacks + encode.
struct OwnershipTransferInfo
{
	CommandBuffer::Type old_queue;
	CommandBuffer::Type new_queue;
	VkImageLayout old_image_layout;
	VkImageLayout new_image_layout;
	VkPipelineStageFlags2 dst_pipeline_stage;
	VkAccessFlags2 dst_access;
};

// For an image which was last accessed in old_queue, requests a command buffer
// for new_queue. Commands will be enqueued as necessary in new_queue to ensure that a complete ownership
// transfer has taken place.
// If queue family for old_queue differs from new_queue, a release barrier is enqueued in old_queue.
// In new_queue we perform either an acquire barrier or a simple pipeline barrier to change layout if required.
// If semaphore is a valid handle, it will be waited on in either old_queue to perform release barrier
// or new_queue depending on what is required.
// If the image uses CONCURRENT sharing mode, acquire/release barriers are skipped.
CommandBufferHandle request_command_buffer_with_ownership_transfer(
		Device &device,
		const Vulkan::Image &image,
		const OwnershipTransferInfo &info,
		const Vulkan::Semaphore &semaphore);

using DeviceHandle = Util::IntrusivePtr<Device>;
}
