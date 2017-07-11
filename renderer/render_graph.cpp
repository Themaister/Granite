#include "render_graph.hpp"
#include "type_to_string.hpp"
#include "format.hpp"
#include <algorithm>
#include "vulkan_events.hpp"

using namespace std;

namespace Granite
{
void RenderPassShaderBlitImplementation::build_render_pass(RenderPass &pass, Vulkan::CommandBuffer &cmd)
{
	pass.set_texture_inputs(cmd, 0, 0, sampler);
	Vulkan::CommandBufferUtil::draw_quad(cmd, vertex, fragment, defines);
}

void RenderPass::set_texture_inputs(Vulkan::CommandBuffer &cmd, unsigned set, unsigned start_binding,
                                     Vulkan::StockSampler sampler)
{
	for (auto &tex : texture_inputs)
	{
		cmd.set_texture(set, start_binding, graph.get_physical_texture_resource(tex->get_physical_index()), sampler);
		start_binding++;
	}
}

RenderTextureResource &RenderPass::add_attachment_input(const std::string &name)
{
	auto &res = graph.get_texture_resource(name);
	res.read_in_pass(index);
	attachments_inputs.push_back(&res);
	return res;
}

RenderTextureResource &RenderPass::add_history_input(const std::string &name)
{
	auto &res = graph.get_texture_resource(name);
	// History inputs are not used in any particular pass, but next frame.
	history_inputs.push_back(&res);
	return res;
}

RenderBufferResource &RenderPass::add_uniform_input(const std::string &name)
{
	auto &res = graph.get_buffer_resource(name);
	res.read_in_pass(index);
	uniform_inputs.push_back(&res);
	return res;
}

RenderBufferResource &RenderPass::add_storage_read_only_input(const std::string &name)
{
	auto &res = graph.get_buffer_resource(name);
	res.read_in_pass(index);
	storage_read_inputs.push_back(&res);
	return res;
}

RenderBufferResource &RenderPass::add_storage_output(const std::string &name, const BufferInfo &info, const std::string &input)
{
	auto &res = graph.get_buffer_resource(name);
	res.set_buffer_info(info);
	res.written_in_pass(index);
	storage_outputs.push_back(&res);

	if (!input.empty())
	{
		auto &input_res = graph.get_buffer_resource(input);
		input_res.read_in_pass(index);
		storage_inputs.push_back(&input_res);
	}
	else
		storage_inputs.push_back(nullptr);

	return res;
}

RenderTextureResource &RenderPass::add_texture_input(const std::string &name)
{
	auto &res = graph.get_texture_resource(name);
	res.read_in_pass(index);
	texture_inputs.push_back(&res);
	return res;
}

RenderTextureResource &RenderPass::add_color_output(const std::string &name, const AttachmentInfo &info, const std::string &input)
{
	auto &res = graph.get_texture_resource(name);
	res.written_in_pass(index);
	res.set_attachment_info(info);
	color_outputs.push_back(&res);

	if (!input.empty())
	{
		auto &input_res = graph.get_texture_resource(input);
		input_res.read_in_pass(index);
		color_inputs.push_back(&input_res);
		color_scale_inputs.push_back(nullptr);
	}
	else
	{
		color_inputs.push_back(nullptr);
		color_scale_inputs.push_back(nullptr);
	}

	return res;
}

RenderTextureResource &RenderPass::add_storage_texture_output(const std::string &name, const AttachmentInfo &info,
                                                              const std::string &input)
{
	auto &res = graph.get_texture_resource(name);
	res.written_in_pass(index);
	res.set_attachment_info(info);
	res.set_storage_state(true);
	storage_texture_outputs.push_back(&res);

	if (!input.empty())
	{
		auto &input_res = graph.get_texture_resource(input);
		input_res.read_in_pass(index);
		storage_texture_inputs.push_back(&input_res);
	}
	else
		storage_texture_inputs.push_back(nullptr);

	return res;
}

RenderTextureResource &RenderPass::set_depth_stencil_output(const std::string &name, const AttachmentInfo &info)
{
	auto &res = graph.get_texture_resource(name);
	res.written_in_pass(index);
	res.set_attachment_info(info);
	depth_stencil_output = &res;
	return res;
}

RenderTextureResource &RenderPass::set_depth_stencil_input(const std::string &name)
{
	auto &res = graph.get_texture_resource(name);
	res.read_in_pass(index);
	depth_stencil_input = &res;
	return res;
}

RenderGraph::RenderGraph()
{
	EventManager::get_global().register_latch_handler(Vulkan::SwapchainParameterEvent::type_id,
                                                      &RenderGraph::on_swapchain_changed,
                                                      &RenderGraph::on_swapchain_destroyed,
                                                      this);
}

void RenderGraph::on_swapchain_destroyed(const Event &)
{
	physical_image_attachments.clear();
	physical_history_image_attachments.clear();
}

void RenderGraph::on_swapchain_changed(const Event &)
{
}

RenderTextureResource &RenderGraph::get_texture_resource(const std::string &name)
{
	auto itr = resource_to_index.find(name);
	if (itr != end(resource_to_index))
	{
		assert(resources[itr->second]->get_type() == RenderResource::Type::Texture);
		return static_cast<RenderTextureResource &>(*resources[itr->second]);
	}
	else
	{
		unsigned index = resources.size();
		resources.emplace_back(new RenderTextureResource(index));
		resource_to_index[name] = index;
		return static_cast<RenderTextureResource &>(*resources.back());
	}
}

RenderBufferResource &RenderGraph::get_buffer_resource(const std::string &name)
{
	auto itr = resource_to_index.find(name);
	if (itr != end(resource_to_index))
	{
		assert(resources[itr->second]->get_type() == RenderResource::Type::Buffer);
		return static_cast<RenderBufferResource &>(*resources[itr->second]);
	}
	else
	{
		unsigned index = resources.size();
		resources.emplace_back(new RenderBufferResource(index));
		resource_to_index[name] = index;
		return static_cast<RenderBufferResource &>(*resources.back());
	}
}

std::vector<Vulkan::BufferHandle> RenderGraph::consume_physical_buffers() const
{
	return physical_buffers;
}

void RenderGraph::install_physical_buffers(std::vector<Vulkan::BufferHandle> buffers)
{
	physical_buffers = move(buffers);
}

Vulkan::BufferHandle RenderGraph::consume_persistent_physical_buffer_resource(unsigned index) const
{
	if (index >= physical_buffers.size())
		return {};
	if (!physical_buffers[index])
		return {};

	return physical_buffers[index];
}

void RenderGraph::install_persistent_physical_buffer_resource(unsigned index, Vulkan::BufferHandle buffer)
{
	if (index >= physical_buffers.size())
		throw logic_error("Out of range.");
	physical_buffers[index] = buffer;
}

RenderPass &RenderGraph::add_pass(const std::string &name, VkPipelineStageFlags stages)
{
	auto itr = pass_to_index.find(name);
	if (itr != end(pass_to_index))
	{
		return *passes[itr->second];
	}
	else
	{
		unsigned index = passes.size();
		passes.emplace_back(new RenderPass(*this, index, stages));
		pass_to_index[name] = index;
		return *passes.back();
	}
}

void RenderGraph::set_backbuffer_source(const std::string &name)
{
	backbuffer_source = name;
}

void RenderGraph::validate_passes()
{
	for (auto &pass_ptr : passes)
	{
		auto &pass = *pass_ptr;

		if (pass.get_color_inputs().size() != pass.get_color_outputs().size())
			throw logic_error("Size of color inputs must match color outputs.");

		if (pass.get_storage_inputs().size() != pass.get_storage_outputs().size())
			throw logic_error("Size of storage inputs must match storage outputs.");

		if (pass.get_storage_texture_inputs().size() != pass.get_storage_texture_outputs().size())
			throw logic_error("Size of storage texture inputs must match storage texture outputs.");

		unsigned num_inputs = pass.get_color_inputs().size();
		for (unsigned i = 0; i < num_inputs; i++)
		{
			if (!pass.get_color_inputs()[i])
				continue;

			if (get_resource_dimensions(*pass.get_color_inputs()[i]) != get_resource_dimensions(*pass.get_color_outputs()[i]))
				pass.make_color_input_scaled(i);
		}

		if (!pass.get_storage_outputs().empty())
		{
			unsigned num_outputs = pass.get_storage_outputs().size();
			for (unsigned i = 0; i < num_outputs; i++)
			{
				if (!pass.get_storage_inputs()[i])
					continue;

				if (pass.get_storage_outputs()[i]->get_buffer_info() != pass.get_storage_inputs()[i]->get_buffer_info())
					throw logic_error("Doing RMW on a storage buffer, but usage and sizes do not match.");
			}
		}

		if (!pass.get_storage_texture_outputs().empty())
		{
			unsigned num_outputs = pass.get_storage_texture_outputs().size();
			for (unsigned i = 0; i < num_outputs; i++)
			{
				if (!pass.get_storage_texture_inputs()[i])
					continue;

				if (get_resource_dimensions(*pass.get_storage_texture_outputs()[i]) != get_resource_dimensions(*pass.get_storage_texture_inputs()[i]))
					throw logic_error("Doing RMW on a storage texture image, but sizes do not match.");
			}
		}

		if (pass.get_depth_stencil_input() && pass.get_depth_stencil_output())
		{
			if (get_resource_dimensions(*pass.get_depth_stencil_input()) != get_resource_dimensions(*pass.get_depth_stencil_output()))
				throw logic_error("Dimension mismatch.");
		}
	}
}

void RenderGraph::build_physical_resources()
{
	unsigned phys_index = 0;

	// Find resources which can alias safely.
	for (auto &pass_index : pass_stack)
	{
		auto &pass = *passes[pass_index];

		for (auto *input : pass.get_attachment_inputs())
		{
			if (input->get_physical_index() == RenderResource::Unused)
			{
				physical_dimensions.push_back(get_resource_dimensions(*input));
				input->set_physical_index(phys_index++);
			}
		}

		for (auto *input : pass.get_texture_inputs())
		{
			if (input->get_physical_index() == RenderResource::Unused)
			{
				physical_dimensions.push_back(get_resource_dimensions(*input));
				input->set_physical_index(phys_index++);
			}
		}

		for (auto *input : pass.get_uniform_inputs())
		{
			if (input->get_physical_index() == RenderResource::Unused)
			{
				physical_dimensions.push_back(get_resource_dimensions(*input));
				input->set_physical_index(phys_index++);
			}
		}

		for (auto *input : pass.get_storage_read_inputs())
		{
			if (input->get_physical_index() == RenderResource::Unused)
			{
				physical_dimensions.push_back(get_resource_dimensions(*input));
				input->set_physical_index(phys_index++);
			}
		}

		for (auto *input : pass.get_color_scale_inputs())
		{
			if (input && input->get_physical_index() == RenderResource::Unused)
			{
				physical_dimensions.push_back(get_resource_dimensions(*input));
				input->set_physical_index(phys_index++);
			}
		}

		if (!pass.get_color_inputs().empty())
		{
			unsigned size = pass.get_color_inputs().size();
			for (unsigned i = 0; i < size; i++)
			{
				auto *input = pass.get_color_inputs()[i];
				if (input)
				{
					if (input->get_physical_index() == RenderResource::Unused)
					{
						physical_dimensions.push_back(get_resource_dimensions(*input));
						input->set_physical_index(phys_index++);
					}

					if (pass.get_color_outputs()[i]->get_physical_index() == RenderResource::Unused)
						pass.get_color_outputs()[i]->set_physical_index(input->get_physical_index());
					else if (pass.get_color_outputs()[i]->get_physical_index() != input->get_physical_index())
						throw logic_error("Cannot alias resources. Index already claimed.");
				}
			}
		}

		if (!pass.get_storage_inputs().empty())
		{
			unsigned size = pass.get_storage_inputs().size();
			for (unsigned i = 0; i < size; i++)
			{
				auto *input = pass.get_storage_inputs()[i];
				if (input)
				{
					if (input->get_physical_index() == RenderResource::Unused)
					{
						physical_dimensions.push_back(get_resource_dimensions(*input));
						input->set_physical_index(phys_index++);
					}

					if (pass.get_storage_outputs()[i]->get_physical_index() == RenderResource::Unused)
						pass.get_storage_outputs()[i]->set_physical_index(input->get_physical_index());
					else if (pass.get_storage_outputs()[i]->get_physical_index() != input->get_physical_index())
						throw logic_error("Cannot alias resources. Index already claimed.");
				}
			}
		}

		if (!pass.get_storage_texture_inputs().empty())
		{
			unsigned size = pass.get_storage_texture_inputs().size();
			for (unsigned i = 0; i < size; i++)
			{
				auto *input = pass.get_storage_texture_inputs()[i];
				if (input)
				{
					if (input->get_physical_index() == RenderResource::Unused)
					{
						physical_dimensions.push_back(get_resource_dimensions(*input));
						input->set_physical_index(phys_index++);
					}

					if (pass.get_storage_texture_outputs()[i]->get_physical_index() == RenderResource::Unused)
						pass.get_storage_texture_outputs()[i]->set_physical_index(input->get_physical_index());
					else if (pass.get_storage_texture_outputs()[i]->get_physical_index() != input->get_physical_index())
						throw logic_error("Cannot alias resources. Index already claimed.");
				}
			}
		}

		for (auto *output : pass.get_color_outputs())
		{
			if (output->get_physical_index() == RenderResource::Unused)
			{
				physical_dimensions.push_back(get_resource_dimensions(*output));
				output->set_physical_index(phys_index++);
			}
		}

		for (auto *output : pass.get_storage_outputs())
		{
			if (output->get_physical_index() == RenderResource::Unused)
			{
				physical_dimensions.push_back(get_resource_dimensions(*output));
				output->set_physical_index(phys_index++);
			}
		}

		for (auto *output : pass.get_storage_texture_outputs())
		{
			if (output->get_physical_index() == RenderResource::Unused)
			{
				physical_dimensions.push_back(get_resource_dimensions(*output));
				output->set_physical_index(phys_index++);
			}
		}

		auto *ds_output = pass.get_depth_stencil_output();
		auto *ds_input = pass.get_depth_stencil_input();
		if (ds_input)
		{
			if (ds_input->get_physical_index() == RenderResource::Unused)
			{
				physical_dimensions.push_back(get_resource_dimensions(*ds_input));
				ds_input->set_physical_index(phys_index++);
			}

			if (ds_output)
			{
				if (ds_output->get_physical_index() == RenderResource::Unused)
					ds_output->set_physical_index(ds_input->get_physical_index());
				else if (ds_output->get_physical_index() != ds_input->get_physical_index())
					throw logic_error("Cannot alias resources. Index already claimed.");
			}
		}
		else if (ds_output)
		{
			if (ds_output->get_physical_index() == RenderResource::Unused)
			{
				physical_dimensions.push_back(get_resource_dimensions(*ds_output));
				ds_output->set_physical_index(phys_index++);
			}
		}
	}

	// Figure out which physical resources need to have history.
	physical_image_has_history.clear();
	physical_image_has_history.resize(physical_dimensions.size());

	for (auto &pass_index : pass_stack)
	{
		auto &pass = *passes[pass_index];
		for (auto &history : pass.get_history_inputs())
		{
			unsigned phys_index = history->get_physical_index();
			if (phys_index == RenderResource::Unused)
				throw logic_error("History input is used, but it was never written to.");
			physical_image_has_history[phys_index] = true;
		}
	}
}

void RenderGraph::build_transients()
{
	vector<unsigned> physical_pass_used(physical_dimensions.size());
	for (auto &u : physical_pass_used)
		u = RenderPass::Unused;

	for (auto &dim : physical_dimensions)
	{
		// Buffers are never transient.
		if (dim.buffer_info.size)
			dim.transient = false;
		else
			dim.transient = true;

		unsigned index = unsigned(&dim - physical_dimensions.data());
		if (physical_image_has_history[index])
			dim.transient = false;
	}

	for (auto &resource : resources)
	{
		if (resource->get_type() != RenderResource::Type::Texture)
			continue;

		unsigned physical_index = resource->get_physical_index();
		if (physical_index == RenderResource::Unused)
			continue;

		for (auto &pass : resource->get_write_passes())
		{
			unsigned phys = passes[pass]->get_physical_pass_index();
			if (phys != RenderPass::Unused)
			{
				if (physical_pass_used[physical_index] != RenderPass::Unused &&
				    phys != physical_pass_used[physical_index])
				{
					physical_dimensions[physical_index].transient = false;
					break;
				}
				physical_pass_used[physical_index] = phys;
			}
		}

		for (auto &pass : resource->get_read_passes())
		{
			unsigned phys = passes[pass]->get_physical_pass_index();
			if (phys != RenderPass::Unused)
			{
				if (physical_pass_used[physical_index] != RenderPass::Unused &&
				    phys != physical_pass_used[physical_index])
				{
					physical_dimensions[physical_index].transient = false;
					break;
				}
				physical_pass_used[physical_index] = phys;
			}
		}
	}
}

void RenderGraph::build_render_pass_info()
{
	for (auto &physical_pass : physical_passes)
	{
		auto &rp = physical_pass.render_pass_info;
		physical_pass.subpasses.resize(physical_pass.passes.size());
		rp.subpasses = physical_pass.subpasses.data();
		rp.num_subpasses = physical_pass.subpasses.size();
		rp.clear_attachments = 0;
		rp.load_attachments = 0;
		rp.store_attachments = ~0u;
		rp.op_flags = Vulkan::RENDER_PASS_OP_COLOR_OPTIMAL_BIT;
		physical_pass.color_clear_requests.clear();
		physical_pass.depth_clear_request = {};

		auto &colors = physical_pass.physical_color_attachments;
		colors.clear();

		const auto add_unique_color = [&](unsigned index) -> pair<unsigned, bool> {
			auto itr = find(begin(colors), end(colors), index);
			if (itr != end(colors))
				return make_pair(unsigned(itr - begin(colors)), false);
			else
			{
				unsigned ret = colors.size();
				colors.push_back(index);
				return make_pair(ret, true);
			}
		};

		const auto add_unique_input_attachment = [&](unsigned index) -> pair<unsigned, bool> {
			if (index == physical_pass.physical_depth_stencil_attachment)
				return make_pair(unsigned(colors.size()), false); // The N + 1 attachment refers to depth.
			else
				return add_unique_color(index);
		};

		for (auto &subpass : physical_pass.passes)
		{
			vector<ScaledClearRequests> scaled_clear_requests;

			auto &pass = *passes[subpass];
			unsigned subpass_index = unsigned(&subpass - physical_pass.passes.data());

			// Add color attachments.
			unsigned num_color_attachments = pass.get_color_outputs().size();
			physical_pass.subpasses[subpass_index].num_color_attachments = num_color_attachments;
			for (unsigned i = 0; i < num_color_attachments; i++)
			{
				auto res = add_unique_color(pass.get_color_outputs()[i]->get_physical_index());
				physical_pass.subpasses[subpass_index].color_attachments[i] = res.first;

				if (res.second) // This is the first time the color attachment is used, check if we need LOAD, or if we can clear it.
				{
					bool has_color_input = !pass.get_color_inputs().empty() && pass.get_color_inputs()[i];
					bool has_scaled_color_input = !pass.get_color_scale_inputs().empty() && pass.get_color_scale_inputs()[i];

					if (!has_color_input && !has_scaled_color_input)
					{
						if (pass.get_implementation().get_clear_color(i))
						{
							rp.clear_attachments |= 1u << res.first;
							physical_pass.color_clear_requests.push_back({ &pass.get_implementation(), &rp.clear_color[res.first], i });
						}
					}
					else
					{
						if (has_scaled_color_input)
							scaled_clear_requests.push_back({ i, pass.get_color_scale_inputs()[i]->get_physical_index() });
						else
							rp.load_attachments |= 1u << res.first;
					}
				}
			}

			physical_pass.scaled_clear_requests.push_back(move(scaled_clear_requests));

			auto *ds_input = pass.get_depth_stencil_input();
			auto *ds_output = pass.get_depth_stencil_output();

			const auto add_unique_ds = [&](unsigned index) -> pair<unsigned, bool> {
				assert(physical_pass.physical_depth_stencil_attachment == RenderResource::Unused ||
				       physical_pass.physical_depth_stencil_attachment == index);

				bool new_attachment = physical_pass.physical_depth_stencil_attachment == RenderResource::Unused;
				physical_pass.physical_depth_stencil_attachment = index;
				return make_pair(index, new_attachment);
			};

			if (ds_output && ds_input)
			{
				auto res = add_unique_ds(ds_output->get_physical_index());
				// If this is the first subpass the attachment is used, we need to load it.
				if (res.second)
					rp.load_attachments |= 1u << res.first;

				rp.op_flags |= Vulkan::RENDER_PASS_OP_DEPTH_STENCIL_OPTIMAL_BIT | Vulkan::RENDER_PASS_OP_STORE_DEPTH_STENCIL_BIT;
				physical_pass.subpasses[subpass_index].depth_stencil_mode = Vulkan::RenderPassInfo::DepthStencil::ReadWrite;

			}
			else if (ds_output)
			{
				auto res = add_unique_ds(ds_output->get_physical_index());
				// If this is the first subpass the attachment is used, we need to either clear or discard.
				if (res.second && pass.get_implementation().get_clear_depth_stencil())
				{
					rp.op_flags |= Vulkan::RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT;
					physical_pass.depth_clear_request.implementation = &pass.get_implementation();
					physical_pass.depth_clear_request.target = &rp.clear_depth_stencil;
				}

				rp.op_flags |= Vulkan::RENDER_PASS_OP_DEPTH_STENCIL_OPTIMAL_BIT | Vulkan::RENDER_PASS_OP_STORE_DEPTH_STENCIL_BIT;
				physical_pass.subpasses[subpass_index].depth_stencil_mode = Vulkan::RenderPassInfo::DepthStencil::ReadWrite;

				assert(physical_pass.physical_depth_stencil_attachment == RenderResource::Unused ||
				       physical_pass.physical_depth_stencil_attachment == ds_output->get_physical_index());
				physical_pass.physical_depth_stencil_attachment = ds_output->get_physical_index();
			}
			else if (ds_input)
			{
				auto res = add_unique_ds(ds_input->get_physical_index());

				// If this is the first subpass the attachment is used, we need to load.
				if (res.second)
				{
					rp.op_flags |= Vulkan::RENDER_PASS_OP_DEPTH_STENCIL_READ_ONLY_BIT |
					               Vulkan::RENDER_PASS_OP_LOAD_DEPTH_STENCIL_BIT;

					bool preserve_depth = false;
					for (auto &read_pass : ds_input->get_read_passes())
					{
						if (passes[read_pass]->get_physical_pass_index() > unsigned(&physical_pass - physical_passes.data()))
						{
							preserve_depth = true;
							break;
						}
					}

					if (preserve_depth)
					{
						// Have to store here, or the attachment becomes undefined in future passes.
						rp.op_flags |= Vulkan::RENDER_PASS_OP_STORE_DEPTH_STENCIL_BIT;
					}
				}

				physical_pass.subpasses[subpass_index].depth_stencil_mode = Vulkan::RenderPassInfo::DepthStencil::ReadOnly;
			}
			else
			{
				physical_pass.subpasses[subpass_index].depth_stencil_mode = Vulkan::RenderPassInfo::DepthStencil::None;
			}
		}

		for (auto &subpass : physical_pass.passes)
		{
			auto &pass = *passes[subpass];
			unsigned subpass_index = unsigned(&subpass - physical_pass.passes.data());

			// Add input attachments.
			// Have to do these in a separate loop so we can pick up depth stencil input attachments properly.
			unsigned num_input_attachments = pass.get_attachment_inputs().size();
			physical_pass.subpasses[subpass_index].num_input_attachments = num_input_attachments;
			for (unsigned i = 0; i < num_input_attachments; i++)
			{
				auto res = add_unique_input_attachment(pass.get_attachment_inputs()[i]->get_physical_index());
				physical_pass.subpasses[subpass_index].input_attachments[i] = res.first;

				// If this is the first subpass the attachment is used, we need to load it.
				if (res.second)
					rp.load_attachments |= 1u << res.first;
			}
		}

		physical_pass.render_pass_info.num_color_attachments = physical_pass.physical_color_attachments.size();
	}
}

void RenderGraph::build_physical_passes()
{
	physical_passes.clear();
	PhysicalPass physical_pass;

	const auto find_attachment = [](const vector<RenderTextureResource *> &resources, const RenderTextureResource *resource) -> bool {
		auto itr = find(begin(resources), end(resources), resource);
		return itr != end(resources);
	};

	const auto find_buffer = [](const vector<RenderBufferResource *> &resources, const RenderBufferResource *resource) -> bool {
		auto itr = find(begin(resources), end(resources), resource);
		return itr != end(resources);
	};

	const auto should_merge = [&](const RenderPass &prev, const RenderPass &next) -> bool {
		// Can only merge graphics.
		if (prev.get_stages() != VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT || next.get_stages() != VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT)
			return false;

		// Need non-local dependency, cannot merge.
		for (auto *input : next.get_texture_inputs())
		{
			if (find_attachment(prev.get_color_outputs(), input))
				return false;
			if (find_attachment(prev.get_storage_texture_outputs(), input))
				return false;
			if (input && prev.get_depth_stencil_output() == input)
				return false;
		}

		// Need non-local dependency, cannot merge.
		for (auto *input : next.get_uniform_inputs())
			if (find_buffer(prev.get_storage_outputs(), input))
				return false;

		// Need non-local dependency, cannot merge.
		for (auto *input : next.get_storage_read_inputs())
			if (find_buffer(prev.get_storage_outputs(), input))
				return false;

		// Need non-local dependency, cannot merge.
		for (auto *input : next.get_storage_inputs())
			if (find_buffer(prev.get_storage_outputs(), input))
				return false;

		// Need non-local dependency, cannot merge.
		for (auto *input : next.get_storage_texture_inputs())
			if (find_attachment(prev.get_storage_texture_outputs(), input))
				return false;

		// Need non-local dependency, cannot merge.
		for (auto *input : next.get_color_scale_inputs())
		{
			if (find_attachment(prev.get_storage_texture_outputs(), input))
				return false;
			if (find_attachment(prev.get_color_outputs(), input))
				return false;
		}

		// Keep color on tile.
		for (auto *input : next.get_color_inputs())
		{
			if (!input)
				continue;
			if (find_attachment(prev.get_storage_texture_outputs(), input))
				return false;
			if (find_attachment(prev.get_color_outputs(), input))
				return true;
		}

		const auto different_attachment = [](const RenderResource *a, const RenderResource *b) {
			return a && b && a->get_physical_index() != b->get_physical_index();
		};

		// Need a different depth attachment, break up the pass.
		if (different_attachment(next.get_depth_stencil_input(), prev.get_depth_stencil_input()))
			return false;
		if (different_attachment(next.get_depth_stencil_output(), prev.get_depth_stencil_input()))
			return false;
		if (different_attachment(next.get_depth_stencil_input(), prev.get_depth_stencil_output()))
			return false;
		if (different_attachment(next.get_depth_stencil_output(), prev.get_depth_stencil_output()))
			return false;

		// Keep depth on tile.
		if (next.get_depth_stencil_input() && next.get_depth_stencil_input() == prev.get_depth_stencil_output())
			return true;

		// Keep depth attachment or color on-tile.
		for (auto *input : next.get_attachment_inputs())
		{
			if (find_attachment(prev.get_color_outputs(), input))
				return true;
			if (input && prev.get_depth_stencil_output() == input)
				return true;
		}

		return true;
	};

	for (unsigned index = 0; index < pass_stack.size(); )
	{
		unsigned merge_end = index + 1;
		for (; merge_end < pass_stack.size(); merge_end++)
		{
			bool merge = true;
			for (unsigned merge_start = index; merge_start < merge_end; merge_start++)
			{
				if (!should_merge(*passes[pass_stack[merge_start]], *passes[pass_stack[merge_end]]))
				{
					merge = false;
					break;
				}
			}

			if (!merge)
				break;
		}

		physical_pass.passes.insert(end(physical_pass.passes), begin(pass_stack) + index, begin(pass_stack) + merge_end);
		physical_passes.push_back(move(physical_pass));
		index = merge_end;
	}

	for (auto &physical_pass : physical_passes)
	{
		unsigned index = &physical_pass - physical_passes.data();
		for (auto &pass : physical_pass.passes)
			passes[pass]->set_physical_pass_index(index);
	}
}

void RenderGraph::log()
{
	for (auto &resource : physical_dimensions)
	{
		if (resource.buffer_info.size)
		{
			LOGI("Resource #%u: size: %u\n", unsigned(&resource - physical_dimensions.data()), unsigned(resource.buffer_info.size));
		}
		else
		{
			LOGI("Resource #%u: %u x %u (fmt: %u), transient: %s%s\n", unsigned(&resource - physical_dimensions.data()),
			     resource.width, resource.height, unsigned(resource.format), resource.transient ? "yes" : "no",
			     unsigned(&resource - physical_dimensions.data()) == swapchain_physical_index ? " (swapchain)" : "");
		}
	}

	auto barrier_itr = begin(pass_barriers);

	const auto swap_str = [this](const Barrier &barrier) -> const char * {
		return barrier.resource_index == swapchain_physical_index ?
	           " (swapchain)" : "";
	};

	for (auto &barrier : initial_top_of_pipe_barriers)
	{
		LOGI("ImmediateDiscardBarrier: %u%s, layout: %s, access: %s, stages: %s\n",
		     barrier.resource_index,
		     swap_str(barrier),
		     Vulkan::layout_to_string(barrier.layout),
		     Vulkan::access_flags_to_string(barrier.access).c_str(),
		     Vulkan::stage_flags_to_string(barrier.stages).c_str());
	}

	for (auto &barrier : initial_barriers)
	{
		LOGI("DiscardBarrier: %u%s, layout: %s, access: %s, stages: %s\n",
		     barrier.resource_index,
		     swap_str(barrier),
		     Vulkan::layout_to_string(barrier.layout),
		     Vulkan::access_flags_to_string(barrier.access).c_str(),
		     Vulkan::stage_flags_to_string(barrier.stages).c_str());
	}

	for (auto &passes : physical_passes)
	{
		LOGI("Physical pass #%u:\n", unsigned(&passes - physical_passes.data()));

		for (auto &barrier : passes.invalidate)
		{
			LOGI("  Invalidate: %u%s, layout: %s, access: %s, stages: %s\n",
			     barrier.resource_index,
			     swap_str(barrier),
			     Vulkan::layout_to_string(barrier.layout),
			     Vulkan::access_flags_to_string(barrier.access).c_str(),
			     Vulkan::stage_flags_to_string(barrier.stages).c_str());
		}

		for (auto &subpass : passes.passes)
		{
			LOGI("    Subpass #%u:\n", unsigned(&subpass - passes.passes.data()));
			auto &pass = *this->passes[subpass];

			auto &barriers = *barrier_itr;
			for (auto &barrier : barriers.invalidate)
			{
				if (!physical_dimensions[barrier.resource_index].transient)
				{
					LOGI("      Invalidate: %u%s, layout: %s, access: %s, stages: %s\n",
					     barrier.resource_index,
					     swap_str(barrier),
					     Vulkan::layout_to_string(barrier.layout),
					     Vulkan::access_flags_to_string(barrier.access).c_str(),
					     Vulkan::stage_flags_to_string(barrier.stages).c_str());
				}
			}

			if (pass.get_depth_stencil_output())
				LOGI("        DepthStencil RW: %u\n", pass.get_depth_stencil_output()->get_physical_index());
			else if (pass.get_depth_stencil_input())
				LOGI("        DepthStencil ReadOnly: %u\n", pass.get_depth_stencil_input()->get_physical_index());

			for (auto &output : pass.get_color_outputs())
				LOGI("        ColorAttachment #%u: %u\n", unsigned(&output - pass.get_color_outputs().data()), output->get_physical_index());
			for (auto &input : pass.get_attachment_inputs())
				LOGI("        InputAttachment #%u: %u\n", unsigned(&input - pass.get_attachment_inputs().data()), input->get_physical_index());
			for (auto &input : pass.get_texture_inputs())
				LOGI("        Texture #%u: %u\n", unsigned(&input - pass.get_texture_inputs().data()), input->get_physical_index());

			for (auto &input : pass.get_color_scale_inputs())
			{
				if (input)
				{
					LOGI("        ColorScaleInput #%u: %u\n",
					     unsigned(&input - pass.get_color_scale_inputs().data()),
					     input->get_physical_index());
				}
			}

			for (auto &barrier : barriers.flush)
			{
				if (!physical_dimensions[barrier.resource_index].transient &&
					barrier.resource_index != swapchain_physical_index)
				{
					LOGI("      Flush: %u, layout: %s, access: %s, stages: %s\n",
					     barrier.resource_index, Vulkan::layout_to_string(barrier.layout),
					     Vulkan::access_flags_to_string(barrier.access).c_str(),
					     Vulkan::stage_flags_to_string(barrier.stages).c_str());
				}
			}

			++barrier_itr;
		}

		for (auto &barrier : passes.flush)
		{
			LOGI("  Flush: %u%s, layout: %s, access: %s, stages: %s\n",
			     barrier.resource_index,
			     swap_str(barrier),
			     Vulkan::layout_to_string(barrier.layout),
			     Vulkan::access_flags_to_string(barrier.access).c_str(),
			     Vulkan::stage_flags_to_string(barrier.stages).c_str());
		}
	}
}

void RenderGraph::enqueue_scaled_requests(Vulkan::CommandBuffer &cmd, const std::vector<ScaledClearRequests> &requests)
{
	if (requests.empty())
		return;

	vector<pair<string, int>> defines;
	defines.reserve(requests.size());

	for (auto &req : requests)
	{
		defines.push_back({string("HAVE_TARGET_") + to_string(req.target), 1});
		cmd.set_texture(0, req.target, *physical_attachments[req.physical_resource], Vulkan::StockSampler::LinearClamp);
	}

	Vulkan::CommandBufferUtil::draw_quad(cmd, "assets://shaders/quad.vert", "assets://shaders/scaled_readback.frag", defines);
}

void RenderGraph::enqueue_render_passes(Vulkan::Device &device)
{
	{
		auto cmd = device.request_command_buffer();
		enqueue_initial_barriers(*cmd);
		device.submit(cmd);
	}

	struct BarrierData
	{
		VkPipelineStageFlags src_stages = 0;
		VkAccessFlags src_access = 0;
		VkImageLayout current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		Vulkan::PipelineEvent event;
	};
	vector<BarrierData> resources(physical_dimensions.size());
	vector<VkBufferMemoryBarrier> buffer_barriers;
	vector<VkImageMemoryBarrier> image_barriers;
	vector<VkEvent> events;

	for (auto &physical_pass : physical_passes)
	{
		auto cmd = device.request_command_buffer();

		VkPipelineStageFlags dst_stages = 0;
		VkPipelineStageFlags src_stages = 0;
		buffer_barriers.clear();
		image_barriers.clear();
		events.clear();

		const auto add_unique_event = [&](VkEvent event) {
			assert(event != VK_NULL_HANDLE);
			auto itr = find(begin(events), end(events), event);
			if (itr == end(events))
				events.push_back(event);
		};

		// Queue up invalidates and change layouts.
		for (auto &barrier : physical_pass.invalidate)
		{
			auto &resource = resources[barrier.resource_index];

			if (physical_dimensions[barrier.resource_index].buffer_info.size)
			{
				auto &buffer = *physical_buffers[barrier.resource_index];
				VkBufferMemoryBarrier b = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
				b.srcAccessMask = resource.src_access;
				b.dstAccessMask = barrier.access;
				b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				b.buffer = buffer.get_buffer();
				b.offset = 0;
				b.size = VK_WHOLE_SIZE;

				buffer_barriers.push_back(b);
			}
			else
			{
				auto &view = *physical_attachments[barrier.resource_index];
				VkImageMemoryBarrier b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
				b.oldLayout = resource.current_layout;
				b.newLayout = barrier.layout;
				b.srcAccessMask = resource.src_access;
				b.dstAccessMask = barrier.access;
				b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				b.image = physical_attachments[barrier.resource_index]->get_image().get_image();
				b.subresourceRange.aspectMask = Vulkan::format_to_aspect_mask(view.get_image().get_format());
				b.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
				b.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
				physical_attachments[barrier.resource_index]->get_image().set_layout(barrier.layout);

				image_barriers.push_back(b);
			}

			resource.current_layout = barrier.layout;
			resource.src_access = 0;
			src_stages |= resource.src_stages;
			dst_stages |= barrier.stages;
			resource.src_stages = 0;

			add_unique_event(resource.event->get_event());
		}

		if (!image_barriers.empty() || !buffer_barriers.empty())
		{
			cmd->wait_events(events.size(), events.data(),
			                 src_stages, dst_stages,
			                 0, nullptr,
			                 buffer_barriers.size(), buffer_barriers.empty() ? nullptr : buffer_barriers.data(),
			                 image_barriers.size(), image_barriers.empty() ? nullptr : image_barriers.data());
		}

		bool graphics = (passes[physical_pass.passes.front()]->get_stages() & VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT) != 0;

		if (graphics)
		{
			for (auto &clear_req : physical_pass.color_clear_requests)
				clear_req.implementation->get_clear_color(clear_req.index, clear_req.target);
			if (physical_pass.depth_clear_request.implementation)
				physical_pass.depth_clear_request.implementation->get_clear_depth_stencil(
					physical_pass.depth_clear_request.target);

			cmd->begin_render_pass(physical_pass.render_pass_info);

			for (auto &subpass : physical_pass.passes)
			{
				unsigned subpass_index = unsigned(&subpass - physical_pass.passes.data());
				auto &scaled_requests = physical_pass.scaled_clear_requests[subpass_index];
				enqueue_scaled_requests(*cmd, scaled_requests);

				auto &pass = *passes[subpass];
				pass.get_implementation().build_render_pass(pass, *cmd);
				if (&subpass != &physical_pass.passes.back())
					cmd->next_subpass();
			}

			cmd->end_render_pass();
		}
		else
		{
			assert(physical_pass.passes.size() == 1);
			auto &pass = *passes[physical_pass.passes.front()];
			pass.get_implementation().build_render_pass(pass, *cmd);
		}

		VkPipelineStageFlags wait_stages = 0;
		for (auto &barrier : physical_pass.flush)
			wait_stages |= barrier.stages;

		Vulkan::PipelineEvent event;
		if (wait_stages != 0)
			event = cmd->signal_event(wait_stages);

		for (auto &barrier : physical_pass.flush)
		{
			auto &resource = resources[barrier.resource_index];

			if (!physical_dimensions[barrier.resource_index].buffer_info.size)
				physical_attachments[barrier.resource_index]->get_image().set_layout(barrier.layout);

			resource.current_layout = barrier.layout;
			resource.src_stages = wait_stages;
			resource.src_access |= barrier.access;
			resource.event = event;
		}

		device.submit(cmd);
	}

	// Scale to swapchain.
	if (swapchain_physical_index == RenderResource::Unused)
	{
		auto cmd = device.request_command_buffer();
		unsigned index = this->resources[resource_to_index[backbuffer_source]]->get_physical_index();
		cmd->image_barrier(physical_attachments[index]->get_image(),
		                   resources[index].current_layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		                   resources[index].src_stages, resources[index].src_access,
		                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
		physical_attachments[index]->get_image().set_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		auto rp_info = device.get_swapchain_render_pass(Vulkan::SwapchainRenderPass::ColorOnly);
		rp_info.clear_attachments = 0;
		cmd->begin_render_pass(rp_info);
		enqueue_scaled_requests(*cmd, {{ 0, index }});
		cmd->end_render_pass();
		device.submit(cmd);
	}
}

void RenderGraph::setup_physical_buffer(Vulkan::Device &device, unsigned attachment)
{
	auto &att = physical_dimensions[attachment];

	Vulkan::BufferCreateInfo info = {};
	info.size = att.buffer_info.size;
	info.usage = att.buffer_info.usage;
	info.domain = Vulkan::BufferDomain::Device;

	bool need_buffer = true;
	if (physical_buffers[attachment])
	{
		if (att.persistent &&
		    physical_buffers[attachment]->get_create_info().size == info.size &&
		    (physical_buffers[attachment]->get_create_info().usage & info.usage) == info.usage)
		{
			need_buffer = false;
		}
	}

	if (need_buffer)
	{
		// Zero-initialize buffers. TODO: Make this configurable.
		vector<uint8_t> blank(info.size);
		physical_buffers[attachment] = device.create_buffer(info, blank.data());
	}
}

void RenderGraph::setup_physical_image(Vulkan::Device &device, unsigned attachment, bool storage)
{
	auto &att = physical_dimensions[attachment];

	bool need_image = true;
	VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
	VkImageCreateFlags flags = 0;

	if (storage)
	{
		usage |= VK_IMAGE_USAGE_STORAGE_BIT;
		flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
	}

	if (Vulkan::format_is_stencil(att.format) ||
	    Vulkan::format_is_depth_stencil(att.format) ||
	    Vulkan::format_is_depth(att.format))
	{
		usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	}
	else
	{
		usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	}

	if (physical_image_attachments[attachment])
	{
		if (att.persistent &&
		    physical_image_attachments[attachment]->get_create_info().format == att.format &&
		    physical_image_attachments[attachment]->get_create_info().width == att.width &&
		    physical_image_attachments[attachment]->get_create_info().height == att.height &&
		    (physical_image_attachments[attachment]->get_create_info().usage & usage) == usage &&
			(physical_image_attachments[attachment]->get_create_info().flags & flags) == flags)
		{
			need_image = false;
		}
	}

	if (need_image)
	{
		Vulkan::ImageCreateInfo info;
		info.format = att.format;
		info.width = att.width;
		info.height = att.height;
		info.domain = Vulkan::ImageDomain::Physical;
		info.levels = 1;
		info.layers = 1;
		info.usage = usage;
		info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		info.samples = VK_SAMPLE_COUNT_1_BIT;
		info.flags = flags;
		physical_image_attachments[attachment] = device.create_image(info, nullptr);
	}

	physical_attachments[attachment] = &physical_image_attachments[attachment]->get_view();
}

void RenderGraph::setup_attachments(Vulkan::Device &device, Vulkan::ImageView *swapchain)
{
	physical_attachments.clear();
	physical_attachments.resize(physical_dimensions.size());

	// Try to reuse the buffers if possible.
	physical_buffers.resize(physical_dimensions.size());

	// Try to reuse render targets if possible.
	physical_image_attachments.resize(physical_dimensions.size());
	physical_history_image_attachments.resize(physical_dimensions.size());

	swapchain_attachment = swapchain;

	unsigned num_attachments = physical_dimensions.size();
	for (unsigned i = 0; i < num_attachments; i++)
	{
		// Move over history attachments.
		if (physical_image_has_history[i])
			swap(physical_history_image_attachments[i], physical_image_attachments[i]);

		auto &att = physical_dimensions[i];
		if (att.buffer_info.size != 0)
		{
			setup_physical_buffer(device, i);
		}
		else
		{
			if (att.storage)
				setup_physical_image(device, i, true);
			else if (i == swapchain_physical_index)
				physical_attachments[i] = swapchain;
			else if (att.transient)
				physical_attachments[i] = &device.get_transient_attachment(att.width, att.height, att.format, i, 1);
			else
				setup_physical_image(device, i, false);
		}
	}

	// Assign concrete ImageViews to the render pass.
	for (auto &physical_pass : physical_passes)
	{
		unsigned num_attachments = physical_pass.physical_color_attachments.size();
		for (unsigned i = 0; i < num_attachments; i++)
			physical_pass.render_pass_info.color_attachments[i] = physical_attachments[physical_pass.physical_color_attachments[i]];

		if (physical_pass.physical_depth_stencil_attachment != RenderResource::Unused)
			physical_pass.render_pass_info.depth_stencil = physical_attachments[physical_pass.physical_depth_stencil_attachment];
		else
			physical_pass.render_pass_info.depth_stencil = nullptr;
	}
}

void RenderGraph::enqueue_initial_barriers(Vulkan::CommandBuffer &cmd)
{
	enqueue_initial_barriers(cmd, initial_barriers, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
	enqueue_initial_barriers(cmd, initial_top_of_pipe_barriers, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
}

void RenderGraph::enqueue_initial_barriers(Vulkan::CommandBuffer &cmd, const vector<Barrier> &barriers, VkPipelineStageFlags src_stages)
{
	unsigned num_barriers = barriers.size();
	if (!num_barriers)
		return;

	vector<VkBufferMemoryBarrier> buffer_barriers;
	vector<VkImageMemoryBarrier> image_barriers;
	buffer_barriers.reserve(barriers.size());
	image_barriers.reserve(barriers.size());

	// This serializes frames, which might not be the ideal, but we can overlap TRANSFERS and some compute at least ...
	VkPipelineStageFlags dst_stages = 0;

	for (unsigned i = 0; i < num_barriers; i++)
	{
		if (physical_dimensions[barriers[i].resource_index].buffer_info.size)
		{
			auto &buffer = *physical_buffers[barriers[i].resource_index];
			VkBufferMemoryBarrier b = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
			b.srcAccessMask = 0;
			b.dstAccessMask = barriers[i].access;
			b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			b.buffer = buffer.get_buffer();
			b.offset = 0;
			b.size = VK_WHOLE_SIZE;
			buffer_barriers.push_back(b);
		}
		else
		{
			auto *view = physical_attachments[barriers[i].resource_index];
			VkImageMemoryBarrier b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
			b.image = view->get_image().get_image();
			b.srcAccessMask = 0;
			b.dstAccessMask = barriers[i].access;
			b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			b.newLayout = barriers[i].layout;
			b.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
			b.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
			b.subresourceRange.aspectMask = Vulkan::format_to_aspect_mask(view->get_image().get_format());
			b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			image_barriers.push_back(b);
			view->get_image().set_layout(barriers[i].layout);
		}

		dst_stages |= barriers[i].stages;
	}

	// Transition history resources to SHADER_READ_ONLY_OPTIMAL if necessary.
	if (src_stages == VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT)
	{
		for (auto &image : physical_history_image_attachments)
		{
			if (!image)
				continue;
			if (image->get_layout() == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) // Already in SHADER_READ_ONLY layout, no need to transition.
				continue;

			VkImageMemoryBarrier b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
			b.image = image->get_image();
			b.srcAccessMask = 0;
			b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			b.oldLayout = image->get_layout();
			b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			b.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
			b.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
			b.subresourceRange.aspectMask = Vulkan::format_to_aspect_mask(image->get_format());
			b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			image_barriers.push_back(b);
			image->set_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		}
	}

	cmd.barrier(src_stages, dst_stages, 0, nullptr,
	            buffer_barriers.size(), buffer_barriers.empty() ? nullptr : buffer_barriers.data(),
	            image_barriers.size(), image_barriers.empty() ? nullptr : image_barriers.data());
}

void RenderGraph::bake()
{
	// First, validate that the graph is sane.
	validate_passes();

	auto itr = resource_to_index.find(backbuffer_source);
	if (itr == end(resource_to_index))
		throw logic_error("Backbuffer source does not exist.");

	pushed_passes.clear();
	pushed_passes_tmp.clear();
	pass_stack.clear();
	handled_passes.clear();

	// Work our way back from the backbuffer, and sort out all the dependencies.
	auto &backbuffer_resource = *resources[itr->second];

	if (backbuffer_resource.get_write_passes().empty())
		throw logic_error("No pass exists which writes to resource.");

	for (auto &pass : backbuffer_resource.get_write_passes())
	{
		pass_stack.push_back(pass);
		pushed_passes.push_back(pass);
	}

	const auto depend_passes = [&](const std::unordered_set<unsigned> &passes) {
		if (passes.empty())
			throw logic_error("No pass exists which writes to resource.");

		for (auto &pass : passes)
		{
			pushed_passes_tmp.push_back(pass);
			pass_stack.push_back(pass);
		}
	};

	const auto depend_passes_no_check = [&](const std::unordered_set<unsigned> &passes) {
		for (auto &pass : passes)
		{
			pushed_passes_tmp.push_back(pass);
			pass_stack.push_back(pass);
		}
	};

	const auto depend_passes_no_check_ignore_self = [&](unsigned self, const std::unordered_set<unsigned> &passes) {
		for (auto &pass : passes)
		{
			if (pass != self)
			{
				pushed_passes_tmp.push_back(pass);
				pass_stack.push_back(pass);
			}
		}
	};

	const auto make_unique_list = [](std::vector<unsigned> &passes) {
		// As tie-break rule on ordering, place earlier passes late in the stack.
		sort(begin(passes), end(passes), greater<unsigned>());
		passes.erase(unique(begin(passes), end(passes)), end(passes));
	};

	unsigned iteration_count = 0;

	while (!pushed_passes.empty())
	{
		pushed_passes_tmp.clear();
		make_unique_list(pushed_passes);

		for (auto &pushed_pass : pushed_passes)
		{
			handled_passes.insert(pushed_pass);

			auto &pass = *passes[pushed_pass];
			if (pass.get_depth_stencil_input())
				depend_passes(pass.get_depth_stencil_input()->get_write_passes());
			for (auto *input : pass.get_attachment_inputs())
				depend_passes(input->get_write_passes());

			for (auto *input : pass.get_color_inputs())
			{
				if (input)
					depend_passes(input->get_write_passes());
			}

			for (auto *input : pass.get_color_scale_inputs())
			{
				if (input)
					depend_passes(input->get_write_passes());
			}

			for (auto *input : pass.get_texture_inputs())
				depend_passes(input->get_write_passes());

			for (auto *input : pass.get_storage_inputs())
			{
				if (input)
				{
					// There might be no writers of this resource if it's used in a feedback fashion.
					depend_passes_no_check(input->get_write_passes());
					// Deal with write-after-read hazards if a storage buffer is read in other passes
					// (feedback) before being updated.
					depend_passes_no_check_ignore_self(pass.get_index(), input->get_read_passes());
				}
			}

			for (auto *input : pass.get_storage_texture_inputs())
			{
				if (input)
					depend_passes(input->get_write_passes());
			}

			for (auto *input : pass.get_uniform_inputs())
			{
				// There might be no writers of this resource if it's used in a feedback fashion.
				depend_passes_no_check(input->get_write_passes());
			}

			for (auto *input : pass.get_storage_read_inputs())
			{
				// There might be no writers of this resource if it's used in a feedback fashion.
				depend_passes(input->get_write_passes());
			}
		}

		pushed_passes.clear();
		swap(pushed_passes, pushed_passes_tmp);

		if (++iteration_count > passes.size())
			throw logic_error("Cycle detected.");
	}

	reverse(begin(pass_stack), end(pass_stack));
	filter_passes(pass_stack);

	// Now, we have a linear list of passes to submit in-order which would obey the dependencies.

	// Next, try to merge adjacent passes together.
	build_physical_passes();

	// Figure out which physical resources we need. Here we will alias resources which can trivially alias via renaming.
	// E.g. depth input -> depth output is just one physical attachment, similar with color.
	build_physical_resources();

	// After merging physical passes and resources, if an image resource is only used in a single physical pass, make it transient.
	build_transients();

	// Now that we are done, we can make render passes.
	build_render_pass_info();

	// For each render pass in isolation, figure out the barriers required.
	build_barriers();

	// Check if the swapchain needs to be blitted to (in case the geometry does not match the backbuffer).
	swapchain_physical_index = resources[resource_to_index[backbuffer_source]]->get_physical_index();
	physical_dimensions[swapchain_physical_index].transient = false;
	if (physical_dimensions[swapchain_physical_index] != swapchain_dimensions)
		swapchain_physical_index = RenderResource::Unused;
	else
		physical_dimensions[swapchain_physical_index].transient = true;

	// Based on our render graph, figure out the barriers we actually need.
	// Some barriers are implicit (transients), and some are redundant, i.e. same texture read in multiple passes.
	build_physical_barriers();
}

ResourceDimensions RenderGraph::get_resource_dimensions(const RenderBufferResource &resource) const
{
	ResourceDimensions dim;
	auto &info = resource.get_buffer_info();
	dim.buffer_info = info;
	dim.persistent = info.persistent;
	return dim;
}

ResourceDimensions RenderGraph::get_resource_dimensions(const RenderTextureResource &resource) const
{
	ResourceDimensions dim;
	auto &info = resource.get_attachment_info();
	dim.format = info.format;
	dim.transient = resource.get_transient_state();
	dim.persistent = info.persistent;
	dim.storage = resource.get_storage_state();

	switch (info.size_class)
	{
	case SizeClass::SwapchainRelative:
		dim.width = unsigned(info.size_x * swapchain_dimensions.width);
		dim.height = unsigned(info.size_y * swapchain_dimensions.height);
		break;

	case SizeClass::Absolute:
		dim.width = unsigned(info.size_x);
		dim.height = unsigned(info.size_y);
		break;

	case SizeClass::InputRelative:
	{
		auto itr = resource_to_index.find(info.size_relative_name);
		if (itr == end(resource_to_index))
			throw logic_error("Resource does not exist.");
		auto &input = static_cast<RenderTextureResource &>(*resources[itr->second]);
		auto input_dim = get_resource_dimensions(input);

		dim.width = unsigned(input_dim.width * info.size_x);
		dim.height = unsigned(input_dim.height * info.size_y);
		dim.depth = input_dim.depth;
		dim.layers = input_dim.layers;
		dim.levels = input_dim.levels;
		break;
	}
	}

	if (dim.format == VK_FORMAT_UNDEFINED)
		dim.format = swapchain_dimensions.format;

	return dim;
}

void RenderGraph::build_physical_barriers()
{
	initial_barriers.clear();
	initial_top_of_pipe_barriers.clear();

	auto barrier_itr = begin(pass_barriers);

	const auto flush_access_to_invalidate = [](VkAccessFlags flags) -> VkAccessFlags {
		if (flags & VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)
			flags |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
		if (flags & VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT)
			flags |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
		if (flags & VK_ACCESS_SHADER_WRITE_BIT)
			flags |= VK_ACCESS_SHADER_READ_BIT;
		return flags;
	};

	struct ResourceState
	{
		VkImageLayout initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkImageLayout final_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkImageLayout current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkAccessFlags invalidated_types = 0;
		VkAccessFlags flushed_types = 0;

		VkPipelineStageFlags invalidated_stages = 0;
		VkPipelineStageFlags flushed_stages = 0;

		// If we need to tack on multiple invalidates after the fact ...
		unsigned last_invalidate_pass = RenderPass::Unused;
		unsigned last_read_pass = RenderPass::Unused;
		unsigned last_flush_pass = RenderPass::Unused;

		// Have we ever written to this resource in this pass?
		bool has_writer = false;
	};

	// To handle global state.
	vector<ResourceState> global_resource_state(physical_dimensions.size());

	// To handle state inside a physical pass.
	vector<ResourceState> resource_state;
	resource_state.reserve(physical_dimensions.size());

	for (auto &physical_pass : physical_passes)
	{
		resource_state.clear();
		resource_state.resize(physical_dimensions.size());
		unsigned physical_pass_index = unsigned(&physical_pass - physical_passes.data());

		// Go over all physical passes, and observe their use of barriers.
		// In multipass, only the first and last barriers need to be considered externally.
		// Compute never has multipass.
		unsigned subpasses = physical_pass.passes.size();
		for (unsigned i = 0; i < subpasses; i++, ++barrier_itr)
		{
			auto &barriers = *barrier_itr;
			auto &invalidates = barriers.invalidate;
			auto &flushes = barriers.flush;

			for (auto &invalidate : invalidates)
			{
				// Transients and swapchain images are handled implicitly.
				if (physical_dimensions[invalidate.resource_index].transient ||
					invalidate.resource_index == swapchain_physical_index)
				{
					continue;
				}

				global_resource_state[invalidate.resource_index].last_read_pass = physical_pass_index;

				// This is the very first time the resource has been used, but it hasn't been written to.
				// This is a read-only operation and the initial barriers will ensure that transitions are made.
				if (!global_resource_state[invalidate.resource_index].has_writer &&
					resource_state[invalidate.resource_index].initial_layout == VK_IMAGE_LAYOUT_UNDEFINED)
				{
					// This only makes sense for persistent buffer resources. Otherwise we are reading dummy data.
					if (!physical_dimensions[invalidate.resource_index].buffer_info.size ||
					    !physical_dimensions[invalidate.resource_index].persistent)
					{
						throw logic_error("Starting a resource as read-only is only valid for persistent buffers.");
					}

					auto itr = find_if(begin(initial_barriers), end(initial_barriers), [&](const Barrier &barrier) {
						return barrier.resource_index == invalidate.resource_index;
					});

					// We might have multiple, separate readers which all need different access flags and stages,
					// just batch them all up to start of frame.
					if (itr != end(initial_barriers))
					{
						itr->access |= flush_access_to_invalidate(invalidate.access);
						itr->stages |= invalidate.stages;
					}
					else
					{
						initial_barriers.push_back(
							{ invalidate.resource_index,
							  invalidate.layout,
							  flush_access_to_invalidate(invalidate.access),
							  invalidate.stages });
					}
				}

				// Only the first use of a resource in a physical pass needs to be handled externally.
				if (resource_state[invalidate.resource_index].initial_layout == VK_IMAGE_LAYOUT_UNDEFINED)
				{
					resource_state[invalidate.resource_index].invalidated_types |= invalidate.access;
					resource_state[invalidate.resource_index].invalidated_stages |= invalidate.stages;
					resource_state[invalidate.resource_index].initial_layout = invalidate.layout;
				}

				// All pending flushes have been invalidated in the appropriate stages already.
				// This is relevant if the invalidate happens in subpass #1 and beyond.
				resource_state[invalidate.resource_index].flushed_types = 0;
				resource_state[invalidate.resource_index].flushed_stages = 0;
			}

			for (auto &flush : flushes)
			{
				// Transients are handled implicitly.
				if (physical_dimensions[flush.resource_index].transient ||
				    flush.resource_index == swapchain_physical_index)
				{
					continue;
				}

				// The last use of a resource in a physical pass needs to be handled externally.
				resource_state[flush.resource_index].flushed_types |= flush.access;
				resource_state[flush.resource_index].flushed_stages |= flush.stages;
				resource_state[flush.resource_index].final_layout = flush.layout;

				// This is the first time we used this resource, so queue up initial barriers which transition from
				// UNDEFINED to flush.layout on the start of the frame.
				if (resource_state[flush.resource_index].initial_layout == VK_IMAGE_LAYOUT_UNDEFINED)
				{
					// Make sure that no other pass tries to discard the same resource.
					auto itr = find_if(begin(initial_barriers), end(initial_barriers), [&](const Barrier &barrier) {
						return barrier.resource_index == flush.resource_index;
					});
					if (itr != end(initial_barriers))
						throw logic_error("Cannot have two passes which both invalidate a resource.");

					itr = find_if(begin(initial_top_of_pipe_barriers), end(initial_top_of_pipe_barriers), [&](const Barrier &barrier) {
						return barrier.resource_index == flush.resource_index;
					});
					if (itr != end(initial_top_of_pipe_barriers))
						throw logic_error("Cannot have two passes which both invalidate a resource.");

					resource_state[flush.resource_index].initial_layout = flush.layout;
					global_resource_state[flush.resource_index].has_writer = true;

					// If a resource is created anew every frame, there is no reason to wait for previous frame to complete.
					bool need_initial_barrier = !physical_dimensions[flush.resource_index].buffer_info.size ||
					                            physical_dimensions[flush.resource_index].persistent;

					if (need_initial_barrier)
					{
						// For storage images which are not persistent, we recreate them every frame, so
						// we can use top-of-pipe barrier to do the initial transition.
						if (!physical_dimensions[flush.resource_index].buffer_info.size &&
							!physical_dimensions[flush.resource_index].persistent)
						{
							initial_top_of_pipe_barriers.push_back(
								{ flush.resource_index,
								  flush.layout,
								  flush_access_to_invalidate(flush.access),
								  flush.stages });
						}
						else
						{
							initial_barriers.push_back(
								{ flush.resource_index,
								  flush.layout,
								  flush_access_to_invalidate(flush.access),
								  flush.stages });
						}
					}
				}
			}
		}

		// Now that the render pass has been studied, look at each resource individually and see how we need to deal
		// with the physical render pass as a whole.
		for (auto &resource : resource_state)
		{
			// Resource was not touched in this pass.
			if (resource.final_layout == VK_IMAGE_LAYOUT_UNDEFINED && resource.initial_layout == VK_IMAGE_LAYOUT_UNDEFINED)
				continue;

			bool read_only_pass = false;
			// If there are only invalidations in this pass it is read-only, and the final layout becomes the initial one.
			// Promote the last initial layout to the final layout.
			if (resource.final_layout == VK_IMAGE_LAYOUT_UNDEFINED)
			{
				resource.final_layout = resource.initial_layout;
				read_only_pass = true;
			}

			unsigned index = unsigned(&resource - resource_state.data());

			bool need_invalidate_barrier = false;
			if (resource.initial_layout != VK_IMAGE_LAYOUT_UNDEFINED)
			{
				if (resource.initial_layout != global_resource_state[index].initial_layout)
				{
					// Need to change the image layout before we start.
					// If we change the layout, we need to invalidate all types and stages again.
					global_resource_state[index].invalidated_types = 0;
					global_resource_state[index].invalidated_stages = 0;
				}

				if (resource.invalidated_stages & ~global_resource_state[index].invalidated_stages)
				{
					// There are some stages which have yet to be made visible to relevant stages.
					// If we introduce new stages, make sure we don't forget the relevant types to the new stages which we introduced.
					need_invalidate_barrier = true;
					global_resource_state[index].invalidated_types = 0;
				}
				else if (resource.invalidated_types & ~global_resource_state[index].invalidated_types)
				{
					// There are some access flags which have yet to be made visible to relevant stages.
					need_invalidate_barrier = true;
				}

				// If we have never flushed anything, there is no need to invalidate yet.
				if (read_only_pass && global_resource_state[index].current_layout == VK_IMAGE_LAYOUT_UNDEFINED)
					need_invalidate_barrier = false;
			}

			// Do we need to invalidate this resource before starting the pass?
			if (need_invalidate_barrier)
			{
				Barrier *last_barrier = nullptr;

				// Find the last time this resource was invalidated.
				if (global_resource_state[index].last_invalidate_pass != RenderPass::Unused)
				{
					unsigned last_pass = global_resource_state[index].last_invalidate_pass;
					auto itr = find_if(begin(physical_passes[last_pass].invalidate), end(physical_passes[last_pass].invalidate), [index](const Barrier &b) {
						return b.resource_index == index;
					});

					if (itr != end(physical_passes[last_pass].invalidate))
						last_barrier = &*itr;
				}

				// If we just need to tack on more access flags or stages,
				// and no layout change is needed, just modify the old barrier.
				if (last_barrier && last_barrier->layout == resource.initial_layout)
				{
					last_barrier->access |= resource.invalidated_types;
					last_barrier->stages |= resource.invalidated_stages;
				}
				else
				{
					physical_pass.invalidate.push_back(
						{ index, resource.initial_layout, resource.invalidated_types, resource.invalidated_stages });
					global_resource_state[index].invalidated_types |= resource.invalidated_types;
					global_resource_state[index].invalidated_stages |= resource.invalidated_stages;
					global_resource_state[index].current_layout = resource.initial_layout;
					global_resource_state[index].last_invalidate_pass = physical_pass_index;
					global_resource_state[index].last_flush_pass = RenderPass::Unused;
					global_resource_state[index].flushed_types = 0;
				}
			}

			if (resource.flushed_types)
			{
				if (global_resource_state[index].last_flush_pass != RenderPass::Unused)
					throw logic_error("Two flushes in a row observed. Need to invalidate at least once in-between each flush.");

				// Did the pass write anything in this pass which needs to be flushed?
				physical_pass.flush.push_back({ index, resource.final_layout, resource.flushed_types, resource.flushed_stages });

				// We cannot move any invalidates to earlier passes now, so clear this state out.
				global_resource_state[index].invalidated_types = 0;
				global_resource_state[index].invalidated_stages = 0;
				global_resource_state[index].last_invalidate_pass = RenderPass::Unused;

				// Just to detect if we have two flushes in a row. This is illegal.
				global_resource_state[index].last_flush_pass = physical_pass_index;
			}
			else if (resource.invalidated_types)
			{
				// Did the pass read anything in this pass which needs to be protected before it can be written?
				// Implement this as a flush with 0 access bits.
				// This is how Vulkan essentially implements a write-after-read hazard.
				// The only purpose of this flush barrier is to set the last pass which the resource was used as a stage.
				physical_pass.flush.push_back({ index, resource.final_layout, 0, resource.invalidated_stages });
			}

			global_resource_state[index].current_layout = resource.final_layout;
		}
	}

	// If there are pending writes which have not been invalidated, they will vanish into the aether.
	// This is only relevant for the swapchain dummy image, but this will be explicitly synchronized later.
}

void RenderGraph::build_barriers()
{
	pass_barriers.clear();
	pass_barriers.reserve(pass_stack.size());

	const auto get_access = [&](vector<Barrier> &barriers, unsigned index) -> Barrier & {
		auto itr = find_if(begin(barriers), end(barriers), [index](const Barrier &b) {
			return index == b.resource_index;
		});
		if (itr != end(barriers))
			return *itr;
		else
		{
			barriers.push_back({ index, VK_IMAGE_LAYOUT_UNDEFINED, 0, 0 });
			return barriers.back();
		}
	};

	for (auto &index : pass_stack)
	{
		auto &pass = *passes[index];
		Barriers barriers;

		const auto get_invalidate_access = [&](unsigned index) -> Barrier & {
			return get_access(barriers.invalidate, index);
		};

		const auto get_flush_access = [&](unsigned index) -> Barrier & {
			return get_access(barriers.flush, index);
		};

		for (auto *input : pass.get_uniform_inputs())
		{
			auto &barrier = get_invalidate_access(input->get_physical_index());
			barrier.access |= VK_ACCESS_UNIFORM_READ_BIT;
			if (pass.get_stages() == VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT)
				barrier.stages |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT; // TODO: Pick appropriate stage.
			else
				barrier.stages |= pass.get_stages();

			if (barrier.layout != VK_IMAGE_LAYOUT_UNDEFINED)
				throw logic_error("Layout mismatch.");
			barrier.layout = VK_IMAGE_LAYOUT_GENERAL; // It's a buffer, but use this as a sentinel.
		}

		for (auto *input : pass.get_storage_read_inputs())
		{
			auto &barrier = get_invalidate_access(input->get_physical_index());
			barrier.access |= VK_ACCESS_SHADER_READ_BIT;
			if (pass.get_stages() == VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT)
				barrier.stages |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; // TODO: Pick appropriate stage.
			else
				barrier.stages |= pass.get_stages();

			if (barrier.layout != VK_IMAGE_LAYOUT_UNDEFINED)
				throw logic_error("Layout mismatch.");
			barrier.layout = VK_IMAGE_LAYOUT_GENERAL; // It's a buffer, but use this as a sentinel.
		}

		for (auto *input : pass.get_texture_inputs())
		{
			auto &barrier = get_invalidate_access(input->get_physical_index());
			barrier.access |= VK_ACCESS_SHADER_READ_BIT;

			if (pass.get_stages() == VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT)
				barrier.stages |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; // TODO: VERTEX_SHADER can also read textures!
			else
				barrier.stages |= pass.get_stages();

			if (barrier.layout != VK_IMAGE_LAYOUT_UNDEFINED)
				throw logic_error("Layout mismatch.");
			barrier.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}

		for (auto *input : pass.get_attachment_inputs())
		{
			if (pass.get_stages() != VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT)
				throw logic_error("Only graphics passes can have input attachments.");

			auto &barrier = get_invalidate_access(input->get_physical_index());
			barrier.access |= VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
			barrier.stages |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			if (barrier.layout != VK_IMAGE_LAYOUT_UNDEFINED)
				throw logic_error("Layout mismatch.");
			barrier.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}

		for (auto *input : pass.get_storage_inputs())
		{
			if (!input)
				continue;

			auto &barrier = get_invalidate_access(input->get_physical_index());
			barrier.access |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			if (pass.get_stages() == VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT)
				barrier.stages |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; // TODO: Pick appropriate stage.
			else
				barrier.stages |= pass.get_stages();

			if (barrier.layout != VK_IMAGE_LAYOUT_UNDEFINED)
				throw logic_error("Layout mismatch.");
			barrier.layout = VK_IMAGE_LAYOUT_GENERAL;
		}

		for (auto *input : pass.get_storage_texture_inputs())
		{
			if (!input)
				continue;

			auto &barrier = get_invalidate_access(input->get_physical_index());
			barrier.access |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			if (pass.get_stages() == VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT)
				barrier.stages |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; // TODO: Pick appropriate stage.
			else
				barrier.stages |= pass.get_stages();

			if (barrier.layout != VK_IMAGE_LAYOUT_UNDEFINED)
				throw logic_error("Layout mismatch.");
			barrier.layout = VK_IMAGE_LAYOUT_GENERAL;
		}

		for (auto *input : pass.get_color_inputs())
		{
			if (!input)
				continue;

			if (pass.get_stages() != VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT)
				throw logic_error("Only graphics passes can have color inputs.");

			auto &barrier = get_invalidate_access(input->get_physical_index());
			barrier.access |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
			barrier.stages |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			if (barrier.layout != VK_IMAGE_LAYOUT_UNDEFINED)
				throw logic_error("Layout mismatch.");
			barrier.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}

		for (auto *input : pass.get_color_scale_inputs())
		{
			if (!input)
				continue;

			if (pass.get_stages() != VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT)
				throw logic_error("Only graphics passes can have scaled color inputs.");

			auto &barrier = get_invalidate_access(input->get_physical_index());
			barrier.access |= VK_ACCESS_SHADER_READ_BIT;
			barrier.stages |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			if (barrier.layout != VK_IMAGE_LAYOUT_UNDEFINED)
				throw logic_error("Layout mismatch.");
			barrier.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}

		for (auto *output : pass.get_color_outputs())
		{
			if (pass.get_stages() != VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT)
				throw logic_error("Only graphics passes can have scaled color outputs.");

			auto &barrier = get_flush_access(output->get_physical_index());
			barrier.access |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			barrier.stages |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			if (barrier.layout != VK_IMAGE_LAYOUT_UNDEFINED)
				throw logic_error("Layout mismatch.");
			barrier.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}

		for (auto *output : pass.get_storage_outputs())
		{
			auto &barrier = get_flush_access(output->get_physical_index());
			barrier.access |= VK_ACCESS_SHADER_WRITE_BIT;

			if (pass.get_stages() == VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT)
				barrier.stages |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; // TODO: Pick appropriate stage.
			else
				barrier.stages |= pass.get_stages();

			if (barrier.layout != VK_IMAGE_LAYOUT_UNDEFINED)
				throw logic_error("Layout mismatch.");
			barrier.layout = VK_IMAGE_LAYOUT_GENERAL;
		}

		for (auto *output : pass.get_storage_texture_outputs())
		{
			auto &barrier = get_flush_access(output->get_physical_index());
			barrier.access |= VK_ACCESS_SHADER_WRITE_BIT;

			if (pass.get_stages() == VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT)
				barrier.stages |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; // TODO: Pick appropriate stage.
			else
				barrier.stages |= pass.get_stages();

			if (barrier.layout != VK_IMAGE_LAYOUT_UNDEFINED)
				throw logic_error("Layout mismatch.");
			barrier.layout = VK_IMAGE_LAYOUT_GENERAL;
		}

		auto *output = pass.get_depth_stencil_output();
		auto *input = pass.get_depth_stencil_input();

		if ((output || input) && pass.get_stages() != VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT)
			throw logic_error("Only graphics passes can have depth attachments.");

		if (output && input)
		{
			auto &dst_barrier = get_invalidate_access(input->get_physical_index());
			auto &src_barrier = get_flush_access(output->get_physical_index());

			if (dst_barrier.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
				dst_barrier.layout = VK_IMAGE_LAYOUT_GENERAL;
			else
				dst_barrier.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			dst_barrier.access |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			dst_barrier.stages |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;

			src_barrier.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			src_barrier.access |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			src_barrier.stages |= VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		}
		else if (input)
		{
			auto &dst_barrier = get_invalidate_access(input->get_physical_index());
			if (dst_barrier.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
				dst_barrier.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
			else
				dst_barrier.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			dst_barrier.access |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
			dst_barrier.stages |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		}
		else if (output)
		{
			auto &src_barrier = get_flush_access(output->get_physical_index());
			src_barrier.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			src_barrier.access |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			src_barrier.stages |= VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		}

		pass_barriers.push_back(move(barriers));
	}
}

void RenderGraph::filter_passes(std::vector<unsigned> &list)
{
	unordered_set<unsigned> seen;

	auto output_itr = begin(list);
	for (auto itr = begin(list); itr != end(list); ++itr)
	{
		if (!seen.count(*itr))
		{
			*output_itr = *itr;
			seen.insert(*itr);
			++output_itr;
		}
	}
	list.erase(output_itr, end(list));
}

void RenderGraph::reset()
{
	passes.clear();
	resources.clear();
	pass_to_index.clear();
	resource_to_index.clear();
	physical_passes.clear();
	physical_dimensions.clear();
	physical_attachments.clear();
	physical_buffers.clear();
}

}