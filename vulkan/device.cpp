#include "device.hpp"
#include "format.hpp"
#include <algorithm>
#include <string.h>

using namespace std;

namespace Vulkan
{
Device::Device()
    : framebuffer_allocator(this)
    , transient_allocator(this)
{
}

Semaphore Device::request_semaphore()
{
	auto semaphore = semaphore_manager.request_cleared_semaphore();
	auto ptr = make_handle<SemaphoreHolder>(this, semaphore);
	return ptr;
}

void Device::add_wait_semaphore(Semaphore semaphore, VkPipelineStageFlags stages)
{
	wait_semaphores.push_back(semaphore);
	wait_stages.push_back(stages);
}

void *Device::map_host_buffer(Buffer &buffer, MemoryAccessFlags access)
{
	void *host = allocator.map_memory(&buffer.get_allocation(), access);
	return host;
}

void Device::unmap_host_buffer(const Buffer &buffer)
{
	allocator.unmap_memory(buffer.get_allocation());
}

ShaderHandle Device::create_shader(ShaderStage stage, const uint32_t *data, size_t size)
{
	return make_handle<Shader>(device, stage, data, size);
}

ProgramHandle Device::create_program(const uint32_t *compute_data, size_t compute_size)
{
	auto compute = make_handle<Shader>(device, ShaderStage::Compute, compute_data, compute_size);
	auto program = make_handle<Program>(this);
	program->set_shader(compute);
	bake_program(*program);
	return program;
}

ProgramHandle Device::create_program(const uint32_t *vertex_data, size_t vertex_size, const uint32_t *fragment_data,
                                     size_t fragment_size)
{
	auto vertex = make_handle<Shader>(device, ShaderStage::Vertex, vertex_data, vertex_size);
	auto fragment = make_handle<Shader>(device, ShaderStage::Fragment, fragment_data, fragment_size);
	auto program = make_handle<Program>(this);
	program->set_shader(vertex);
	program->set_shader(fragment);
	bake_program(*program);
	return program;
}

PipelineLayout *Device::request_pipeline_layout(const CombinedResourceLayout &layout)
{
	Hasher h;
	h.data(reinterpret_cast<const uint32_t *>(layout.sets), sizeof(layout.sets));
	h.data(reinterpret_cast<const uint32_t *>(layout.ranges), sizeof(layout.ranges));
	h.u32(layout.attribute_mask);

	auto hash = h.get();
	auto itr = pipeline_layouts.find(hash);
	if (itr != end(pipeline_layouts))
		return itr->second.get();

	auto *pipe = new PipelineLayout(this, layout);
	pipeline_layouts.insert(make_pair(hash, unique_ptr<PipelineLayout>(pipe)));
	return pipe;
}

DescriptorSetAllocator *Device::request_descriptor_set_allocator(const DescriptorSetLayout &layout)
{
	Hasher h;
	h.data(reinterpret_cast<const uint32_t *>(&layout), sizeof(layout));
	auto hash = h.get();
	auto itr = descriptor_set_allocators.find(hash);
	if (itr != end(descriptor_set_allocators))
		return itr->second.get();

	auto *allocator = new DescriptorSetAllocator(this, layout);
	descriptor_set_allocators.insert(make_pair(hash, unique_ptr<DescriptorSetAllocator>(allocator)));
	return allocator;
}

void Device::bake_program(Program &program)
{
	CombinedResourceLayout layout;
	if (program.get_shader(ShaderStage::Vertex))
		layout.attribute_mask = program.get_shader(ShaderStage::Vertex)->get_layout().attribute_mask;

	layout.descriptor_set_mask = 0;

	for (unsigned i = 0; i < static_cast<unsigned>(ShaderStage::Count); i++)
	{
		auto *shader = program.get_shader(static_cast<ShaderStage>(i));
		if (!shader)
			continue;

		auto &shader_layout = shader->get_layout();
		for (unsigned set = 0; set < VULKAN_NUM_DESCRIPTOR_SETS; set++)
		{
			layout.sets[set].sampled_image_mask |= shader_layout.sets[set].sampled_image_mask;
			layout.sets[set].storage_image_mask |= shader_layout.sets[set].storage_image_mask;
			layout.sets[set].uniform_buffer_mask |= shader_layout.sets[set].uniform_buffer_mask;
			layout.sets[set].storage_buffer_mask |= shader_layout.sets[set].storage_buffer_mask;
			layout.sets[set].sampled_buffer_mask |= shader_layout.sets[set].sampled_buffer_mask;
			layout.sets[set].input_attachment_mask |= shader_layout.sets[set].input_attachment_mask;
			layout.sets[set].stages |= shader_layout.sets[set].stages;
		}

		layout.ranges[i].stageFlags = 1u << i;
		layout.ranges[i].offset = shader_layout.push_constant_offset;
		layout.ranges[i].size = shader_layout.push_constant_range;
	}

	for (unsigned i = 0; i < VULKAN_NUM_DESCRIPTOR_SETS; i++)
	{
		if (layout.sets[i].stages != 0)
			layout.descriptor_set_mask |= 1u << i;
	}

	Hasher h;
	h.data(reinterpret_cast<uint32_t *>(layout.ranges), sizeof(layout.ranges));
	layout.push_constant_layout_hash = h.get();

	program.set_pipeline_layout(request_pipeline_layout(layout));

	if (program.get_shader(ShaderStage::Compute))
	{
		auto &shader = *program.get_shader(ShaderStage::Compute);
		VkComputePipelineCreateInfo info = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
		info.layout = program.get_pipeline_layout()->get_layout();
		info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		info.stage.module = shader.get_module();
		info.stage.pName = "main";
		info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;

		VkPipeline compute_pipeline;
		if (vkCreateComputePipelines(device, pipeline_cache, 1, &info, nullptr, &compute_pipeline) != VK_SUCCESS)
			LOG("Failed to create compute pipeline!\n");
		program.set_compute_pipeline(compute_pipeline);
	}
}

void Device::set_context(const Context &context)
{
	instance = context.get_instance();
	gpu = context.get_gpu();
	device = context.get_device();
	queue_family_index = context.get_queue_family();
	queue = context.get_queue();

	mem_props = context.get_mem_props();
	gpu_props = context.get_gpu_props();

	allocator.init(gpu, device);
	init_stock_samplers();

	VkPipelineCacheCreateInfo info = { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
	vkCreatePipelineCache(device, &info, nullptr, &pipeline_cache);

	semaphore_manager.init(device);
}

void Device::init_stock_samplers()
{
	SamplerCreateInfo info = {};
	info.maxLod = VK_LOD_CLAMP_NONE;

	for (unsigned i = 0; i < static_cast<unsigned>(StockSampler::Count); i++)
	{
		auto mode = static_cast<StockSampler>(i);

		switch (mode)
		{
		case StockSampler::NearestShadow:
		case StockSampler::LinearShadow:
			info.compareEnable = true;
			info.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
			break;

		default:
			info.compareEnable = false;
			break;
		}

		switch (mode)
		{
		case StockSampler::TrilinearClamp:
		case StockSampler::TrilinearWrap:
			info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			break;

		default:
			info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
			break;
		}

		switch (mode)
		{
		case StockSampler::LinearClamp:
		case StockSampler::LinearWrap:
		case StockSampler::TrilinearClamp:
		case StockSampler::TrilinearWrap:
		case StockSampler::LinearShadow:
			info.magFilter = VK_FILTER_LINEAR;
			info.minFilter = VK_FILTER_LINEAR;
			break;

		default:
			info.magFilter = VK_FILTER_NEAREST;
			info.minFilter = VK_FILTER_NEAREST;
			break;
		}

		switch (mode)
		{
		default:
		case StockSampler::LinearWrap:
		case StockSampler::NearestWrap:
		case StockSampler::TrilinearWrap:
			info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			break;

		case StockSampler::LinearClamp:
		case StockSampler::NearestClamp:
		case StockSampler::TrilinearClamp:
		case StockSampler::NearestShadow:
		case StockSampler::LinearShadow:
			info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			break;
		}
		samplers[i] = create_sampler(info);
	}
}

void Device::submit(CommandBufferHandle cmd, Fence *fence, Semaphore *semaphore)
{
	if (staging_cmd)
	{
		frame().cmd_pool.signal_submitted(staging_cmd->get_command_buffer());
		vkEndCommandBuffer(staging_cmd->get_command_buffer());
		frame().submissions.push_back(staging_cmd);
		staging_cmd.reset();
	}

	frame().cmd_pool.signal_submitted(cmd->get_command_buffer());
	vkEndCommandBuffer(cmd->get_command_buffer());
	frame().submissions.push_back(move(cmd));

	if (fence || semaphore)
		submit_queue(fence, semaphore);
}

void Device::submit_queue(Fence *fence, Semaphore *semaphore)
{
	if (frame().submissions.empty())
		return;

	vector<VkCommandBuffer> cmds;
	cmds.reserve(frame().submissions.size());

	vector<VkSubmitInfo> submits;
	submits.reserve(2);
	size_t last_cmd = 0;

	vector<VkSemaphore> waits[2];
	vector<VkSemaphore> signals[2];
	vector<VkFlags> stages[2];

	// Add external wait semaphores.
	stages[0] = wait_stages;
	for (auto &semaphore : wait_semaphores)
	{
		auto wait = semaphore->consume();
		frame().recycled_semaphores.push_back(wait);
		waits[0].push_back(wait);
	}
	wait_stages.clear();
	wait_semaphores.clear();

	for (auto &cmd : frame().submissions)
	{
		if (cmd->swapchain_touched() && !frame().swapchain_touched)
		{
			if (!cmds.empty())
			{
				// Push all pending cmd buffers to their own submission.
				submits.emplace_back();

				auto &submit = submits.back();
				memset(&submit, 0, sizeof(submit));
				submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
				submit.pNext = nullptr;
				submit.commandBufferCount = cmds.size() - last_cmd;
				submit.pCommandBuffers = cmds.data() + last_cmd;
				last_cmd = cmds.size();
			}
			frame().swapchain_touched = true;
		}

		cmds.push_back(cmd->get_command_buffer());
	}

	if (cmds.size() > last_cmd)
	{
		unsigned index = submits.size();

		// Push all pending cmd buffers to their own submission.
		submits.emplace_back();

		auto &submit = submits.back();
		memset(&submit, 0, sizeof(submit));
		submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit.pNext = nullptr;
		submit.commandBufferCount = cmds.size() - last_cmd;
		submit.pCommandBuffers = cmds.data() + last_cmd;
		if (frame().swapchain_touched)
		{
			static const VkFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			waits[index].push_back(wsi_acquire);
			stages[index].push_back(wait);
			signals[index].push_back(wsi_release);
		}
		last_cmd = cmds.size();
	}
	VkFence cleared_fence = frame().fence_manager.request_cleared_fence();

	VkSemaphore cleared_semaphore = VK_NULL_HANDLE;
	if (semaphore)
	{
		cleared_semaphore = semaphore_manager.request_cleared_semaphore();
	}

	for (unsigned i = 0; i < submits.size(); i++)
	{
		auto &submit = submits[i];
		submit.waitSemaphoreCount = waits[i].size();
		if (!waits[i].empty())
		{
			submit.pWaitSemaphores = waits[i].data();
			submit.pWaitDstStageMask = stages[i].data();
		}

		submit.signalSemaphoreCount = signals[i].size();
		if (!signals[i].empty())
			submit.pSignalSemaphores = signals[i].data();
	}

	VkResult result = vkQueueSubmit(queue, submits.size(), submits.data(), cleared_fence);
	if (result != VK_SUCCESS)
		LOG("vkQueueSubmit failed.\n");
	frame().submissions.clear();

	if (fence)
	{
		auto ptr = make_shared<FenceHolder>(this, cleared_fence);
		*fence = ptr;
		frame().fences.push_back(move(ptr));
	}

	if (semaphore)
	{
		auto ptr = make_handle<SemaphoreHolder>(this, cleared_semaphore);
		*semaphore = ptr;
	}
}

void Device::flush_frame()
{
	if (staging_cmd)
	{
		frame().cmd_pool.signal_submitted(staging_cmd->get_command_buffer());
		vkEndCommandBuffer(staging_cmd->get_command_buffer());
		frame().submissions.push_back(staging_cmd);
		staging_cmd.reset();
	}

	submit_queue(nullptr, nullptr);
}

void Device::begin_staging()
{
	if (!staging_cmd)
		staging_cmd = request_command_buffer();
}

CommandBufferHandle Device::request_command_buffer()
{
	auto cmd = frame().cmd_pool.request_command_buffer();

	VkCommandBufferBeginInfo info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(cmd, &info);
	return make_handle<CommandBuffer>(this, cmd, pipeline_cache);
}

VkSemaphore Device::set_acquire(VkSemaphore acquire)
{
	swap(acquire, wsi_acquire);
	return acquire;
}

VkSemaphore Device::set_release(VkSemaphore release)
{
	swap(release, wsi_release);
	return release;
}

const Sampler &Device::get_stock_sampler(StockSampler sampler) const
{
	return *samplers[static_cast<unsigned>(sampler)];
}

bool Device::swapchain_touched() const
{
	return frame().swapchain_touched;
}

Device::~Device()
{
	wait_idle();

	if (pipeline_cache != VK_NULL_HANDLE)
		vkDestroyPipelineCache(device, pipeline_cache, nullptr);

	framebuffer_allocator.clear();
	transient_allocator.clear();
	for (auto &sampler : samplers)
		sampler.reset();

	for (auto &frame : per_frame)
		frame->cleanup();
}

void Device::init_virtual_swapchain(unsigned num_swapchain_images)
{
	wait_idle();

	// Clear out caches which might contain stale data from now on.
	framebuffer_allocator.clear();
	transient_allocator.clear();

	for (auto &frame : per_frame)
		frame->cleanup();
	per_frame.clear();

	for (unsigned i = 0; i < num_swapchain_images; i++)
		per_frame.emplace_back(new PerFrame(this, allocator, semaphore_manager, queue_family_index));
}

void Device::init_swapchain(const vector<VkImage> swapchain_images, unsigned width, unsigned height, VkFormat format)
{
	wait_idle();

	// Clear out caches which might contain stale data from now on.
	framebuffer_allocator.clear();
	transient_allocator.clear();

	for (auto &frame : per_frame)
		frame->cleanup();
	per_frame.clear();

	const auto info = ImageCreateInfo::render_target(width, height, format);

	for (auto &image : swapchain_images)
	{
		auto frame = unique_ptr<PerFrame>(new PerFrame(this, allocator, semaphore_manager, queue_family_index));

		VkImageViewCreateInfo view_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		view_info.image = image;
		view_info.format = format;
		view_info.components.r = VK_COMPONENT_SWIZZLE_R;
		view_info.components.g = VK_COMPONENT_SWIZZLE_G;
		view_info.components.b = VK_COMPONENT_SWIZZLE_B;
		view_info.components.a = VK_COMPONENT_SWIZZLE_A;
		view_info.subresourceRange.aspectMask = format_to_aspect_mask(format);
		view_info.subresourceRange.baseMipLevel = 0;
		view_info.subresourceRange.baseArrayLayer = 0;
		view_info.subresourceRange.levelCount = 1;
		view_info.subresourceRange.layerCount = 1;
		view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;

		VkImageView image_view;
		if (vkCreateImageView(device, &view_info, nullptr, &image_view) != VK_SUCCESS)
			LOG("Failed to create view for backbuffer.");

		frame->backbuffer = make_handle<Image>(this, image, image_view, DeviceAllocation{}, info);
		per_frame.emplace_back(move(frame));
	}
}

Device::PerFrame::PerFrame(Device *device, DeviceAllocator &global, SemaphoreManager &semaphore_manager,
                           uint32_t queue_family_index)
    : device(device->get_device())
    , global_allocator(global)
    , semaphore_manager(semaphore_manager)
    , cmd_pool(device->get_device(), queue_family_index)
    , fence_manager(device->get_device())
    , vbo_chain(device, 1024 * 1024, 64, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
    , ibo_chain(device, 1024 * 1024, 64, VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
    , ubo_chain(device, 1024 * 1024, device->get_gpu_properties().limits.minUniformBufferOffsetAlignment,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
    , staging_chain(device, 4 * 1024 * 1024, 64, VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
{
}

void Device::free_memory(const DeviceAllocation &alloc)
{
	frame().allocations.push_back(alloc);
}

#ifdef VULKAN_DEBUG

template <typename T, typename U>
static inline bool exists(const T &container, const U &value)
{
	return find(begin(container), end(container), value) != end(container);
}

#endif

void Device::destroy_pipeline(VkPipeline pipeline)
{
	VK_ASSERT(!exists(frame().destroyed_pipelines, pipeline));
	frame().destroyed_pipelines.push_back(pipeline);
}

void Device::destroy_image_view(VkImageView view)
{
	VK_ASSERT(!exists(frame().destroyed_image_views, view));
	frame().destroyed_image_views.push_back(view);
}

void Device::destroy_buffer_view(VkBufferView view)
{
	VK_ASSERT(!exists(frame().destroyed_buffer_views, view));
	frame().destroyed_buffer_views.push_back(view);
}

void Device::destroy_semaphore(VkSemaphore semaphore)
{
	VK_ASSERT(!exists(frame().destroyed_semaphores, semaphore));
	frame().destroyed_semaphores.push_back(semaphore);
}

void Device::destroy_image(VkImage image)
{
	VK_ASSERT(!exists(frame().destroyed_images, image));
	frame().destroyed_images.push_back(image);
}

void Device::destroy_buffer(VkBuffer buffer)
{
	VK_ASSERT(!exists(frame().destroyed_buffers, buffer));
	frame().destroyed_buffers.push_back(buffer);
}

void Device::destroy_sampler(VkSampler sampler)
{
	VK_ASSERT(!exists(frame().destroyed_samplers, sampler));
	frame().destroyed_samplers.push_back(sampler);
}

void Device::destroy_framebuffer(VkFramebuffer framebuffer)
{
	VK_ASSERT(!exists(frame().destroyed_framebuffers, framebuffer));
	frame().destroyed_framebuffers.push_back(framebuffer);
}

void Device::wait_idle()
{
	if (!per_frame.empty())
		flush_frame();

	vkDeviceWaitIdle(device);
	for (auto &frame : per_frame)
	{
		// Avoid double-wait-on-semaphore scenarios.
		bool touched_swapchain = frame->swapchain_touched;
		frame->begin();
		frame->swapchain_touched = touched_swapchain;
	}

	framebuffer_allocator.clear();
	transient_allocator.clear();
	for (auto &allocator : descriptor_set_allocators)
		allocator.second->clear();
}

void Device::begin_frame(unsigned index)
{
	current_swapchain_index = index;

	// Flush the frame here as we might have pending staging command buffers from init stage.
	flush_frame();

	frame().begin();
	framebuffer_allocator.begin_frame();
	transient_allocator.begin_frame();
	for (auto &allocator : descriptor_set_allocators)
		allocator.second->begin_frame();
}

void Device::PerFrame::begin()
{
	ubo_chain.discard();
	staging_chain.discard();
	vbo_chain.discard();
	ibo_chain.discard();
	fence_manager.begin();
	cmd_pool.begin();

	for (auto &framebuffer : destroyed_framebuffers)
		vkDestroyFramebuffer(device, framebuffer, nullptr);
	for (auto &sampler : destroyed_samplers)
		vkDestroySampler(device, sampler, nullptr);
	for (auto &pipeline : destroyed_pipelines)
		vkDestroyPipeline(device, pipeline, nullptr);
	for (auto &view : destroyed_image_views)
		vkDestroyImageView(device, view, nullptr);
	for (auto &view : destroyed_buffer_views)
		vkDestroyBufferView(device, view, nullptr);
	for (auto &image : destroyed_images)
		vkDestroyImage(device, image, nullptr);
	for (auto &buffer : destroyed_buffers)
		vkDestroyBuffer(device, buffer, nullptr);
	for (auto &semaphore : destroyed_semaphores)
		vkDestroySemaphore(device, semaphore, nullptr);
	for (auto &semaphore : recycled_semaphores)
		semaphore_manager.recycle(semaphore);
	for (auto &alloc : allocations)
		alloc.free_immediate(global_allocator);

	destroyed_framebuffers.clear();
	destroyed_samplers.clear();
	destroyed_pipelines.clear();
	destroyed_image_views.clear();
	destroyed_buffer_views.clear();
	destroyed_images.clear();
	destroyed_buffers.clear();
	destroyed_semaphores.clear();
	recycled_semaphores.clear();
	allocations.clear();
	fences.clear();

	swapchain_touched = false;
}

void Device::PerFrame::cleanup()
{
	backbuffer.reset();
	vbo_chain.reset();
	ibo_chain.reset();
	ubo_chain.reset();
	staging_chain.reset();
}

Device::PerFrame::~PerFrame()
{
	cleanup();
	begin();
}

ChainDataAllocation Device::allocate_constant_data(VkDeviceSize size)
{
	return frame().ubo_chain.allocate(size);
}

ChainDataAllocation Device::allocate_vertex_data(VkDeviceSize size)
{
	return frame().vbo_chain.allocate(size);
}

ChainDataAllocation Device::allocate_index_data(VkDeviceSize size)
{
	return frame().ibo_chain.allocate(size);
}

ChainDataAllocation Device::allocate_staging_data(VkDeviceSize size)
{
	return frame().staging_chain.allocate(size);
}

uint32_t Device::find_memory_type(BufferDomain domain, uint32_t mask)
{
	uint32_t desired = 0, fallback = 0;
	switch (domain)
	{
	case BufferDomain::Device:
		desired = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		fallback = 0;
		break;

	case BufferDomain::Host:
		desired = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
		          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		fallback = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		break;

	case BufferDomain::CachedHost:
		desired = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
		fallback = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
		break;
	}

	for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++)
	{
		if ((1u << i) & mask)
		{
			uint32_t flags = mem_props.memoryTypes[i].propertyFlags;
			if ((flags & desired) == desired)
				return i;
		}
	}

	for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++)
	{
		if ((1u << i) & mask)
		{
			uint32_t flags = mem_props.memoryTypes[i].propertyFlags;
			if ((flags & fallback) == fallback)
				return i;
		}
	}

	throw runtime_error("Couldn't find memory type.");
}

uint32_t Device::find_memory_type(ImageDomain domain, uint32_t mask)
{
	uint32_t desired = 0, fallback = 0;
	switch (domain)
	{
	case ImageDomain::Physical:
		desired = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		fallback = 0;
		break;

	case ImageDomain::Transient:
		desired = VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
		fallback = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		break;
	}

	for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++)
	{
		if ((1u << i) & mask)
		{
			uint32_t flags = mem_props.memoryTypes[i].propertyFlags;
			if ((flags & desired) == desired)
				return i;
		}
	}

	for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++)
	{
		if ((1u << i) & mask)
		{
			uint32_t flags = mem_props.memoryTypes[i].propertyFlags;
			if ((flags & fallback) == fallback)
				return i;
		}
	}

	throw runtime_error("Couldn't find memory type.");
}

static inline VkImageViewType get_image_view_type(const ImageCreateInfo &create_info, const ImageViewCreateInfo *view)
{
	unsigned layers = view ? view->layers : create_info.layers;
	unsigned levels = view ? view->levels : create_info.levels;
	unsigned base_level = view ? view->base_level : 0;
	unsigned base_layer = view ? view->base_layer : 0;

	if (layers == VK_REMAINING_ARRAY_LAYERS)
		layers = create_info.layers - base_layer;
	if (levels == VK_REMAINING_MIP_LEVELS)
		levels = create_info.levels - base_level;

	bool force_array =
	    view ? (view->misc & IMAGE_VIEW_MISC_FORCE_ARRAY_BIT) : (create_info.misc & IMAGE_MISC_FORCE_ARRAY_BIT);

	switch (create_info.type)
	{
	case VK_IMAGE_TYPE_1D:
		VK_ASSERT(create_info.width >= 1);
		VK_ASSERT(create_info.height == 1);
		VK_ASSERT(create_info.depth == 1);
		VK_ASSERT(create_info.samples == VK_SAMPLE_COUNT_1_BIT);

		if (layers > 1 || force_array)
			return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
		else
			return VK_IMAGE_VIEW_TYPE_1D;

	case VK_IMAGE_TYPE_2D:
		VK_ASSERT(create_info.width >= 1);
		VK_ASSERT(create_info.height >= 1);
		VK_ASSERT(create_info.depth == 1);

		if ((create_info.flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) && (layers % 6) == 0)
		{
			VK_ASSERT(create_info.width == create_info.height);

			if (layers > 6 || force_array)
				return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
			else
				return VK_IMAGE_VIEW_TYPE_CUBE;
		}
		else
		{
			if (layers > 1 || force_array)
				return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
			else
				return VK_IMAGE_VIEW_TYPE_2D;
		}

	case VK_IMAGE_TYPE_3D:
		VK_ASSERT(create_info.width >= 1);
		VK_ASSERT(create_info.height >= 1);
		VK_ASSERT(create_info.depth >= 1);
		return VK_IMAGE_VIEW_TYPE_3D;

	default:
		VK_ASSERT(0 && "bogus");
		return VK_IMAGE_VIEW_TYPE_RANGE_SIZE;
	}
}

BufferViewHandle Device::create_buffer_view(const BufferViewCreateInfo &view_info)
{
	VkBufferViewCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO };
	info.buffer = view_info.buffer->get_buffer();
	info.format = view_info.format;
	info.offset = view_info.offset;
	info.range = view_info.range;

	VkBufferView view;
	auto res = vkCreateBufferView(device, &info, nullptr, &view);
	if (res != VK_SUCCESS)
		return nullptr;

	return make_handle<BufferView>(this, view, view_info);
}

ImageViewHandle Device::create_image_view(const ImageViewCreateInfo &create_info)
{
	auto &image_create_info = create_info.image->get_create_info();

	VkFormat format = create_info.format != VK_FORMAT_UNDEFINED ? create_info.format : image_create_info.format;

	VkImageViewCreateInfo view_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	view_info.image = create_info.image->get_image();
	view_info.format = format;
	view_info.components = create_info.swizzle;
	view_info.subresourceRange.aspectMask = format_to_aspect_mask(format);
	view_info.subresourceRange.baseMipLevel = create_info.base_level;
	view_info.subresourceRange.baseArrayLayer = create_info.base_layer;
	view_info.subresourceRange.levelCount = create_info.levels;
	view_info.subresourceRange.layerCount = create_info.layers;
	view_info.viewType = get_image_view_type(image_create_info, &create_info);

	VkImageView image_view;
	if (vkCreateImageView(device, &view_info, nullptr, &image_view) != VK_SUCCESS)
		return nullptr;

	ImageViewCreateInfo tmp = create_info;
	tmp.format = format;
	return make_handle<ImageView>(this, image_view, tmp);
}

ImageHandle Device::create_image(const ImageCreateInfo &create_info, const ImageInitialData *initial)
{
	VkImage image;
	VkMemoryRequirements reqs;
	DeviceAllocation allocation;

	VkImageCreateInfo info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	info.format = create_info.format;
	info.extent.width = create_info.width;
	info.extent.height = create_info.height;
	info.extent.depth = create_info.depth;
	info.imageType = create_info.type;
	info.mipLevels = create_info.levels;
	info.arrayLayers = create_info.layers;
	info.samples = create_info.samples;
	info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	info.tiling = VK_IMAGE_TILING_OPTIMAL;
	info.usage = create_info.usage;
	if (create_info.domain == ImageDomain::Transient)
		info.usage |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
	if (initial)
		info.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

	if (create_info.usage & VK_IMAGE_USAGE_STORAGE_BIT)
		info.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;

	if (info.mipLevels == 0)
		info.mipLevels = image_num_miplevels(info.extent);

	VK_ASSERT(format_is_supported(create_info.format, image_usage_to_features(info.usage)));

	if (vkCreateImage(device, &info, nullptr, &image) != VK_SUCCESS)
		return nullptr;

	vkGetImageMemoryRequirements(device, image, &reqs);

	uint32_t memory_type = find_memory_type(create_info.domain, reqs.memoryTypeBits);
	if (!allocator.allocate(reqs.size, reqs.alignment, memory_type, ALLOCATION_TILING_OPTIMAL, &allocation))
	{
		vkDestroyImage(device, image, nullptr);
		return nullptr;
	}

	if (vkBindImageMemory(device, image, allocation.get_memory(), allocation.get_offset()) != VK_SUCCESS)
	{
		allocation.free_immediate(allocator);
		vkDestroyImage(device, image, nullptr);
		return nullptr;
	}

	auto tmpinfo = create_info;
	tmpinfo.usage = info.usage;
	tmpinfo.levels = info.mipLevels;

	// Create a default image view.
	VkImageView image_view = VK_NULL_HANDLE;
	if (info.usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
	                  VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT))
	{
		VkImageViewCreateInfo view_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		view_info.image = image;
		view_info.format = create_info.format;
		view_info.components.r = VK_COMPONENT_SWIZZLE_R;
		view_info.components.g = VK_COMPONENT_SWIZZLE_G;
		view_info.components.b = VK_COMPONENT_SWIZZLE_B;
		view_info.components.a = VK_COMPONENT_SWIZZLE_A;
		view_info.subresourceRange.aspectMask = format_to_aspect_mask(view_info.format);
		view_info.subresourceRange.baseMipLevel = 0;
		view_info.subresourceRange.baseArrayLayer = 0;
		view_info.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
		view_info.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
		view_info.viewType = get_image_view_type(create_info, nullptr);

		if (vkCreateImageView(device, &view_info, nullptr, &image_view) != VK_SUCCESS)
		{
			allocation.free_immediate(allocator);
			vkDestroyImage(device, image, nullptr);
			return nullptr;
		}
	}

