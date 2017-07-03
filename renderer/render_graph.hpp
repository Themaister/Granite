#pragma once

#include <vector>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <string>
#include "vulkan.hpp"
#include "device.hpp"

namespace Granite
{
class RenderGraph;

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

class RenderPass
{
public:
	RenderPass(RenderGraph &graph, unsigned index)
		: graph(graph), index(index)
	{
	}

	enum { Unused = ~0u };

	unsigned get_index() const
	{
		return index;
	}

	RenderTextureResource &set_depth_stencil_input(const std::string &name);
	RenderTextureResource &set_depth_stencil_output(const std::string &name, const AttachmentInfo &info);
	RenderTextureResource &add_color_output(const std::string &name, const AttachmentInfo &info);
	RenderTextureResource &add_texture_input(const std::string &name);
	RenderTextureResource &add_color_input(const std::string &name);
	RenderTextureResource &add_attachment_input(const std::string &name);

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

	std::vector<RenderTextureResource *> color_outputs;
	std::vector<RenderTextureResource *> color_inputs;
	std::vector<RenderTextureResource *> color_scale_inputs;
	std::vector<RenderTextureResource *> texture_inputs;
	std::vector<RenderTextureResource *> attachments_inputs;
	RenderTextureResource *depth_stencil_input = nullptr;
	RenderTextureResource *depth_stencil_output = nullptr;
};

class RenderGraph
{
public:
	RenderPass &add_pass(const std::string &name);
	void set_backbuffer_source(const std::string &name);
	void set_backbuffer_dimensions(const ResourceDimensions &dim)
	{
		swapchain_dimensions = dim;
	}

	void bake();
	void reset();
	void log();
	void setup_attachments(Vulkan::Device &device, Vulkan::ImageView *swapchain);
	void enqueue_initial_barriers(Vulkan::CommandBuffer &cmd);
	void enqueue_render_passes(Vulkan::CommandBuffer &cmd);

	RenderTextureResource &get_texture_resource(const std::string &name);

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

	struct PhysicalPass
	{
		std::vector<unsigned> passes;
		std::vector<Barrier> invalidate;
		std::vector<Barrier> flush;
	};
	std::vector<PhysicalPass> physical_passes;
	std::vector<Barrier> initial_barriers;
	void build_physical_passes();
	void build_transients();
	void build_physical_resources();
	void build_physical_barriers();

	std::vector<ResourceDimensions> physical_dimensions;
	std::vector<Vulkan::ImageView *> physical_attachments;
	Vulkan::ImageView *swapchain_attachment = nullptr;
	unsigned swapchain_physical_index = RenderResource::Unused;
};
}