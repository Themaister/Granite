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
#include "shader.hpp"
#include "vulkan.hpp"
#include <memory>
#include <vector>

namespace Vulkan
{
enum class SwapchainRenderPass
{
	ColorOnly,
	Depth,
	DepthStencil
};

class Device
{
public:
	Device();
	~Device();
	void set_context(const Context &context);
	void init_swapchain(const std::vector<VkImage> swapchain_images, unsigned width, unsigned height, VkFormat format);
	void init_virtual_swapchain(unsigned num_swapchain_images);

	unsigned get_num_swapchain_images() const
	{
		return per_frame.size();
	}

	void begin_frame(unsigned index);
	void flush_frame();
	void wait_idle();
	CommandBufferHandle request_command_buffer();
	void submit(CommandBufferHandle cmd, Fence *fence = nullptr, Semaphore *semaphore = nullptr);

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
	void free_memory(const DeviceAllocation &alloc);

	VkSemaphore set_acquire(VkSemaphore acquire);
	VkSemaphore set_release(VkSemaphore release);
	bool swapchain_touched() const;

	bool format_is_supported(VkFormat format, VkFormatFeatureFlags required) const;
	VkFormat get_default_depth_stencil_format() const;
	VkFormat get_default_depth_format() const;
	ImageView &get_transient_attachment(unsigned width, unsigned height, VkFormat format, unsigned index = 0);

	PipelineLayout *request_pipeline_layout(const CombinedResourceLayout &layout);
	DescriptorSetAllocator *request_descriptor_set_allocator(const DescriptorSetLayout &layout);
	const Framebuffer &request_framebuffer(const RenderPassInfo &info);
	const RenderPass &request_render_pass(const RenderPassInfo &info);

	uint64_t allocate_cookie()
	{
		return ++cookie;
	}

	RenderPassInfo get_swapchain_render_pass(SwapchainRenderPass style);
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
	void add_wait_semaphore(Semaphore semaphore, VkPipelineStageFlags stages);

private:
	VkInstance instance = VK_NULL_HANDLE;
	VkPhysicalDevice gpu = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	VkQueue queue = VK_NULL_HANDLE;
	DeviceAllocator allocator;
	uint64_t cookie = 0;

	VkPhysicalDeviceMemoryProperties mem_props;
	VkPhysicalDeviceProperties gpu_props;
	void init_stock_samplers();

	struct PerFrame
	{
		PerFrame(Device *device, DeviceAllocator &global, SemaphoreManager &semaphore_manager,
		         uint32_t queue_family_index);
		~PerFrame();
		void operator=(const PerFrame &) = delete;
		PerFrame(const PerFrame &) = delete;

		void cleanup();
		void begin();

		VkDevice device;
		DeviceAllocator &global_allocator;
		SemaphoreManager &semaphore_manager;
		CommandPool cmd_pool;
		ImageHandle backbuffer;
		FenceManager fence_manager;

		ChainAllocator vbo_chain, ibo_chain, ubo_chain, staging_chain;

		std::vector<DeviceAllocation> allocations;
		std::vector<VkFramebuffer> destroyed_framebuffers;
		std::vector<VkSampler> destroyed_samplers;
		std::vector<VkPipeline> destroyed_pipelines;
		std::vector<VkImageView> destroyed_image_views;
		std::vector<VkBufferView> destroyed_buffer_views;
		std::vector<VkImage> destroyed_images;
		std::vector<VkBuffer> destroyed_buffers;
		std::vector<CommandBufferHandle> submissions;
		std::vector<std::shared_ptr<FenceHolder>> fences;
		std::vector<VkSemaphore> recycled_semaphores;
		std::vector<VkSemaphore> destroyed_semaphores;
		bool swapchain_touched = false;
	};
	SemaphoreManager semaphore_manager;
	VkSemaphore wsi_acquire = VK_NULL_HANDLE;
	VkSemaphore wsi_release = VK_NULL_HANDLE;

	std::vector<Semaphore> wait_semaphores;
	std::vector<VkPipelineStageFlags> wait_stages;

	CommandBufferHandle staging_cmd;
	void begin_staging();
	void submit_queue(Fence *fence, Semaphore *semaphore);

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
	uint32_t queue_family_index = 0;

	uint32_t find_memory_type(BufferDomain domain, uint32_t mask);
	uint32_t find_memory_type(ImageDomain domain, uint32_t mask);
	bool memory_type_is_device_optimal(uint32_t type) const;
	bool memory_type_is_host_visible(uint32_t type) const;

	SamplerHandle samplers[static_cast<unsigned>(StockSampler::Count)];
	HashMap<std::unique_ptr<PipelineLayout>> pipeline_layouts;
	HashMap<std::unique_ptr<DescriptorSetAllocator>> descriptor_set_allocators;
	FramebufferAllocator framebuffer_allocator;
	TransientAllocator transient_allocator;
	HashMap<std::unique_ptr<RenderPass>> render_passes;
	VkPipelineCache pipeline_cache = VK_NULL_HANDLE;
};
}