	auto handle = make_handle<Image>(this, image, image_view, allocation, tmpinfo);

	// Set possible dstStage and dstAccess.
	handle->set_stage_flags(image_usage_to_possible_stages(info.usage));
	handle->set_access_flags(image_usage_to_possible_access(info.usage));

	// Copy initial data to texture.
	if (initial)
	{
		begin_staging();

		VK_ASSERT(create_info.domain != ImageDomain::Transient);
		VK_ASSERT(create_info.initial_layout != VK_IMAGE_LAYOUT_UNDEFINED);
		bool generate_mips = (create_info.misc & IMAGE_MISC_GENERATE_MIPS_BIT) != 0;
		unsigned copy_levels = generate_mips ? 1u : info.mipLevels;

		staging_cmd->image_barrier(*handle, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
		                           VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT,
		                           VK_ACCESS_TRANSFER_WRITE_BIT);
		handle->set_layout(VK_IMAGE_LAYOUT_GENERAL);

		VkExtent3D extent = { create_info.width, create_info.height, create_info.depth };

		VkImageSubresourceLayers subresource = {
			format_to_aspect_mask(info.format), 0, 0, create_info.layers,
		};

		for (unsigned i = 0; i < copy_levels; i++)
		{
			uint32_t row_length = initial[i].row_length ? initial[i].row_length : extent.width;
			uint32_t array_height = initial[i].array_height ? initial[i].array_height : extent.height;
			VkDeviceSize size =
			    format_pixel_size(create_info.format) * create_info.layers * extent.depth * row_length * array_height;

			subresource.mipLevel = i;
			auto *ptr = staging_cmd->update_image(*handle, { 0, 0, 0 }, extent, row_length, array_height, subresource);
			VK_ASSERT(ptr);
			memcpy(ptr, initial[i].data, size);

			extent.width = max(extent.width >> 1u, 1u);
			extent.height = max(extent.height >> 1u, 1u);
			extent.depth = max(extent.depth >> 1u, 1u);
		}

		if (generate_mips)
		{
			staging_cmd->image_barrier(*handle, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
			                           VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
			staging_cmd->generate_mipmap(*handle);
		}

		staging_cmd->image_barrier(
		    *handle, VK_IMAGE_LAYOUT_GENERAL, create_info.initial_layout, VK_PIPELINE_STAGE_TRANSFER_BIT,
		    VK_ACCESS_TRANSFER_WRITE_BIT, handle->get_stage_flags(),
		    handle->get_access_flags() & image_layout_to_possible_access(create_info.initial_layout));
	}
	else if (create_info.initial_layout != VK_IMAGE_LAYOUT_UNDEFINED)
	{
		begin_staging();

		VK_ASSERT(create_info.domain != ImageDomain::Transient);
		staging_cmd->image_barrier(*handle, VK_IMAGE_LAYOUT_UNDEFINED, create_info.initial_layout,
		                           VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, handle->get_stage_flags(),
		                           handle->get_access_flags() &
		                               image_layout_to_possible_access(create_info.initial_layout));
	}
	handle->set_layout(create_info.initial_layout);

	return handle;
}

SamplerHandle Device::create_sampler(const SamplerCreateInfo &sampler_info)
{
	VkSamplerCreateInfo info = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };

	info.magFilter = sampler_info.magFilter;
	info.minFilter = sampler_info.minFilter;
	info.mipmapMode = sampler_info.mipmapMode;
	info.addressModeU = sampler_info.addressModeU;
	info.addressModeV = sampler_info.addressModeV;
	info.addressModeW = sampler_info.addressModeW;
	info.mipLodBias = sampler_info.mipLodBias;
	info.anisotropyEnable = sampler_info.anisotropyEnable;
	info.maxAnisotropy = sampler_info.maxAnisotropy;
	info.compareEnable = sampler_info.compareEnable;
	info.compareOp = sampler_info.compareOp;
	info.minLod = sampler_info.minLod;
	info.maxLod = sampler_info.maxLod;
	info.borderColor = sampler_info.borderColor;
	info.unnormalizedCoordinates = sampler_info.unnormalizedCoordinates;

	VkSampler sampler;
	if (vkCreateSampler(device, &info, nullptr, &sampler) != VK_SUCCESS)
		return nullptr;
	return make_handle<Sampler>(this, sampler, sampler_info);
}

BufferHandle Device::create_buffer(const BufferCreateInfo &create_info, const void *initial)
{
	VkBuffer buffer;
	VkMemoryRequirements reqs;
	DeviceAllocation allocation;

	VkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	info.size = create_info.size;
	info.usage = create_info.usage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	if (vkCreateBuffer(device, &info, nullptr, &buffer) != VK_SUCCESS)
		return nullptr;

	vkGetBufferMemoryRequirements(device, buffer, &reqs);

	uint32_t memory_type = find_memory_type(create_info.domain, reqs.memoryTypeBits);

	if (!allocator.allocate(reqs.size, reqs.alignment, memory_type, ALLOCATION_TILING_LINEAR, &allocation))
	{
		vkDestroyBuffer(device, buffer, nullptr);
		return nullptr;
	}

	if (vkBindBufferMemory(device, buffer, allocation.get_memory(), allocation.get_offset()) != VK_SUCCESS)
	{
		allocation.free_immediate(allocator);
		vkDestroyBuffer(device, buffer, nullptr);
		return nullptr;
	}

	auto tmpinfo = create_info;
	tmpinfo.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	auto handle = make_handle<Buffer>(this, buffer, allocation, tmpinfo);

	if (create_info.domain == BufferDomain::Device && initial && !memory_type_is_host_visible(memory_type))
	{
		begin_staging();

		auto *ptr = staging_cmd->update_buffer(*handle, 0, create_info.size);
		VK_ASSERT(ptr);
		memcpy(ptr, initial, create_info.size);
		staging_cmd->buffer_barrier(*handle, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
		                            buffer_usage_to_possible_stages(info.usage),
		                            buffer_usage_to_possible_access(info.usage));
	}
	else if (initial)
	{
		void *ptr = allocator.map_memory(&allocation, MEMORY_ACCESS_WRITE);
		if (!ptr)
			return nullptr;
		memcpy(ptr, initial, create_info.size);
		allocator.unmap_memory(allocation);
	}
	return handle;
}

