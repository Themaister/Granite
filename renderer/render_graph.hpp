#pragma once

#include <vector>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <string>
#include <functional>
#include "vulkan.hpp"
#include "device.hpp"
#include "stack_allocator.hpp"

namespace Granite
{
class RenderGraph;
class RenderPass;

enum SizeClass
{
	Absolute,
	SwapchainRelative,
	InputRelative
};

struct AttachmentInfo
{
	SizeClass size_class = SizeClass::SwapchainRelative;
	float size_x = 1.0f;
	float size_y = 1.0f;
	VkFormat format = VK_FORMAT_UNDEFINED;
	std::string size_relative_name;
};

struct BufferInfo
{
	VkDeviceSize size;
	VkBufferUsageFlags usage;
};

struct ResourceDimensions
{
	VkFormat format = VK_FORMAT_UNDEFINED;
	unsigned width = 0;
	unsigned height = 0;
	unsigned depth = 1;
	unsigned layers = 1;
	unsigned levels = 1;
	bool transient = false;

	bool operator==(const ResourceDimensions &other) const
	{
		return format == other.format &&
		       width == other.width &&
		       height == other.height &&
		       depth == other.depth &&
		       layers == other.layers &&
		       levels == other.levels &&
		       transient == other.transient;
	}

	bool operator!=(const ResourceDimensions &other) const
	{
		return !(*this == other);
	}
};

class RenderResource
{
public:
	enum class Type
	{
		Buffer,
		Texture
	};

	enum { Unused = ~0u };

	RenderResource(Type type, unsigned index)
		: resource_type(type), index(index)
	{
	}

	virtual ~RenderResource() = default;

	Type get_type() const
	{
		return resource_type;
	}

	void written_in_pass(unsigned index)
	{
		written_in_passes.insert(index);
	}

	void read_in_pass(unsigned index)
	{
		read_in_passes.insert(index);
	}

	const std::unordered_set<unsigned> &get_read_passes() const
	{
		return read_in_passes;
	}

	const std::unordered_set<unsigned> &get_write_passes() const
	{
		return written_in_passes;
	}

	unsigned get_index() const
	{
		return index;
	}

	void set_physical_index(unsigned index)
	{
		physical_index = index;
	}

	unsigned get_physical_index() const
	{
		return physical_index;
	}

private:
	Type resource_type;
	unsigned index;
	unsigned physical_index = Unused;
	std::unordered_set<unsigned> written_in_passes;
	std::unordered_set<unsigned> read_in_passes;
};

class RenderBufferResource : public RenderResource
{
public:
	RenderBufferResource(unsigned index)
		: RenderResource(RenderResource::Type::Buffer, index)
	{
	}

	void set_buffer_info(const BufferInfo &info)
	{
		this->info = info;
	}

	const BufferInfo &get_buffer_info() const
	{
		return info;
	}

private:
	BufferInfo info;
};

class RenderTextureResource : public RenderResource
{
public:
	RenderTextureResource(unsigned index)
		: RenderResource(RenderResource::Type::Texture, index)
	{
	}

	void set_attachment_info(const AttachmentInfo &info)
	{
		this->info = info;
	}

	const AttachmentInfo &get_attachment_info() const
	{
		return info;
	}

	void set_transient_state(bool enable)
	{
		transient = enable;
	}

	bool get_transient_state() const
	{
		return transient;
	}

private:
	AttachmentInfo info;
	bool transient = false;
};

class RenderPassImplementation
{
public:
	virtual bool get_clear_color(unsigned, VkClearColorValue * = nullptr)
	{
		return false;
	}

	virtual bool get_clear_depth_stencil(VkClearDepthStencilValue * = nullptr)
	{
		return false;
	}

	virtual void build_render_pass(RenderPass &pass, Vulkan::CommandBuffer &cmd) = 0;
};

class RenderPassShaderBlitImplementation : public RenderPassImplementation
{
public:
	RenderPassShaderBlitImplementation(std::string vertex, std::string fragment)
		: vertex(std::move(vertex)), fragment(std::move(fragment))
	{
	}

	void set_defines(std::vector<std::pair<std::string, int>> defines)
	{
		this->defines = std::move(defines);
	}

	void set_sampler(Vulkan::StockSampler sampler)
	{
		this->sampler = sampler;
	}

	void build_render_pass(RenderPass &pass, Vulkan::CommandBuffer &cmd) override;

private:
	std::string vertex;
	std::string fragment;
	Vulkan::StockSampler sampler = Vulkan::StockSampler::LinearClamp;
	std::vector<std::pair<std::string, int>> defines;
};

class RenderPass
{
public:
	RenderPass(RenderGraph &graph, unsigned index, VkPipelineStageFlags stages)
		: graph(graph), index(index), stages(stages)
	{
	}

	enum { Unused = ~0u };

	VkPipelineStageFlags get_stages() const
	{
		return stages;
	}

	RenderGraph &get_graph()
	{
		return graph;
	}

	unsigned get_index() const
	{
		return index;
	}

	void set_implementation(RenderPassImplementation *impl)
	{
		implementation = impl;
	}

	RenderPassImplementation &get_implementation()
	{
		assert(implementation);
		return *implementation;
	}

	RenderTextureResource &set_depth_stencil_input(const std::string &name);
	RenderTextureResource &set_depth_stencil_output(const std::string &name, const AttachmentInfo &info);
	RenderTextureResource &add_color_output(const std::string &name, const AttachmentInfo &info);
	RenderTextureResource &add_texture_input(const std::string &name);
	RenderTextureResource &add_color_input(const std::string &name);
	RenderTextureResource &add_attachment_input(const std::string &name);

