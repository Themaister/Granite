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

#include "buffer.hpp"
#include "chain_allocator.hpp"
#include "command_buffer.hpp"
#include "command_pool.hpp"
#include "fence.hpp"
#include "fence_manager.hpp"
#include "hashmap.hpp"
#include "image.hpp"
#include "memory_allocator.hpp"
#include "render_pass.hpp"
#include "sampler.hpp"
#include "semaphore.hpp"
#include "semaphore_manager.hpp"
#include "event_manager.hpp"
#include "shader.hpp"
#include "vulkan.hpp"
#include "shader_manager.hpp"
#include "texture_manager.hpp"
#include "query_pool.hpp"
#include <memory>
#include <vector>
#include <atomic>

namespace Vulkan
{
enum class SwapchainRenderPass
{
	ColorOnly,
	Depth,
	DepthStencil
};

struct InitialImageBuffer
{
	BufferHandle buffer;
	std::vector<VkBufferImageCopy> blits;
};

class Device
{
public:
	Device();
	~Device();
	void set_context(const Context &context);
	void init_swapchain(const std::vector<VkImage> &swapchain_images, unsigned width, unsigned height, VkFormat format);
	void init_external_swapchain(const std::vector<ImageHandle> &swapchain_images);

	unsigned get_num_swapchain_images() const
	{
		return per_frame.size();
	}

	void begin_frame(unsigned index);
	void wait_idle();
	void end_frame();
	void flush_frame();
	CommandBufferHandle request_command_buffer(CommandBuffer::Type type = CommandBuffer::Type::Graphics);
	void submit(CommandBufferHandle cmd, Fence *fence = nullptr, Semaphore *semaphore = nullptr, Semaphore *semaphore_alt = nullptr);
	void submit_empty(CommandBuffer::Type type, Fence *fence, Semaphore *semaphore, Semaphore *semaphore_alt);

	VkDevice get_device()
	{
		return device;
	}

	ShaderHandle create_shader(ShaderStage stage, const uint32_t *code, size_t size);
	ProgramHandle create_program(const uint32_t *vertex_data, size_t vertex_size, const uint32_t *fragment_data,
	                             size_t fragment_size);
	ProgramHandle create_program(const uint32_t *compute_data, size_t compute_size);
	void bake_program(Program &program);

	void *map_host_buffer(Buffer &buffer, MemoryAccessFlags access);
	void unmap_host_buffer(const Buffer &buffer);

	BufferHandle create_buffer(const BufferCreateInfo &info, const void *initial);
	ImageHandle create_image(const ImageCreateInfo &info, const ImageInitialData *initial = nullptr);
	InitialImageBuffer create_image_staging_buffer(const ImageCreateInfo &info, const ImageInitialData *initial);

#ifndef _WIN32
	ImageHandle create_imported_image(int fd,
	                                  VkDeviceSize size,
	                                  uint32_t memory_type,
	                                  VkExternalMemoryHandleTypeFlagBitsKHR handle_type,
	                                  const ImageCreateInfo &create_info);
#endif

	ImageViewHandle create_image_view(const ImageViewCreateInfo &view_info);
	BufferViewHandle create_buffer_view(const BufferViewCreateInfo &view_info);
	SamplerHandle create_sampler(const SamplerCreateInfo &info);
	const Sampler &get_stock_sampler(StockSampler sampler) const;

	void destroy_buffer(VkBuffer buffer);
	void destroy_image(VkImage image);
	void destroy_image_view(VkImageView view);
	void destroy_buffer_view(VkBufferView view);
	void destroy_pipeline(VkPipeline pipeline);
	void destroy_sampler(VkSampler sampler);
	void destroy_framebuffer(VkFramebuffer framebuffer);
	void destroy_semaphore(VkSemaphore semaphore);
	void destroy_event(VkEvent event);
	void free_memory(const DeviceAllocation &alloc);

	VkSemaphore set_acquire(VkSemaphore acquire);
	VkSemaphore set_release(VkSemaphore release);
	bool swapchain_touched() const;