bool Device::memory_type_is_device_optimal(uint32_t type) const
{
	return (mem_props.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;
}

bool Device::memory_type_is_host_visible(uint32_t type) const
{
	return (mem_props.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
}

bool Device::format_is_supported(VkFormat format, VkFormatFeatureFlags required) const
{
	VkFormatProperties props;
	vkGetPhysicalDeviceFormatProperties(gpu, format, &props);
	auto flags = props.optimalTilingFeatures;
	return (flags & required) == required;
}

VkFormat Device::get_default_depth_stencil_format() const
{
	if (format_is_supported(VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
		return VK_FORMAT_D24_UNORM_S8_UINT;
	if (format_is_supported(VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
		return VK_FORMAT_D32_SFLOAT_S8_UINT;

	return VK_FORMAT_UNDEFINED;
}

VkFormat Device::get_default_depth_format() const
{
	if (format_is_supported(VK_FORMAT_D32_SFLOAT, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
		return VK_FORMAT_D32_SFLOAT;
	if (format_is_supported(VK_FORMAT_X8_D24_UNORM_PACK32, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
		return VK_FORMAT_X8_D24_UNORM_PACK32;
	if (format_is_supported(VK_FORMAT_D16_UNORM, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
		return VK_FORMAT_D16_UNORM;

	return VK_FORMAT_UNDEFINED;
}

const RenderPass &Device::request_render_pass(const RenderPassInfo &info)
{
	Hasher h;
	VkFormat formats[VULKAN_NUM_ATTACHMENTS];
	VkFormat depth_stencil;
	uint32_t lazy = 0;
	uint32_t swapchain = 0;

	for (unsigned i = 0; i < info.num_color_attachments; i++)
	{
		formats[i] = info.color_attachments[i] ? info.color_attachments[i]->get_format() : VK_FORMAT_UNDEFINED;
		if (info.color_attachments[i]->get_image().get_create_info().domain == ImageDomain::Transient)
			lazy |= 1u << i;
		if (info.color_attachments[i]->get_image().is_swapchain_image())
			swapchain |= 1u << i;
	}

	if (info.depth_stencil && info.depth_stencil->get_image().get_create_info().domain == ImageDomain::Transient)
		lazy |= 1u << info.num_color_attachments;

	depth_stencil = info.depth_stencil ? info.depth_stencil->get_format() : VK_FORMAT_UNDEFINED;
	h.data(formats, info.num_color_attachments * sizeof(VkFormat));
	h.u32(info.num_color_attachments);
	h.u32(depth_stencil);
	h.u32(info.op_flags);
	h.u32(lazy);
	h.u32(swapchain);

	auto hash = h.get();
	auto itr = render_passes.find(hash);
	if (itr != end(render_passes))
		return *itr->second.get();
	else
	{
		RenderPass *pass = new RenderPass(this, info);
		render_passes.insert(make_pair(hash, unique_ptr<RenderPass>(pass)));
		return *pass;
	}
}

const Framebuffer &Device::request_framebuffer(const RenderPassInfo &info)
{
	return framebuffer_allocator.request_framebuffer(info);
}

ImageView &Device::get_transient_attachment(unsigned width, unsigned height, VkFormat format, unsigned int index)
{
	return transient_allocator.request_attachment(width, height, format, index);
}

RenderPassInfo Device::get_swapchain_render_pass(SwapchainRenderPass style)
{
	RenderPassInfo info;
	info.num_color_attachments = 1;
	info.color_attachments[0] = &frame().backbuffer->get_view();
	info.op_flags = RENDER_PASS_OP_COLOR_OPTIMAL_BIT | RENDER_PASS_OP_CLEAR_ALL_BIT | RENDER_PASS_OP_STORE_COLOR_BIT;

	switch (style)
	{
	case SwapchainRenderPass::Depth:
	{
		info.op_flags |= RENDER_PASS_OP_DEPTH_STENCIL_OPTIMAL_BIT;
		info.depth_stencil =
		    &get_transient_attachment(frame().backbuffer->get_create_info().width,
		                              frame().backbuffer->get_create_info().height, get_default_depth_format());
		break;
	}

	case SwapchainRenderPass::DepthStencil:
	{
		info.op_flags |= RENDER_PASS_OP_DEPTH_STENCIL_OPTIMAL_BIT;
		info.depth_stencil =
		    &get_transient_attachment(frame().backbuffer->get_create_info().width,
		                              frame().backbuffer->get_create_info().height, get_default_depth_stencil_format());
		break;
	}

	default:
		break;
	}
	return info;
}

void Device::wait_for_fence(const Fence &fence)
{
	auto locked_fence = fence.lock();
	if (locked_fence)
		vkWaitForFences(device, 1, &locked_fence->get_fence(), true, UINT64_MAX);
}
}