	void set_texture_inputs(Vulkan::CommandBuffer &cmd, unsigned set, unsigned start_binding,
	                        Vulkan::StockSampler sampler);

	void make_color_input_scaled(unsigned index)
	{
		std::swap(color_scale_inputs[index], color_inputs[index]);
	}

	const std::vector<RenderTextureResource *> &get_color_outputs() const
	{
		return color_outputs;
	}

	const std::vector<RenderTextureResource *> &get_color_inputs() const
	{
		return color_inputs;
	}

	const std::vector<RenderTextureResource *> &get_color_scale_inputs() const
	{
		return color_scale_inputs;
	}

	const std::vector<RenderTextureResource *> &get_texture_inputs() const
	{
		return texture_inputs;
	}

	const std::vector<RenderTextureResource *> &get_attachment_inputs() const
	{
		return attachments_inputs;
	}

	RenderTextureResource *get_depth_stencil_input() const
	{
		return depth_stencil_input;
	}

	RenderTextureResource *get_depth_stencil_output() const
	{
		return depth_stencil_output;
	}

	unsigned get_physical_pass_index() const
	{
		return physical_pass;
	}

	void set_physical_pass_index(unsigned index)
	{
		physical_pass = index;
	}

private:
	RenderGraph &graph;
	unsigned index;
	unsigned physical_pass = Unused;
	VkPipelineStageFlags stages;

	std::vector<RenderTextureResource *> color_outputs;
	std::vector<RenderTextureResource *> color_inputs;
	std::vector<RenderTextureResource *> color_scale_inputs;
	std::vector<RenderTextureResource *> texture_inputs;
	std::vector<RenderTextureResource *> attachments_inputs;
	RenderTextureResource *depth_stencil_input = nullptr;
	RenderTextureResource *depth_stencil_output = nullptr;

	RenderPassImplementation *implementation = nullptr;
};

class RenderGraph : public Vulkan::NoCopyNoMove
{
public:
	RenderPass &add_pass(const std::string &name, VkPipelineStageFlags stages);
	void set_backbuffer_source(const std::string &name);
	void set_backbuffer_dimensions(const ResourceDimensions &dim)
	{
		swapchain_dimensions = dim;
	}

	void bake();
	void reset();
	void log();
	void setup_attachments(Vulkan::Device &device, Vulkan::ImageView *swapchain);
	void enqueue_render_passes(Vulkan::Device &device);

	RenderTextureResource &get_texture_resource(const std::string &name);
	RenderBufferResource &get_buffer_resource(const std::string &name);

	Vulkan::ImageView &get_physical_texture_resource(unsigned index)
	{
		assert(physical_attachments[index]);
		return *physical_attachments[index];
	}

	Vulkan::Buffer &get_physical_buffer_resource(unsigned index)
	{
		assert(physical_buffers[index]);
		return *physical_buffers[index];
	}

private:
	std::vector<std::unique_ptr<RenderPass>> passes;
	std::vector<std::unique_ptr<RenderResource>> resources;
	std::unordered_map<std::string, unsigned> pass_to_index;
	std::unordered_map<std::string, unsigned> resource_to_index;
	std::string backbuffer_source;

	std::vector<unsigned> pass_stack;
	std::vector<unsigned> pushed_passes;
	std::vector<unsigned> pushed_passes_tmp;
	std::unordered_set<unsigned> handled_passes;

	struct Barrier
	{
		unsigned resource_index;
		VkImageLayout layout;
		VkAccessFlags access;
		VkPipelineStageFlags stages;
	};

	struct Barriers
	{
		std::vector<Barrier> invalidate;
		std::vector<Barrier> flush;
	};

	std::vector<Barriers> pass_barriers;

	void filter_passes(std::vector<unsigned> &list);
	void validate_passes();
	void build_barriers();

	ResourceDimensions get_resource_dimensions(const RenderTextureResource &resource) const;
	ResourceDimensions swapchain_dimensions;

	struct ColorClearRequest
	{
		RenderPassImplementation *implementation;
		VkClearColorValue *target;
		unsigned index;
	};

	struct DepthClearRequest
	{
		RenderPassImplementation *implementation;
		VkClearDepthStencilValue *target;
	};

	struct ScaledClearRequests
	{
		unsigned target;
		unsigned physical_resource;
	};

	struct PhysicalPass
	{
		std::vector<unsigned> passes;
		std::vector<Barrier> invalidate;
		std::vector<Barrier> flush;

		Vulkan::RenderPassInfo render_pass_info;
		std::vector<Vulkan::RenderPassInfo::Subpass> subpasses;
		std::vector<unsigned> physical_color_attachments;
		unsigned physical_depth_stencil_attachment = RenderResource::Unused;

		std::vector<ColorClearRequest> color_clear_requests;
		DepthClearRequest depth_clear_request;

		std::vector<std::vector<ScaledClearRequests>> scaled_clear_requests;
	};
	std::vector<PhysicalPass> physical_passes;
	std::vector<Barrier> initial_barriers;
	void build_physical_passes();
	void build_transients();
	void build_physical_resources();
	void build_physical_barriers();
	void build_render_pass_info();

	std::vector<ResourceDimensions> physical_dimensions;
	std::vector<Vulkan::ImageView *> physical_attachments;
	std::vector<Vulkan::BufferHandle> physical_buffers;
	Vulkan::ImageView *swapchain_attachment = nullptr;
	unsigned swapchain_physical_index = RenderResource::Unused;

	void enqueue_scaled_requests(Vulkan::CommandBuffer &cmd, const std::vector<ScaledClearRequests> &requests);
	void enqueue_initial_barriers(Vulkan::CommandBuffer &cmd);
};
}