	bool format_is_supported(VkFormat format, VkFormatFeatureFlags required) const;
	VkFormat get_default_depth_stencil_format() const;
	VkFormat get_default_depth_format() const;
	ImageView &get_transient_attachment(unsigned width, unsigned height, VkFormat format,
	                                    unsigned index = 0, unsigned samples = 1);
	ImageView &get_physical_attachment(unsigned width, unsigned height, VkFormat format,
	                                   unsigned index = 0, unsigned samples = 1);

	PipelineLayout *request_pipeline_layout(const CombinedResourceLayout &layout);
	DescriptorSetAllocator *request_descriptor_set_allocator(const DescriptorSetLayout &layout);
	const Framebuffer &request_framebuffer(const RenderPassInfo &info);
	const RenderPass &request_render_pass(const RenderPassInfo &info);

	uint64_t allocate_cookie();

	RenderPassInfo get_swapchain_render_pass(SwapchainRenderPass style);
	ImageView &get_swapchain_view();
	ChainDataAllocation allocate_constant_data(VkDeviceSize size);
	ChainDataAllocation allocate_vertex_data(VkDeviceSize size);
	ChainDataAllocation allocate_index_data(VkDeviceSize size);
	ChainDataAllocation allocate_staging_data(VkDeviceSize size);

	const VkPhysicalDeviceMemoryProperties &get_memory_properties() const
	{
		return mem_props;
	}

	const VkPhysicalDeviceProperties &get_gpu_properties() const
	{
		return gpu_props;
	}

	void wait_for_fence(const Fence &fence);
	Semaphore request_semaphore();

#ifndef _WIN32
	Semaphore request_imported_semaphore(int fd, VkExternalSemaphoreHandleTypeFlagBitsKHR handle_type);
#endif

	void add_wait_semaphore(CommandBuffer::Type type, Semaphore semaphore, VkPipelineStageFlags stages);

	ShaderManager &get_shader_manager()
	{
		return shader_manager;
	}

	TextureManager &get_texture_manager()
	{
		return texture_manager;
	}

	PipelineEvent request_pipeline_event();

	// For some platforms, the device and queue might be shared, possibly across threads, so need some mechanism to
	// lock the global device and queue.
	void set_queue_lock(std::function<void ()> lock_callback, std::function<void ()> unlock_callback);

	QueryPoolHandle write_timestamp(VkCommandBuffer cmd, VkPipelineStageFlagBits stage);

private:
	VkInstance instance = VK_NULL_HANDLE;
	VkPhysicalDevice gpu = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	VkQueue graphics_queue = VK_NULL_HANDLE;
	VkQueue compute_queue = VK_NULL_HANDLE;
	VkQueue transfer_queue = VK_NULL_HANDLE;
	DeviceAllocator allocator;
	std::atomic_uint64_t cookie;

	VkPhysicalDeviceMemoryProperties mem_props;
	VkPhysicalDeviceProperties gpu_props;
	bool supports_external = false;
	bool supports_dedicated = false;
	void init_stock_samplers();

	struct PerFrame
	{
		PerFrame(Device *device, DeviceAllocator &global, SemaphoreManager &semaphore_manager,
		         EventManager &event_manager,
		         uint32_t graphics_queue_family_index,
		         uint32_t compute_queue_family_index,
		         uint32_t transfer_queue_family_index);
		~PerFrame();
		void operator=(const PerFrame &) = delete;
		PerFrame(const PerFrame &) = delete;

		void cleanup();
		void begin();
		VkBufferUsageFlags sync_to_gpu(CommandBufferHandle &cmd);
		void release_owned_resources();

		VkDevice device;
		DeviceAllocator &global_allocator;
		SemaphoreManager &semaphore_manager;
		EventManager &event_manager;
		CommandPool graphics_cmd_pool;
		CommandPool compute_cmd_pool;
		CommandPool transfer_cmd_pool;
		ImageHandle backbuffer;
		FenceManager fence_manager;
		QueryPool query_pool;

