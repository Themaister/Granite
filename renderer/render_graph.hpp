#pragma once

#include <vector>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include "vulkan.hpp"

namespace Granite
{
class RenderGraph;

enum SizeClass
{
	Absolute,
	SwapchainRelative,
	InputRelative
};

struct ColorOutputInfo
{
	SizeClass size_class = SizeClass::SwapchainRelative;
	float size_x = 1.0f;
	float size_y = 1.0f;
	VkFormat format = VK_FORMAT_UNDEFINED;
	std::string size_relative_name;
};

class RenderResource
{
public:
	enum class Type
	{
		Buffer,
		Texture
	};

	RenderResource(Type type)
		: resource_type(type)
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

private:
	Type resource_type;
	std::unordered_set<unsigned> written_in_passes;
	std::unordered_set<unsigned> read_in_passes;
};

class RenderTextureResource : public RenderResource
{
public:
	RenderTextureResource()
		: RenderResource(RenderResource::Type::Texture)
	{
	}

	void set_color_output_info(const ColorOutputInfo &info)
	{
		this->info = info;
	}

	const ColorOutputInfo &get_color_output_info() const
	{
		return info;
	}

private:
	ColorOutputInfo info;
};

class RenderPass
{
public:
	RenderPass(RenderGraph &graph, unsigned index)
		: graph(graph), index(index)
	{
	}

	unsigned get_index() const
	{
		return index;
	}

	RenderTextureResource &add_color_output(const std::string &name, const ColorOutputInfo &info);
	RenderTextureResource &add_texture_input(const std::string &name);
	RenderTextureResource &add_color_input(const std::string &name);
	RenderTextureResource &add_attachment_input(const std::string &name);

	const std::vector<RenderTextureResource *> &get_color_outputs() const
	{
		return color_outputs;
	}

	const std::vector<RenderTextureResource *> &get_color_inputs() const
	{
		return color_inputs;
	}

	const std::vector<RenderTextureResource *> &get_texture_inputs() const
	{
		return texture_inputs;
	}

	const std::vector<RenderTextureResource *> &get_attachment_inputs() const
	{
		return attachments_inputs;
	}

private:
	RenderGraph &graph;
	unsigned index;

	std::vector<RenderTextureResource *> color_outputs;
	std::vector<RenderTextureResource *> color_inputs;
	std::vector<RenderTextureResource *> texture_inputs;
	std::vector<RenderTextureResource *> attachments_inputs;
};

class RenderGraph
{
public:
	RenderPass &add_pass(const std::string &name);
	void set_backbuffer_source(const std::string &name);
	void bake();
	void reset();

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
};
}