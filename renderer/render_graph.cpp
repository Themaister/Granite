#include "render_graph.hpp"

using namespace std;

namespace Granite
{
RenderTextureResource &RenderPass::add_attachment_input(const std::string &name)
{
	auto &res = graph.get_texture_resource(name);
	res.read_in_pass(index);
	attachments_inputs.push_back(&res);
	return res;
}

RenderTextureResource &RenderPass::add_color_input(const std::string &name)
{
	auto &res = graph.get_texture_resource(name);
	res.read_in_pass(index);
	color_inputs.push_back(&res);
	return res;
}

RenderTextureResource &RenderPass::add_texture_input(const std::string &name)
{
	auto &res = graph.get_texture_resource(name);
	res.read_in_pass(index);
	texture_inputs.push_back(&res);
	return res;
}

RenderTextureResource &RenderPass::add_color_output(const std::string &name, const ColorOutputInfo &info)
{
	auto &res = graph.get_texture_resource(name);
	res.written_in_pass(index);
	res.set_color_output_info(info);
	color_outputs.push_back(&res);
	return res;
}

RenderTextureResource& RenderGraph::get_texture_resource(const std::string &name)
{
	auto itr = resource_to_index.find(name);
	if (itr != end(resource_to_index))
	{
		return static_cast<RenderTextureResource &>(*resources[itr->second]);
	}
	else
	{
		resources.emplace_back(new RenderTextureResource);
		return static_cast<RenderTextureResource &>(*resources.back());
	}
}

RenderPass &RenderGraph::add_pass(const std::string &name)
{
	auto itr = pass_to_index.find(name);
	if (itr != end(pass_to_index))
	{
		return *passes[itr->second];
	}
	else
	{
		unsigned index = passes.size();
		passes.emplace_back(new RenderPass(*this, index));
		return *passes.back();
	}
}

void RenderGraph::set_backbuffer_source(const std::string &name)
{
	backbuffer_source = name;
}

void RenderGraph::bake()
{
	auto itr = resource_to_index.find(backbuffer_source);
	if (itr == end(resource_to_index))
		throw runtime_error("Backbuffer source does not exist.");

	pushed_passes.clear();
	pushed_passes_tmp.clear();
	pass_stack.clear();

	auto &backbuffer_resource = *resources[itr->second];
	for (auto &pass : backbuffer_resource.get_write_passes())
	{
		pass_stack.push_back(pass);
		pushed_passes.push_back(pass);
	}

	const auto depend_passes = [&](const std::unordered_set<unsigned> &passes) {
		for (auto &pass : passes)
			pushed_passes_tmp.push_back(pass);
	};

	while (!pushed_passes.empty())
	{
		pushed_passes_tmp.clear();

		for (auto &pushed_pass : pushed_passes)
		{
			auto &pass = *passes[pushed_pass];
			for (auto *input : pass.get_color_inputs())
				depend_passes(input->get_write_passes());
			for (auto *input : pass.get_attachment_inputs())
				depend_passes(input->get_write_passes());
			for (auto *input : pass.get_texture_inputs())
				depend_passes(input->get_write_passes());
		}

		pushed_passes.clear();
		swap(pushed_passes, pushed_passes_tmp);
	}

}

void RenderGraph::reset()
{
	passes.clear();
	resources.clear();
	pass_to_index.clear();
	resource_to_index.clear();
}

}