		ChainAllocator vbo_chain, ibo_chain, ubo_chain, staging_chain;

		std::vector<DeviceAllocation> allocations;
		std::vector<VkFramebuffer> destroyed_framebuffers;
		std::vector<VkSampler> destroyed_samplers;
		std::vector<VkPipeline> destroyed_pipelines;
		std::vector<VkImageView> destroyed_image_views;
		std::vector<VkBufferView> destroyed_buffer_views;
		std::vector<VkImage> destroyed_images;
		std::vector<VkBuffer> destroyed_buffers;
		std::vector<CommandBufferHandle> graphics_submissions;
		std::vector<CommandBufferHandle> compute_submissions;
		std::vector<CommandBufferHandle> transfer_submissions;
		std::vector<std::shared_ptr<FenceHolder>> fences;
		std::vector<VkSemaphore> recycled_semaphores;
		std::vector<VkEvent> recycled_events;
		std::vector<VkSemaphore> destroyed_semaphores;
		bool swapchain_touched = false;
		bool swapchain_consumed = false;
	};
	SemaphoreManager semaphore_manager;
	EventManager event_manager;
	VkSemaphore wsi_acquire = VK_NULL_HANDLE;
	VkSemaphore wsi_release = VK_NULL_HANDLE;

	struct QueueData
	{
		std::vector<Semaphore> wait_semaphores;
		std::vector<VkPipelineStageFlags> wait_stages;
	} graphics, compute, transfer;

	void submit_queue(CommandBuffer::Type type, Fence *fence, Semaphore *semaphore, Semaphore *semaphore_alt);

	PerFrame &frame()
	{
		VK_ASSERT(current_swapchain_index < per_frame.size());
		VK_ASSERT(per_frame[current_swapchain_index]);
		return *per_frame[current_swapchain_index];
	}

	const PerFrame &frame() const
	{
		VK_ASSERT(current_swapchain_index < per_frame.size());
		VK_ASSERT(per_frame[current_swapchain_index]);
		return *per_frame[current_swapchain_index];
	}

	// The per frame structure must be destroyed after
	// the hashmap data structures below, so it must be declared before.
	std::vector<std::unique_ptr<PerFrame>> per_frame;

	unsigned current_swapchain_index = 0;
	uint32_t graphics_queue_family_index = 0;
	uint32_t compute_queue_family_index = 0;
	uint32_t transfer_queue_family_index = 0;

	uint32_t find_memory_type(BufferDomain domain, uint32_t mask);
	uint32_t find_memory_type(ImageDomain domain, uint32_t mask);
	bool memory_type_is_device_optimal(uint32_t type) const;
	bool memory_type_is_host_visible(uint32_t type) const;

	SamplerHandle samplers[static_cast<unsigned>(StockSampler::Count)];
	Util::HashMap<std::unique_ptr<PipelineLayout>> pipeline_layouts;
	Util::HashMap<std::unique_ptr<DescriptorSetAllocator>> descriptor_set_allocators;
	FramebufferAllocator framebuffer_allocator;
	TransientAttachmentAllocator transient_allocator;
	PhysicalAttachmentAllocator physical_allocator;
	Util::HashMap<std::unique_ptr<RenderPass>> render_passes;
	VkPipelineCache pipeline_cache = VK_NULL_HANDLE;

	ShaderManager shader_manager;
	TextureManager texture_manager;

	void init_pipeline_cache();

	void flush_pipeline_cache();

	CommandPool &get_command_pool(CommandBuffer::Type type);
	QueueData &get_queue_data(CommandBuffer::Type type);
	std::vector<CommandBufferHandle> &get_queue_submissions(CommandBuffer::Type type);
	void clear_wait_semaphores();
	void submit_staging(CommandBufferHandle cmd, VkBufferUsageFlags usage);

	std::function<void ()> queue_lock_callback;
	std::function<void ()> queue_unlock_callback;
	void flush_frame(CommandBuffer::Type type);
	void sync_chain_allocators();
};
}
