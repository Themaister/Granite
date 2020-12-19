/* Copyright (c) 2017-2020 Hans-Kristian Arntzen
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

#include "render_graph.hpp"
#include "type_to_string.hpp"
#include "format.hpp"
#include "quirks.hpp"
#include "muglm/muglm_impl.hpp"
#include "thread_group.hpp"
#include "task_composer.hpp"
#include "vulkan_prerotate.hpp"
#include <algorithm>

using namespace std;

namespace Granite
{
bool RenderPassInterface::render_pass_is_conditional() const
{
	return false;
}

bool RenderPassInterface::render_pass_is_separate_layered() const
{
	return false;
}

bool RenderPassInterface::need_render_pass() const
{
	return true;
}

bool RenderPassInterface::get_clear_depth_stencil(VkClearDepthStencilValue *value) const
{
	if (value)
		*value = { 1.0f, 0u };
	return true;
}

bool RenderPassInterface::get_clear_color(unsigned, VkClearColorValue *value) const
{
	if (value)
		*value = {};
	return true;
}

void RenderPassInterface::build_render_pass(Vulkan::CommandBuffer &)
{
}

void RenderPassInterface::build_render_pass_separate_layer(Vulkan::CommandBuffer &, unsigned)
{
}

void RenderPassInterface::enqueue_prepare_render_pass(TaskComposer &, const Vulkan::RenderPassInfo &, unsigned,
                                                      VkSubpassContents &contents)
{
	contents = VK_SUBPASS_CONTENTS_INLINE;
}

static const RenderGraphQueueFlags compute_queues = RENDER_GRAPH_QUEUE_ASYNC_COMPUTE_BIT |
                                                    RENDER_GRAPH_QUEUE_COMPUTE_BIT;

RenderTextureResource &RenderPass::add_attachment_input(const std::string &name)
{
	auto &res = graph.get_texture_resource(name);
	res.add_queue(queue);
	res.read_in_pass(index);
	res.add_image_usage(VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT);
	attachments_inputs.push_back(&res);
	return res;
}

RenderTextureResource &RenderPass::add_history_input(const std::string &name)
{
	auto &res = graph.get_texture_resource(name);
	res.add_queue(queue);
	res.add_image_usage(VK_IMAGE_USAGE_SAMPLED_BIT);
	// History inputs are not used in any particular pass, but next frame.
	history_inputs.push_back(&res);
	return res;
}

RenderBufferResource &RenderPass::add_generic_buffer_input(const std::string &name, VkPipelineStageFlags stages,
                                                           VkAccessFlags access, VkBufferUsageFlags usage)
{
	auto &res = graph.get_buffer_resource(name);
	res.add_queue(queue);
	res.read_in_pass(index);
	res.add_buffer_usage(usage);

	AccessedBufferResource acc;
	acc.buffer = &res;
	acc.layout = VK_IMAGE_LAYOUT_GENERAL;
	acc.access = access;
	acc.stages = stages;
	generic_buffer.push_back(acc);
	return res;
}

RenderBufferResource &RenderPass::add_vertex_buffer_input(const std::string &name)
{
	return add_generic_buffer_input(name,
	                                VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
	                                VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
	                                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
}

RenderBufferResource &RenderPass::add_index_buffer_input(const std::string &name)
{
	return add_generic_buffer_input(name,
	                                VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
	                                VK_ACCESS_INDEX_READ_BIT,
	                                VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
}

RenderBufferResource &RenderPass::add_indirect_buffer_input(const std::string &name)
{
	return add_generic_buffer_input(name,
	                                VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
	                                VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
	                                VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
}

RenderBufferResource &RenderPass::add_uniform_input(const std::string &name, VkPipelineStageFlags stages)
{
	if (stages == 0)
	{
		if ((queue & compute_queues) != 0)
			stages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		else
			stages = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	}

	return add_generic_buffer_input(name, stages, VK_ACCESS_UNIFORM_READ_BIT, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
}

RenderBufferResource &RenderPass::add_storage_read_only_input(const std::string &name, VkPipelineStageFlags stages)
{
	if (stages == 0)
	{
		if ((queue & compute_queues) != 0)
			stages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		else
			stages = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	}

	return add_generic_buffer_input(name, stages, VK_ACCESS_SHADER_READ_BIT, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
}

RenderBufferResource &RenderPass::add_storage_output(const std::string &name, const BufferInfo &info, const std::string &input)
{
	auto &res = graph.get_buffer_resource(name);
	res.add_queue(queue);
	res.set_buffer_info(info);
	res.written_in_pass(index);
	res.add_buffer_usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
	storage_outputs.push_back(&res);

	if (!input.empty())
	{
		auto &input_res = graph.get_buffer_resource(input);
		input_res.read_in_pass(index);
		input_res.add_buffer_usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
		storage_inputs.push_back(&input_res);
	}
	else
		storage_inputs.push_back(nullptr);

	return res;
}

RenderBufferResource &RenderPass::add_transfer_output(const std::string &name, const BufferInfo &info)
{
	auto &res = graph.get_buffer_resource(name);
	res.add_queue(queue);
	res.set_buffer_info(info);
	res.written_in_pass(index);
	res.add_buffer_usage(VK_BUFFER_USAGE_TRANSFER_DST_BIT);
	transfer_outputs.push_back(&res);
	return res;
}

RenderTextureResource &RenderPass::add_texture_input(const std::string &name, VkPipelineStageFlags stages)
{
	auto &res = graph.get_texture_resource(name);
	res.add_queue(queue);
	res.read_in_pass(index);
	res.add_image_usage(VK_IMAGE_USAGE_SAMPLED_BIT);

	// Support duplicate add_texture_inputs.
	auto itr = find_if(begin(generic_texture), end(generic_texture), [&](const AccessedTextureResource &acc) {
		return acc.texture == &res;
	});

	if (itr != end(generic_texture))
		return *itr->texture;

	AccessedTextureResource acc;
	acc.texture = &res;
	acc.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	acc.access = VK_ACCESS_SHADER_READ_BIT;

	if (stages != 0)
		acc.stages = stages;
	else if ((queue & compute_queues) != 0)
		acc.stages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	else
		acc.stages = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

	generic_texture.push_back(acc);
	return res;
}

RenderTextureResource &RenderPass::add_resolve_output(const std::string &name, const AttachmentInfo &info)
{
	auto &res = graph.get_texture_resource(name);
	res.add_queue(queue);
	res.written_in_pass(index);
	res.set_attachment_info(info);
	res.add_image_usage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
	resolve_outputs.push_back(&res);
	return res;
}

RenderTextureResource &RenderPass::add_color_output(const std::string &name, const AttachmentInfo &info, const std::string &input)
{
	auto &res = graph.get_texture_resource(name);
	res.add_queue(queue);
	res.written_in_pass(index);
	res.set_attachment_info(info);
	res.add_image_usage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);

	if (info.levels != 1)
		res.add_image_usage(VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);

	color_outputs.push_back(&res);

	if (!input.empty())
	{
		auto &input_res = graph.get_texture_resource(input);
		input_res.read_in_pass(index);
		input_res.add_image_usage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
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
	res.add_queue(queue);
	res.written_in_pass(index);
	res.set_attachment_info(info);
	res.add_image_usage(VK_IMAGE_USAGE_STORAGE_BIT);
	storage_texture_outputs.push_back(&res);

	if (!input.empty())
	{
		auto &input_res = graph.get_texture_resource(input);
		input_res.read_in_pass(index);
		input_res.add_image_usage(VK_IMAGE_USAGE_STORAGE_BIT);
		storage_texture_inputs.push_back(&input_res);
	}
	else
		storage_texture_inputs.push_back(nullptr);

	return res;
}

void RenderPass::add_fake_resource_write_alias(const std::string &from, const std::string &to)
{
	auto &from_res = graph.get_texture_resource(from);
	auto &to_res = graph.get_texture_resource(to);
	to_res = from_res;
	to_res.get_read_passes().clear();
	to_res.get_write_passes().clear();
	to_res.written_in_pass(index);

	fake_resource_alias.emplace_back(&from_res, &to_res);
}

RenderTextureResource &RenderPass::add_blit_texture_read_only_input(const std::string &name)
{
	auto &res = graph.get_texture_resource(name);
	res.add_queue(queue);
	res.read_in_pass(index);
	res.add_image_usage(VK_IMAGE_USAGE_TRANSFER_SRC_BIT);

	AccessedTextureResource acc;
	acc.texture = &res;
	acc.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	acc.access = VK_ACCESS_TRANSFER_READ_BIT;
	acc.stages = VK_PIPELINE_STAGE_TRANSFER_BIT;

	generic_texture.push_back(acc);
	return res;
}

RenderTextureResource &RenderPass::add_blit_texture_output(const std::string &name, const AttachmentInfo &info,
                                                           const std::string &input)
{
	auto &res = graph.get_texture_resource(name);
	res.add_queue(queue);
	res.written_in_pass(index);
	res.set_attachment_info(info);
	res.add_image_usage(VK_IMAGE_USAGE_TRANSFER_DST_BIT);
	blit_texture_outputs.push_back(&res);

	if (!input.empty())
	{
		auto &input_res = graph.get_texture_resource(input);
		input_res.read_in_pass(index);
		input_res.add_image_usage(VK_IMAGE_USAGE_TRANSFER_DST_BIT);
		blit_texture_inputs.push_back(&input_res);
	}
	else
		blit_texture_inputs.push_back(nullptr);

	return res;
}

RenderTextureResource &RenderPass::set_depth_stencil_output(const std::string &name, const AttachmentInfo &info)
{
	auto &res = graph.get_texture_resource(name);
	res.add_queue(queue);
	res.written_in_pass(index);
	res.set_attachment_info(info);
	res.add_image_usage(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
	depth_stencil_output = &res;
	return res;
}

RenderTextureResource &RenderPass::set_depth_stencil_input(const std::string &name)
{
	auto &res = graph.get_texture_resource(name);
	res.add_queue(queue);
	res.read_in_pass(index);
	res.add_image_usage(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
	depth_stencil_input = &res;
	return res;
}

RenderGraph::RenderGraph()
{
	EVENT_MANAGER_REGISTER_LATCH(RenderGraph, on_swapchain_changed, on_swapchain_destroyed, Vulkan::SwapchainParameterEvent);
	EVENT_MANAGER_REGISTER_LATCH(RenderGraph, on_device_created, on_device_destroyed, Vulkan::DeviceCreatedEvent);
}

void RenderGraph::on_swapchain_destroyed(const Vulkan::SwapchainParameterEvent &)
{
	physical_image_attachments.clear();
	physical_history_image_attachments.clear();
	physical_events.clear();
	physical_history_events.clear();
}

void RenderGraph::on_swapchain_changed(const Vulkan::SwapchainParameterEvent &)
{
}

void RenderGraph::on_device_created(const Vulkan::DeviceCreatedEvent &)
{
}

void RenderGraph::on_device_destroyed(const Vulkan::DeviceCreatedEvent &)
{
	physical_buffers.clear();
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
		resources.back()->set_name(name);
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
		resources.back()->set_name(name);
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

RenderPass &RenderGraph::add_pass(const std::string &name, RenderGraphQueueFlagBits queue)
{
	auto itr = pass_to_index.find(name);
	if (itr != end(pass_to_index))
	{
		return *passes[itr->second];
	}
	else
	{
		unsigned index = passes.size();
		passes.emplace_back(new RenderPass(*this, index, queue));
		passes.back()->set_name(name);
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

		if (pass.get_blit_texture_inputs().size() != pass.get_blit_texture_outputs().size())
			throw logic_error("Size of blit inputs must match blit outputs.");

		if (pass.get_storage_texture_inputs().size() != pass.get_storage_texture_outputs().size())
			throw logic_error("Size of storage texture inputs must match storage texture outputs.");

		if (!pass.get_resolve_outputs().empty() && pass.get_resolve_outputs().size() != pass.get_color_outputs().size())
			throw logic_error("Must have one resolve output for each color output.");

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

		if (!pass.get_blit_texture_outputs().empty())
		{
			unsigned num_outputs = pass.get_blit_texture_outputs().size();
			for (unsigned i = 0; i < num_outputs; i++)
			{
				if (!pass.get_blit_texture_inputs()[i])
					continue;

				if (get_resource_dimensions(*pass.get_blit_texture_inputs()[i]) != get_resource_dimensions(*pass.get_blit_texture_outputs()[i]))
					throw logic_error("Doing RMW on a blit image, but usage and sizes do not match.");
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

		for (auto &input : pass.get_generic_texture_inputs())
		{
			if (input.texture->get_physical_index() == RenderResource::Unused)
			{
				physical_dimensions.push_back(get_resource_dimensions(*input.texture));
				input.texture->set_physical_index(phys_index++);
			}
			else
			{
				physical_dimensions[input.texture->get_physical_index()].queues |= input.texture->get_used_queues();
				physical_dimensions[input.texture->get_physical_index()].image_usage |= input.texture->get_image_usage();
			}
		}

		for (auto &input : pass.get_generic_buffer_inputs())
		{
			if (input.buffer->get_physical_index() == RenderResource::Unused)
			{
				physical_dimensions.push_back(get_resource_dimensions(*input.buffer));
				input.buffer->set_physical_index(phys_index++);
			}
			else
			{
				physical_dimensions[input.buffer->get_physical_index()].queues |= input.buffer->get_used_queues();
				physical_dimensions[input.buffer->get_physical_index()].image_usage |= input.buffer->get_buffer_usage();
			}
		}

		for (auto *input : pass.get_color_scale_inputs())
		{
			if (input && input->get_physical_index() == RenderResource::Unused)
			{
				physical_dimensions.push_back(get_resource_dimensions(*input));
				input->set_physical_index(phys_index++);
				physical_dimensions[input->get_physical_index()].image_usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
			}
			else if (input)
			{
				physical_dimensions[input->get_physical_index()].queues |= input->get_used_queues();
				physical_dimensions[input->get_physical_index()].image_usage |= input->get_image_usage();
				physical_dimensions[input->get_physical_index()].image_usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
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
					else
					{
						physical_dimensions[input->get_physical_index()].queues |= input->get_used_queues();
						physical_dimensions[input->get_physical_index()].image_usage |= input->get_image_usage();
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
					else
					{
						physical_dimensions[input->get_physical_index()].queues |= input->get_used_queues();
						physical_dimensions[input->get_physical_index()].buffer_info.usage |= input->get_buffer_usage();
					}

					if (pass.get_storage_outputs()[i]->get_physical_index() == RenderResource::Unused)
						pass.get_storage_outputs()[i]->set_physical_index(input->get_physical_index());
					else if (pass.get_storage_outputs()[i]->get_physical_index() != input->get_physical_index())
						throw logic_error("Cannot alias resources. Index already claimed.");
				}
			}
		}

		if (!pass.get_blit_texture_inputs().empty())
		{
			unsigned size = pass.get_blit_texture_inputs().size();
			for (unsigned i = 0; i < size; i++)
			{
				auto *input = pass.get_blit_texture_inputs()[i];
				if (input)
				{
					if (input->get_physical_index() == RenderResource::Unused)
					{
						physical_dimensions.push_back(get_resource_dimensions(*input));
						input->set_physical_index(phys_index++);
					}
					else
					{
						physical_dimensions[input->get_physical_index()].queues |= input->get_used_queues();
						physical_dimensions[input->get_physical_index()].image_usage |= input->get_image_usage();
					}

					if (pass.get_blit_texture_outputs()[i]->get_physical_index() == RenderResource::Unused)
						pass.get_blit_texture_outputs()[i]->set_physical_index(input->get_physical_index());
					else if (pass.get_blit_texture_outputs()[i]->get_physical_index() != input->get_physical_index())
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
					else
					{
						physical_dimensions[input->get_physical_index()].queues |= input->get_used_queues();
						physical_dimensions[input->get_physical_index()].image_usage |= input->get_image_usage();
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
			else
			{
				physical_dimensions[output->get_physical_index()].queues |= output->get_used_queues();
				physical_dimensions[output->get_physical_index()].image_usage |= output->get_image_usage();
			}
		}

		for (auto *output : pass.get_resolve_outputs())
		{
			if (output->get_physical_index() == RenderResource::Unused)
			{
				physical_dimensions.push_back(get_resource_dimensions(*output));
				output->set_physical_index(phys_index++);
			}
			else
			{
				physical_dimensions[output->get_physical_index()].queues |= output->get_used_queues();
				physical_dimensions[output->get_physical_index()].image_usage |= output->get_image_usage();
			}
		}

		for (auto *output : pass.get_storage_outputs())
		{
			if (output->get_physical_index() == RenderResource::Unused)
			{
				physical_dimensions.push_back(get_resource_dimensions(*output));
				output->set_physical_index(phys_index++);
			}
			else
			{
				physical_dimensions[output->get_physical_index()].queues |= output->get_used_queues();
				physical_dimensions[output->get_physical_index()].buffer_info.usage |= output->get_buffer_usage();
			}
		}

		for (auto *output : pass.get_transfer_outputs())
		{
			if (output->get_physical_index() == RenderResource::Unused)
			{
				physical_dimensions.push_back(get_resource_dimensions(*output));
				output->set_physical_index(phys_index++);
			}
			else
			{
				physical_dimensions[output->get_physical_index()].queues |= output->get_used_queues();
				physical_dimensions[output->get_physical_index()].buffer_info.usage |= output->get_buffer_usage();
			}
		}

		for (auto *output : pass.get_blit_texture_outputs())
		{
			if (output->get_physical_index() == RenderResource::Unused)
			{
				physical_dimensions.push_back(get_resource_dimensions(*output));
				output->set_physical_index(phys_index++);
			}
			else
			{
				physical_dimensions[output->get_physical_index()].queues |= output->get_used_queues();
				physical_dimensions[output->get_physical_index()].image_usage |= output->get_image_usage();
			}
		}

		for (auto *output : pass.get_storage_texture_outputs())
		{
			if (output->get_physical_index() == RenderResource::Unused)
			{
				physical_dimensions.push_back(get_resource_dimensions(*output));
				output->set_physical_index(phys_index++);
			}
			else
			{
				physical_dimensions[output->get_physical_index()].queues |= output->get_used_queues();
				physical_dimensions[output->get_physical_index()].image_usage |= output->get_image_usage();
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
			else
			{
				physical_dimensions[ds_input->get_physical_index()].queues |= ds_input->get_used_queues();
				physical_dimensions[ds_input->get_physical_index()].image_usage |= ds_input->get_image_usage();
			}

			if (ds_output)
			{
				if (ds_output->get_physical_index() == RenderResource::Unused)
					ds_output->set_physical_index(ds_input->get_physical_index());
				else if (ds_output->get_physical_index() != ds_input->get_physical_index())
					throw logic_error("Cannot alias resources. Index already claimed.");

				physical_dimensions[ds_output->get_physical_index()].queues |= ds_output->get_used_queues();
				physical_dimensions[ds_output->get_physical_index()].image_usage |= ds_output->get_image_usage();
			}
		}
		else if (ds_output)
		{
			if (ds_output->get_physical_index() == RenderResource::Unused)
			{
				physical_dimensions.push_back(get_resource_dimensions(*ds_output));
				ds_output->set_physical_index(phys_index++);
			}
			else
			{
				physical_dimensions[ds_output->get_physical_index()].queues |= ds_output->get_used_queues();
				physical_dimensions[ds_output->get_physical_index()].image_usage |= ds_output->get_image_usage();
			}
		}

		// Assign input attachments last so they can alias properly with existing color/depth attachments in the
		// same subpass.
		for (auto *input : pass.get_attachment_inputs())
		{
			if (input->get_physical_index() == RenderResource::Unused)
			{
				physical_dimensions.push_back(get_resource_dimensions(*input));
				input->set_physical_index(phys_index++);
			}
			else
			{
				physical_dimensions[input->get_physical_index()].queues |= input->get_used_queues();
				physical_dimensions[input->get_physical_index()].image_usage |= input->get_image_usage();
			}
		}

		for (auto &pair : pass.get_fake_resource_aliases())
			pair.second->set_physical_index(pair.first->get_physical_index());
	}

	// Figure out which physical resources need to have history.
	physical_image_has_history.clear();
	physical_image_has_history.resize(physical_dimensions.size());

	for (auto &pass_index : pass_stack)
	{
		auto &pass = *passes[pass_index];
		for (auto &history : pass.get_history_inputs())
		{
			unsigned history_phys_index = history->get_physical_index();
			if (history_phys_index == RenderResource::Unused)
				throw logic_error("History input is used, but it was never written to.");
			physical_image_has_history[history_phys_index] = true;
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
		// Storage images are never transient.
		if (dim.is_buffer_like())
			dim.transient = false;
		else
			dim.transient = true;

		unsigned index = unsigned(&dim - physical_dimensions.data());
		if (physical_image_has_history[index])
			dim.transient = false;

		if (Vulkan::format_has_depth_or_stencil_aspect(dim.format) && !Vulkan::ImplementationQuirks::get().use_transient_depth_stencil)
			dim.transient = false;
		if (!Vulkan::format_has_depth_or_stencil_aspect(dim.format) && !Vulkan::ImplementationQuirks::get().use_transient_color)
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
			auto subpass_index = unsigned(&subpass - physical_pass.passes.data());

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
						if (pass.get_clear_color(i))
						{
							rp.clear_attachments |= 1u << res.first;
							physical_pass.color_clear_requests.push_back({ &pass, &rp.clear_color[res.first], i });
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

			if (!pass.get_resolve_outputs().empty())
			{
				physical_pass.subpasses[subpass_index].num_resolve_attachments = num_color_attachments;
				for (unsigned i = 0; i < num_color_attachments; i++)
				{
					auto res = add_unique_color(pass.get_resolve_outputs()[i]->get_physical_index());
					physical_pass.subpasses[subpass_index].resolve_attachments[i] = res.first;
					// Resolve attachments are don't care always.
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

				rp.op_flags |= Vulkan::RENDER_PASS_OP_STORE_DEPTH_STENCIL_BIT;
				physical_pass.subpasses[subpass_index].depth_stencil_mode = Vulkan::RenderPassInfo::DepthStencil::ReadWrite;
			}
			else if (ds_output)
			{
				auto res = add_unique_ds(ds_output->get_physical_index());
				// If this is the first subpass the attachment is used, we need to either clear or discard.
				if (res.second && pass.get_clear_depth_stencil())
				{
					rp.op_flags |= Vulkan::RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT;
					physical_pass.depth_clear_request.pass = &pass;
					physical_pass.depth_clear_request.target = &rp.clear_depth_stencil;
				}

				rp.op_flags |= Vulkan::RENDER_PASS_OP_STORE_DEPTH_STENCIL_BIT;
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

	const auto find_attachment = [](const vector<RenderTextureResource *> &resource_list, const RenderTextureResource *resource) -> bool {
		if (!resource)
			return false;

		auto itr = find_if(begin(resource_list), end(resource_list), [resource](const RenderTextureResource *res) {
			return res->get_physical_index() == resource->get_physical_index();
		});
		return itr != end(resource_list);
	};

	const auto find_buffer = [](const vector<RenderBufferResource *> &resource_list, const RenderBufferResource *resource) -> bool {
		if (!resource)
			return false;

		auto itr = find_if(begin(resource_list), end(resource_list), [resource](const RenderBufferResource *res) {
			return res->get_physical_index() == resource->get_physical_index();
		});
		return itr != end(resource_list);
	};

	const auto should_merge = [&](const RenderPass &prev, const RenderPass &next) -> bool {
		// Can only merge graphics in same queue.
		if ((prev.get_queue() & compute_queues) || (next.get_queue() != prev.get_queue()))
			return false;

		if (!Vulkan::ImplementationQuirks::get().merge_subpasses)
			return false;

		for (auto *output : prev.get_color_outputs())
		{
			// Need to mip-map after this pass, so cannot merge.
			if (physical_dimensions[output->get_physical_index()].levels > 1)
				return false;
		}

		// Need non-local dependency, cannot merge.
		for (auto &input : next.get_generic_texture_inputs())
		{
			if (find_attachment(prev.get_color_outputs(), input.texture))
				return false;
			if (find_attachment(prev.get_resolve_outputs(), input.texture))
				return false;
			if (find_attachment(prev.get_storage_texture_outputs(), input.texture))
				return false;
			if (find_attachment(prev.get_blit_texture_outputs(), input.texture))
				return false;
			if (input.texture && prev.get_depth_stencil_output() == input.texture)
				return false;
		}

		// Need non-local dependency, cannot merge.
		for (auto &input : next.get_generic_buffer_inputs())
			if (find_buffer(prev.get_storage_outputs(), input.buffer))
				return false;

		// Need non-local dependency, cannot merge.
		for (auto *input : next.get_blit_texture_inputs())
			if (find_attachment(prev.get_blit_texture_inputs(), input))
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
			if (find_attachment(prev.get_blit_texture_outputs(), input))
				return false;
			if (find_attachment(prev.get_color_outputs(), input))
				return false;
			if (find_attachment(prev.get_resolve_outputs(), input))
				return false;
		}


		const auto different_attachment = [](const RenderResource *a, const RenderResource *b) {
			return a && b && a->get_physical_index() != b->get_physical_index();
		};

		const auto same_attachment = [](const RenderResource *a, const RenderResource *b) {
			return a && b && a->get_physical_index() == b->get_physical_index();
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

		for (auto *input : next.get_color_inputs())
		{
			if (!input)
				continue;
			if (find_attachment(prev.get_storage_texture_outputs(), input))
				return false;
			if (find_attachment(prev.get_blit_texture_outputs(), input))
				return false;
		}

		// Now, we have found all failure cases, try to see if we *should* merge.

		// Keep color on tile.
		for (auto *input : next.get_color_inputs())
		{
			if (!input)
				continue;
			if (find_attachment(prev.get_color_outputs(), input))
				return true;
			if (find_attachment(prev.get_resolve_outputs(), input))
				return true;
		}

		// Keep depth on tile.
		if (same_attachment(next.get_depth_stencil_input(), prev.get_depth_stencil_input()) ||
		    same_attachment(next.get_depth_stencil_input(), prev.get_depth_stencil_output()))
		{
			return true;
		}

		// Keep depth attachment or color on-tile.
		for (auto *input : next.get_attachment_inputs())
		{
			if (find_attachment(prev.get_color_outputs(), input))
				return true;
			if (find_attachment(prev.get_resolve_outputs(), input))
				return true;
			if (input && prev.get_depth_stencil_output() == input)
				return true;
		}

		// No reason to merge, so don't.
		return false;
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

	for (auto &phys_pass : physical_passes)
	{
		unsigned index = unsigned(&phys_pass - physical_passes.data());
		for (auto &pass : phys_pass.passes)
			passes[pass]->set_physical_pass_index(index);
	}
}

void RenderGraph::log()
{
	for (auto &resource : physical_dimensions)
	{
		if (resource.buffer_info.size)
		{
			LOGI("Resource #%u (%s): size: %u\n",
			     unsigned(&resource - physical_dimensions.data()),
			     resource.name.c_str(),
			     unsigned(resource.buffer_info.size));
		}
		else
		{
			LOGI("Resource #%u (%s): %u x %u (fmt: %u), samples: %u, transient: %s%s\n",
			     unsigned(&resource - physical_dimensions.data()),
			     resource.name.c_str(),
			     resource.width, resource.height, unsigned(resource.format), resource.samples, resource.transient ? "yes" : "no",
			     unsigned(&resource - physical_dimensions.data()) == swapchain_physical_index ? " (swapchain)" : "");
		}
	}

	auto barrier_itr = begin(pass_barriers);

	const auto swap_str = [this](const Barrier &barrier) -> const char * {
		return barrier.resource_index == swapchain_physical_index ?
	           " (swapchain)" : "";
	};

	for (auto &subpasses : physical_passes)
	{
		LOGI("Physical pass #%u:\n", unsigned(&subpasses - physical_passes.data()));

		for (auto &barrier : subpasses.invalidate)
		{
			LOGI("  Invalidate: %u%s, layout: %s, access: %s, stages: %s\n",
			     barrier.resource_index,
			     swap_str(barrier),
			     Vulkan::layout_to_string(barrier.layout),
			     Vulkan::access_flags_to_string(barrier.access).c_str(),
			     Vulkan::stage_flags_to_string(barrier.stages).c_str());
		}

		for (auto &subpass : subpasses.passes)
		{
			LOGI("    Subpass #%u (%s):\n", unsigned(&subpass - subpasses.passes.data()), this->passes[subpass]->get_name().c_str());
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
			for (auto &output : pass.get_resolve_outputs())
				LOGI("        ResolveAttachment #%u: %u\n", unsigned(&output - pass.get_resolve_outputs().data()), output->get_physical_index());
			for (auto &input : pass.get_attachment_inputs())
				LOGI("        InputAttachment #%u: %u\n", unsigned(&input - pass.get_attachment_inputs().data()), input->get_physical_index());
			for (auto &input : pass.get_generic_texture_inputs())
				LOGI("        Read-only texture #%u: %u\n", unsigned(&input - pass.get_generic_texture_inputs().data()), input.texture->get_physical_index());
			for (auto &input : pass.get_generic_buffer_inputs())
				LOGI("        Read-only buffer #%u: %u\n", unsigned(&input - pass.get_generic_buffer_inputs().data()), input.buffer->get_physical_index());

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

		for (auto &barrier : subpasses.flush)
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

void RenderGraph::enqueue_mipmap_requests(Vulkan::CommandBuffer &cmd, const std::vector<MipmapRequests> &requests)
{
	if (requests.empty())
		return;

	for (auto &req : requests)
	{
		auto &image = physical_attachments[req.physical_resource]->get_image();

		cmd.begin_region("render-graph-mipgen");
		cmd.barrier_prepare_generate_mipmap(image, req.layout, req.stages, req.access);

		cmd.generate_mipmap(image);
		cmd.end_region();
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
		defines.emplace_back(string("HAVE_TARGET_") + to_string(req.target), 1);
		cmd.set_texture(0, req.target, *physical_attachments[req.physical_resource], Vulkan::StockSampler::LinearClamp);
	}

	Vulkan::CommandBufferUtil::draw_fullscreen_quad(cmd, "builtin://shaders/quad.vert",
	                                                "builtin://shaders/scaled_readback.frag", defines);
}

void RenderGraph::build_aliases()
{
	struct Range
	{
		unsigned first_write_pass = ~0u;
		unsigned last_write_pass = 0;
		unsigned first_read_pass = ~0u;
		unsigned last_read_pass = 0;
		bool block_alias = false;

		bool has_writer() const
		{
			return first_write_pass <= last_write_pass;
		}

		bool has_reader() const
		{
			return first_read_pass <= last_read_pass;
		}

		bool is_used() const
		{
			return has_writer() || has_reader();
		}

		bool can_alias() const
		{
			// If we read before we have completely written to a resource we need to preserve it, so no alias is possible.
			if (has_reader() && has_writer() && first_read_pass <= first_write_pass)
				return false;
			if (block_alias)
				return false;
			return true;
		}

		unsigned last_used_pass() const
		{
			unsigned last_pass = 0;
			if (has_writer())
				last_pass = std::max(last_pass, last_write_pass);
			if (has_reader())
				last_pass = std::max(last_pass, last_read_pass);
			return last_pass;
		}

		unsigned first_used_pass() const
		{
			unsigned first_pass = ~0u;
			if (has_writer())
				first_pass = std::min(first_pass, first_write_pass);
			if (has_reader())
				first_pass = std::min(first_pass, first_read_pass);
			return first_pass;
		}

		bool disjoint_lifetime(const Range &range) const
		{
			if (!is_used() || !range.is_used())
				return false;
			if (!can_alias() || !range.can_alias())
				return false;

			bool left = last_used_pass() < range.first_used_pass();
			bool right = range.last_used_pass() < first_used_pass();
			return left || right;
		}
	};

	vector<Range> pass_range(physical_dimensions.size());

	const auto register_reader = [&pass_range](const RenderTextureResource *resource, unsigned pass_index) {
		if (resource && pass_index != RenderPass::Unused)
		{
			unsigned phys = resource->get_physical_index();
			if (phys != RenderResource::Unused)
			{
				auto &range = pass_range[phys];
				range.last_read_pass = std::max(range.last_read_pass, pass_index);
				range.first_read_pass = std::min(range.first_read_pass, pass_index);
			}
		}
	};

	const auto register_writer = [&pass_range](const RenderTextureResource *resource, unsigned pass_index, bool block_alias) {
		if (resource && pass_index != RenderPass::Unused)
		{
			unsigned phys = resource->get_physical_index();
			if (phys != RenderResource::Unused)
			{
				auto &range = pass_range[phys];
				range.last_write_pass = std::max(range.last_write_pass, pass_index);
				range.first_write_pass = std::min(range.first_write_pass, pass_index);
				if (block_alias)
					range.block_alias = block_alias;
			}
		}
	};

	for (auto &pass : pass_stack)
	{
		auto &subpass = *passes[pass];

		for (auto *input : subpass.get_color_inputs())
			register_reader(input, subpass.get_physical_pass_index());
		for (auto *input : subpass.get_color_scale_inputs())
			register_reader(input, subpass.get_physical_pass_index());
		for (auto *input : subpass.get_attachment_inputs())
			register_reader(input, subpass.get_physical_pass_index());
		for (auto &input : subpass.get_generic_texture_inputs())
			register_reader(input.texture, subpass.get_physical_pass_index());
		for (auto *input : subpass.get_blit_texture_inputs())
			register_reader(input, subpass.get_physical_pass_index());
		for (auto *input : subpass.get_storage_texture_inputs())
			register_reader(input, subpass.get_physical_pass_index());
		if (subpass.get_depth_stencil_input())
			register_reader(subpass.get_depth_stencil_input(), subpass.get_physical_pass_index());

		// If a subpass may not execute, we cannot alias with that resource because some other pass may invalidate it.
		bool block_alias = subpass.may_not_need_render_pass();

		if (subpass.get_depth_stencil_output())
			register_writer(subpass.get_depth_stencil_output(), subpass.get_physical_pass_index(), block_alias);
		for (auto *output : subpass.get_color_outputs())
			register_writer(output, subpass.get_physical_pass_index(), block_alias);
		for (auto *output : subpass.get_resolve_outputs())
			register_writer(output, subpass.get_physical_pass_index(), block_alias);
		for (auto *output : subpass.get_blit_texture_outputs())
			register_writer(output, subpass.get_physical_pass_index(), block_alias);

		// Storage textures are not aliased, because they are implicitly preserved.
		for (auto *output : subpass.get_storage_texture_outputs())
			register_writer(output, subpass.get_physical_pass_index(), true);
	}

	vector<vector<unsigned>> alias_chains(physical_dimensions.size());

	physical_aliases.resize(physical_dimensions.size());
	for (auto &v : physical_aliases)
		v = RenderResource::Unused;

	for (unsigned i = 0; i < physical_dimensions.size(); i++)
	{
		// No aliases for buffers.
		if (physical_dimensions[i].buffer_info.size)
			continue;

		// No aliases for images with history.
		if (physical_image_has_history[i])
			continue;

		// Only try to alias with lower-indexed resources, because we allocate them one-by-one starting from index 0.
		for (unsigned j = 0; j < i; j++)
		{
			if (physical_image_has_history[j])
				continue;

			if (physical_dimensions[i] == physical_dimensions[j])
			{
				// Only alias if the resources are used in the same queue, this way we avoid introducing
				// multi-queue shenanigans. We can only use events to pass aliasing barriers.
				// Also, only alias if we have one single queue.
				bool same_single_queue = physical_dimensions[i].queues == physical_dimensions[j].queues;
				if ((physical_dimensions[i].queues & (physical_dimensions[i].queues - 1)) != 0)
					same_single_queue = false;

				if (pass_range[i].disjoint_lifetime(pass_range[j]) && same_single_queue)
				{
					// We can alias!
					physical_aliases[i] = j;
					if (alias_chains[j].empty())
						alias_chains[j].push_back(j);
					alias_chains[j].push_back(i);

					// We might have different image usage, propagate this information.
					auto merged_image_usage =
							physical_dimensions[j].image_usage |= physical_dimensions[i].image_usage;
					physical_dimensions[i].image_usage = merged_image_usage;
					physical_dimensions[j].image_usage = merged_image_usage;
					break;
				}
			}
		}
	}

	// Now we've found the aliases, so set up the transfer barriers in order of use.
	for (auto &chain : alias_chains)
	{
		if (chain.empty())
			continue;

		sort(begin(chain), end(chain), [&](unsigned a, unsigned b) -> bool {
			return pass_range[a].last_used_pass() < pass_range[b].first_used_pass();
		});

		for (unsigned i = 0; i < chain.size(); i++)
		{
			if (i + 1 < chain.size())
				physical_passes[pass_range[chain[i]].last_used_pass()].alias_transfer.push_back(make_pair(chain[i], chain[i + 1]));
			else
				physical_passes[pass_range[chain[i]].last_used_pass()].alias_transfer.push_back(make_pair(chain[i], chain[0]));
		}
	}
}

bool RenderGraph::need_invalidate(const Barrier &barrier, const PipelineEvent &event)
{
	bool need_invalidate = false;
	Util::for_each_bit(barrier.stages, [&](uint32_t bit) {
		if (barrier.access & ~event.invalidated_in_stage[bit])
			need_invalidate = true;
	});
	return need_invalidate;
}

bool RenderGraph::physical_pass_requires_work(const PhysicalPass &physical_pass) const
{
	for (auto &pass : physical_pass.passes)
		if (passes[pass]->need_render_pass())
			return true;
	return false;
}

void RenderGraph::physical_pass_transfer_ownership(const PhysicalPass &pass)
{
	// Need to wait on this event before we can transfer ownership to another alias.
	for (auto &transfer : pass.alias_transfer)
	{
		auto &phys_events = physical_events[transfer.second];
		phys_events = physical_events[transfer.first];
		for (auto &e : phys_events.invalidated_in_stage)
			e = 0;

		// If we have pending writes, we have a problem. We cannot safely alias unless we first flush caches,
		// but we cannot flush caches from UNDEFINED layout.
		// "Write-only" resources should be transient to begin with, and not hit this path.
		// If required, we could inject a pipeline barrier here which flushes caches.
		// Generally, the last pass a resource is used, it will be *read*, not written to.
		assert(phys_events.to_flush_access == 0);
		phys_events.to_flush_access = 0;
		phys_events.layout = VK_IMAGE_LAYOUT_UNDEFINED;
	}
}

static void get_queue_type(Vulkan::CommandBuffer::Type &queue_type, bool &graphics, RenderGraphQueueFlagBits flag)
{
	switch (flag)
	{
	default:
	case RENDER_GRAPH_QUEUE_GRAPHICS_BIT:
		graphics = true;
		queue_type = Vulkan::CommandBuffer::Type::Generic;
		break;

	case RENDER_GRAPH_QUEUE_COMPUTE_BIT:
		graphics = false;
		queue_type = Vulkan::CommandBuffer::Type::Generic;
		break;

	case RENDER_GRAPH_QUEUE_ASYNC_COMPUTE_BIT:
		graphics = false;
		queue_type = Vulkan::CommandBuffer::Type::AsyncCompute;
		break;

	case RENDER_GRAPH_QUEUE_ASYNC_GRAPHICS_BIT:
		graphics = true;
		queue_type = Vulkan::CommandBuffer::Type::AsyncGraphics;
		break;
	}
}

void RenderGraph::PassSubmissionState::add_unique_event(VkEvent event)
{
	assert(event != VK_NULL_HANDLE);
	auto itr = find(begin(events), end(events), event);
	if (itr == end(events))
		events.push_back(event);
}

void RenderGraph::PassSubmissionState::emit_pre_pass_barriers()
{
	cmd->begin_region("render-graph-sync-pre");

	// Submit barriers.
	if (!semaphore_handover_barriers.empty() || !immediate_image_barriers.empty())
	{
		Util::SmallVector<VkImageMemoryBarrier, 64> combined_barriers;
		combined_barriers.reserve(semaphore_handover_barriers.size() + immediate_image_barriers.size());
		combined_barriers.insert(combined_barriers.end(), semaphore_handover_barriers.begin(), semaphore_handover_barriers.end());
		combined_barriers.insert(combined_barriers.end(), immediate_image_barriers.begin(), immediate_image_barriers.end());

		auto src = handover_stages;
		auto dst = handover_stages | immediate_dst_stages;
		if (!src)
			src = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

		cmd->barrier(src, dst,
		             0, nullptr, 0, nullptr,
		             combined_barriers.size(),
		             combined_barriers.empty() ? nullptr : combined_barriers.data());
	}

	if (!image_barriers.empty() || !buffer_barriers.empty())
	{
		cmd->wait_events(events.size(), events.data(),
		                 src_stages, dst_stages,
		                 0, nullptr,
		                 buffer_barriers.size(), buffer_barriers.empty() ? nullptr : buffer_barriers.data(),
		                 image_barriers.size(), image_barriers.empty() ? nullptr : image_barriers.data());
	}

	cmd->end_region();
}

void RenderGraph::PassSubmissionState::emit_post_pass_barriers()
{
	cmd->begin_region("render-graph-sync-post");
	if (event_signal_stages != 0)
		cmd->complete_signal_event(*signal_event);
	cmd->end_region();
}

static void wait_for_semaphore_in_queue(Vulkan::Device &device_, Vulkan::Semaphore &sem,
                                        Vulkan::CommandBuffer::Type queue_type, VkPipelineStageFlags stages)
{
	if (sem->get_semaphore() != VK_NULL_HANDLE && !sem->is_pending_wait())
		device_.add_wait_semaphore(queue_type, sem, stages, true);
}

void RenderGraph::PassSubmissionState::submit()
{
	if (!cmd)
		return;

	auto &device_ = cmd->get_device();

	size_t num_semaphores = wait_semaphores.size();
	for (size_t i = 0; i < num_semaphores; i++)
		wait_for_semaphore_in_queue(device_, wait_semaphores[i], queue_type, wait_semaphore_stages[i]);

	if (need_submission_semaphore)
	{
		Vulkan::Semaphore semaphores[2];
		device_.submit(cmd, nullptr, 2, semaphores);
		*proxy_semaphores[0] = std::move(*semaphores[0]);
		*proxy_semaphores[1] = std::move(*semaphores[1]);
	}
	else
		device_.submit(cmd);

	if (Vulkan::ImplementationQuirks::get().queue_wait_on_submission)
		device_.flush_frame();
}

void RenderGraph::physical_pass_invalidate_attachments(const PhysicalPass &physical_pass)
{
	// Before invalidating, force the layout to UNDEFINED.
	// This will be required for resource aliasing later.
	// Storage textures are preserved over multiple frames, don't discard.
	for (auto &discard : physical_pass.discards)
		if (!physical_dimensions[discard].is_buffer_like())
			physical_events[discard].layout = VK_IMAGE_LAYOUT_UNDEFINED;
}

void RenderGraph::physical_pass_handle_invalidate_barrier(const Barrier &barrier, PassSubmissionState &state,
                                                          bool physical_graphics_queue)
{

	auto &event = barrier.history ? physical_history_events[barrier.resource_index] :
	              physical_events[barrier.resource_index];

	bool need_event_barrier = false;
	bool layout_change = false;
	bool need_wait_semaphore = false;
	auto &wait_semaphore = physical_graphics_queue ? event.wait_graphics_semaphore : event.wait_compute_semaphore;

	if (physical_dimensions[barrier.resource_index].buffer_info.size)
	{
		// Buffers.
		bool need_sync = (event.to_flush_access != 0) || need_invalidate(barrier, event);

		if (need_sync)
		{
			need_event_barrier = bool(event.event);
			// Signalling and waiting for a semaphore satisfies the memory barrier automatically.
			need_wait_semaphore = bool(wait_semaphore);
		}

		if (need_event_barrier)
		{
			auto &buffer = *physical_buffers[barrier.resource_index];
			VkBufferMemoryBarrier b = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };

			b.srcAccessMask = event.to_flush_access;
			b.dstAccessMask = barrier.access;
			b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			b.buffer = buffer.get_buffer();
			b.offset = 0;
			b.size = VK_WHOLE_SIZE;
			state.buffer_barriers.push_back(b);
		}
	}
	else
	{
		// Images.
		Vulkan::Image *image = barrier.history ?
		                       physical_history_image_attachments[barrier.resource_index].get() :
		                       &physical_attachments[barrier.resource_index]->get_image();

		if (!image)
		{
			// Can happen for history inputs if this is the first frame.
			return;
		}

		VkImageMemoryBarrier b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
		b.oldLayout = event.layout;
		b.newLayout = barrier.layout;
		b.srcAccessMask = event.to_flush_access;
		b.dstAccessMask = barrier.access;

		b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		b.image = image->get_image();
		b.subresourceRange.aspectMask = Vulkan::format_to_aspect_mask(image->get_format());
		b.subresourceRange.layerCount = image->get_create_info().layers;
		b.subresourceRange.levelCount = image->get_create_info().levels;
		event.layout = barrier.layout;

		layout_change = b.oldLayout != b.newLayout;

		bool need_sync =
				layout_change ||
				(event.to_flush_access != 0) ||
				need_invalidate(barrier, event);

		if (need_sync)
		{
			if (event.event)
			{
				// Either we wait for a VkEvent ...
				state.image_barriers.push_back(b);
				need_event_barrier = true;
			}
			else if (wait_semaphore)
			{
				// We wait for a semaphore ...
				if (layout_change)
				{
					// When the semaphore was signalled, caches were flushed, so we don't need to do that again.
					// We still need dstAccessMask however, because layout changes may perform writes.
					b.srcAccessMask = 0;
					state.semaphore_handover_barriers.push_back(b);
					state.handover_stages |= barrier.stages;
				}
				// If we don't need a layout transition, signalling and waiting for semaphores satisfies
				// all requirements we have of srcAccessMask/dstAccessMask.
				need_wait_semaphore = true;
			}
			else
			{
				// ... or vkCmdPipelineBarrier from TOP_OF_PIPE_BIT if this is the first time we use the resource.
				state.immediate_image_barriers.push_back(b);
				if (b.oldLayout != VK_IMAGE_LAYOUT_UNDEFINED)
					throw logic_error("Cannot do immediate image barriers from a layout other than UNDEFINED.");
				state.immediate_dst_stages |= barrier.stages;
			}
		}
	}

	// Any pending writes or layout changes means we have to invalidate caches.
	if (event.to_flush_access || layout_change)
	{
		for (auto &e : event.invalidated_in_stage)
			e = 0;
	}
	event.to_flush_access = 0;

	if (need_event_barrier)
	{
		state.dst_stages |= barrier.stages;

		assert(event.event);
		state.src_stages |= event.event->get_stages();
		state.add_unique_event(event.event->get_event());

		// Mark appropriate caches as invalidated now.
		Util::for_each_bit(barrier.stages, [&](uint32_t bit) {
			event.invalidated_in_stage[bit] |= barrier.access;
		});
	}
	else if (need_wait_semaphore)
	{
		assert(wait_semaphore);

		// Wait for a semaphore, unless it has already been waited for ...
		state.wait_semaphores.push_back(wait_semaphore);
		state.wait_semaphore_stages.push_back(barrier.stages);

		// Waiting for a semaphore makes data visible to all access bits in relevant stages.
		// The exception is if we perform a layout change ...
		// In this case we only invalidate the access bits which we placed in the vkCmdPipelineBarrier.
		Util::for_each_bit(barrier.stages, [&](uint32_t bit) {
			if (layout_change)
				event.invalidated_in_stage[bit] |= barrier.access;
			else
				event.invalidated_in_stage[bit] |= ~0u;
		});
	}
}

void RenderGraph::physical_pass_handle_signal(Vulkan::Device &device_, const PhysicalPass &physical_pass, PassSubmissionState &state)
{
	for (auto &barrier : physical_pass.flush)
	{
		if (physical_dimensions[barrier.resource_index].uses_semaphore())
			state.need_submission_semaphore = true;
		else
			state.event_signal_stages |= barrier.stages;
	}

	if (state.event_signal_stages)
		state.signal_event = device_.begin_signal_event(state.event_signal_stages);

	if (state.need_submission_semaphore)
	{
		state.proxy_semaphores[0] = device_.request_proxy_semaphore();
		state.proxy_semaphores[1] = device_.request_proxy_semaphore();
	}
}

void RenderGraph::physical_pass_handle_flush_barrier(const Barrier &barrier, PassSubmissionState &state)
{
	auto &event = barrier.history ?
	              physical_history_events[barrier.resource_index] :
	              physical_events[barrier.resource_index];

	// A render pass might have changed the final layout.
	if (!physical_dimensions[barrier.resource_index].buffer_info.size)
	{
		auto *image = barrier.history ?
		              physical_history_image_attachments[barrier.resource_index].get() :
		              &physical_attachments[barrier.resource_index]->get_image();

		if (!image)
			return;

		physical_events[barrier.resource_index].layout = barrier.layout;
	}

	// Mark if there are pending writes from this pass.
	event.to_flush_access = barrier.access;

	if (physical_dimensions[barrier.resource_index].uses_semaphore())
	{
		assert(state.proxy_semaphores[0]);
		assert(state.proxy_semaphores[1]);
		event.wait_graphics_semaphore = state.proxy_semaphores[0];
		event.wait_compute_semaphore = state.proxy_semaphores[1];
	}
	else
	{
		assert(state.signal_event);
		event.event = state.signal_event;
	}
}

void RenderGraph::physical_pass_enqueue_graphics_commands(const PhysicalPass &physical_pass, PassSubmissionState &state)
{
	auto &cmd = *state.cmd;
	for (auto &clear_req : physical_pass.color_clear_requests)
		clear_req.pass->get_clear_color(clear_req.index, clear_req.target);

	if (physical_pass.depth_clear_request.pass)
	{
		physical_pass.depth_clear_request.pass->get_clear_depth_stencil(
				physical_pass.depth_clear_request.target);
	}

	Vulkan::QueryPoolHandle start_vertex, start_fragment, end_vertex, end_fragment;
	if (enabled_timestamps)
	{
		start_vertex = cmd.write_timestamp(VK_PIPELINE_STAGE_VERTEX_SHADER_BIT);
		start_fragment = cmd.write_timestamp(VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
	}

	VK_ASSERT(physical_pass.layers != ~0u);

	auto rp_info = physical_pass.render_pass_info;
	unsigned layer_iterations = 1;

	if (physical_pass.layers > 1)
	{
		unsigned multiview_count = 0;
		unsigned separate_count = 0;

		for (auto pass : physical_pass.passes)
		{
			auto &subpass = passes[pass];
			if (subpass->render_pass_is_multiview())
				multiview_count++;
			else
				separate_count++;
		}

		if (multiview_count && separate_count)
		{
			LOGE("Mismatch in physical pass w.r.t. multiview vs separate layers. Do not mix and match! Render pass will be dropped.\n");
			layer_iterations = 0;
		}
		else if (multiview_count)
		{
			if (device->get_device_features().multiview_features.multiview)
			{
				rp_info.num_layers = physical_pass.layers;
				rp_info.base_layer = 0;
			}
			else
			{
				LOGE("VK_KHR_multiview is not supported on this device. Falling back to separate layering.\n");
				layer_iterations = physical_pass.layers;
			}
		}
		else
		{
			layer_iterations = physical_pass.layers;
		}
	}

	for (unsigned layer = 0; layer < layer_iterations; layer++)
	{
		rp_info.base_layer = layer;
		cmd.begin_region("begin-render-pass");
		cmd.begin_render_pass(rp_info, state.subpass_contents[0]);
		cmd.end_region();

		for (auto &subpass : physical_pass.passes)
		{
			auto subpass_index = unsigned(&subpass - physical_pass.passes.data());
			auto &scaled_requests = physical_pass.scaled_clear_requests[subpass_index];
			enqueue_scaled_requests(cmd, scaled_requests);

			auto &pass = *passes[subpass];

			// If we have started the render pass, we have to do it, even if a lone subpass might not be required,
			// due to clearing and so on.
			// This should be an extremely unlikely scenario.
			// Either you need all subpasses or none.
			cmd.begin_region(pass.get_name().c_str());
			pass.build_render_pass(cmd, layer);
			cmd.end_region();

			if (&subpass != &physical_pass.passes.back())
				cmd.next_subpass(state.subpass_contents[subpass_index + 1]);
		}

		cmd.begin_region("end-render-pass");
		cmd.end_render_pass();
		cmd.end_region();
	}

	if (enabled_timestamps)
	{
		end_vertex = cmd.write_timestamp(VK_PIPELINE_STAGE_VERTEX_SHADER_BIT);
		end_fragment = cmd.write_timestamp(VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
		string name;
		if (physical_pass.passes.size() == 1)
			name = passes[physical_pass.passes.front()]->get_name();
		else
		{
			for (auto &pass : physical_pass.passes)
			{
				name += passes[pass]->get_name();
				if (&pass != &physical_pass.passes.back())
					name += " + ";
			}
		}
		device->register_time_interval("geometry", std::move(start_vertex), std::move(end_vertex), name.c_str());
		device->register_time_interval("fragment", std::move(start_fragment), std::move(end_fragment), name.c_str());
	}
	enqueue_mipmap_requests(cmd, physical_pass.mipmap_requests);
}

void RenderGraph::physical_pass_enqueue_compute_commands(const PhysicalPass &physical_pass, PassSubmissionState &state)
{
	assert(physical_pass.passes.size() == 1);

	auto &cmd = *state.cmd;
	auto &pass = *passes[physical_pass.passes.front()];
	Vulkan::QueryPoolHandle start_ts, end_ts;
	if (enabled_timestamps)
		start_ts = cmd.write_timestamp(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
	cmd.begin_region(pass.get_name().c_str());
	pass.build_render_pass(cmd, 0);
	cmd.end_region();
	if (enabled_timestamps)
	{
		end_ts = cmd.write_timestamp(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
		device->register_time_interval("compute", std::move(start_ts), std::move(end_ts), pass.get_name());
	}
}

void RenderGraph::physical_pass_handle_cpu_timeline(Vulkan::Device &device_,
                                                    const PhysicalPass &physical_pass,
                                                    PassSubmissionState &state,
                                                    TaskComposer &incoming_composer)
{
	get_queue_type(state.queue_type, state.graphics, passes[physical_pass.passes.front()]->get_queue());

	physical_pass_invalidate_attachments(physical_pass);

	// Queue up invalidates and change layouts.
	for (auto &barrier : physical_pass.invalidate)
	{
		bool physical_graphics =
				device->get_physical_queue_type(state.queue_type) == Vulkan::CommandBuffer::Type::Generic;
		physical_pass_handle_invalidate_barrier(barrier, state, physical_graphics);
	}

	physical_pass_handle_signal(device_, physical_pass, state);
	for (auto &barrier : physical_pass.flush)
		physical_pass_handle_flush_barrier(barrier, state);

	// Hand over aliases to some future pass.
	physical_pass_transfer_ownership(physical_pass);

	// Create preparation tasks.
	state.subpass_contents.resize(physical_pass.passes.size());
	for (auto &c : state.subpass_contents)
		c = VK_SUBPASS_CONTENTS_INLINE;

	auto &group = incoming_composer.get_thread_group();
	TaskComposer composer(group);
	composer.set_incoming_task(incoming_composer.get_pipeline_stage_dependency());
	composer.begin_pipeline_stage();

	unsigned subpass_index = 0;
	for (auto &pass : physical_pass.passes)
	{
		auto &subpass = *passes[pass];
		subpass.enqueue_prepare_render_pass(composer, physical_pass.render_pass_info,
		                                    subpass_index, state.subpass_contents[subpass_index]);
		subpass_index++;
	}

	state.rendering_dependency = composer.get_outgoing_task();
}

void RenderGraph::physical_pass_handle_gpu_timeline(ThreadGroup &group, Vulkan::Device &device_,
                                                    const PhysicalPass &physical_pass,
                                                    PassSubmissionState &state)
{
	auto task = group.create_task([&]() {
		state.cmd = device_.request_command_buffer(state.queue_type);
		state.emit_pre_pass_barriers();

		if (state.graphics)
			physical_pass_enqueue_graphics_commands(physical_pass, state);
		else
			physical_pass_enqueue_compute_commands(physical_pass, state);

		state.emit_post_pass_barriers();

		// Explicitly end in the thread since we would break threading rules otherwise if we record and End
		// in the submission task.
		state.cmd->end_debug_channel();
		state.cmd->end_threaded_recording();
	});

	task->set_desc((passes[physical_pass.passes.front()]->get_name() + "-build-gpu-commands").c_str());
	if (state.rendering_dependency)
		group.add_dependency(*task, *state.rendering_dependency);
	state.rendering_dependency = task;
}

void RenderGraph::enqueue_render_pass(Vulkan::Device &device_, PhysicalPass &physical_pass, PassSubmissionState &state,
                                      TaskComposer &composer)
{
	if (!physical_pass_requires_work(physical_pass))
	{
		physical_pass_transfer_ownership(physical_pass);
		return;
	}

	state.active = true;

	// Runs serially on CPU resolve barrier states.
	physical_pass_handle_cpu_timeline(device_, physical_pass, state, composer);
}

void RenderGraph::enqueue_swapchain_scale_pass(Vulkan::Device &device_)
{
	unsigned resource_index = resource_to_index[backbuffer_source];
	auto &source_resource = *this->resources[resource_index];

	auto queue_type = (physical_dimensions[resource_index].queues & RENDER_GRAPH_QUEUE_GRAPHICS_BIT) != 0 ?
	                  Vulkan::CommandBuffer::Type::Generic : Vulkan::CommandBuffer::Type::AsyncGraphics;

	auto physical_queue_type = device_.get_physical_queue_type(queue_type);

	auto cmd = device_.request_command_buffer(queue_type);
	cmd->begin_region("render-graph-copy-to-swapchain");

	unsigned index = source_resource.get_physical_index();
	auto &image = physical_attachments[index]->get_image();

	auto &wait_semaphore = physical_queue_type == Vulkan::CommandBuffer::Type::Generic ?
	                       physical_events[index].wait_graphics_semaphore : physical_events[index].wait_compute_semaphore;

	auto target_layout = physical_dimensions[index].is_storage_image() ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	if (physical_events[index].event)
	{
		VkEvent event = physical_events[index].event->get_event();
		VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
		barrier.image = image.get_image();
		barrier.oldLayout = physical_events[index].layout;

		barrier.newLayout = target_layout;
		barrier.srcAccessMask = physical_events[index].to_flush_access;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.subresourceRange.levelCount = image.get_create_info().levels;
		barrier.subresourceRange.layerCount = image.get_create_info().layers;
		barrier.subresourceRange.aspectMask = Vulkan::format_to_aspect_mask(physical_attachments[index]->get_format());

		cmd->wait_events(1, &event,
		                 physical_events[index].event->get_stages(),
		                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		                 0, nullptr,
		                 0, nullptr,
		                 1, &barrier);

		physical_events[index].layout = target_layout;
	}
	else if (wait_semaphore)
	{
		if (wait_semaphore->get_semaphore() != VK_NULL_HANDLE &&
		    !wait_semaphore->is_pending_wait())
		{
			device_.add_wait_semaphore(queue_type,
			                           wait_semaphore,
			                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, true);
		}

		if (physical_events[index].layout != target_layout)
		{
			cmd->image_barrier(image, physical_events[index].layout, target_layout,
			                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
			                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
			physical_events[index].layout = target_layout;
		}
	}
	else
	{
		throw logic_error("Swapchain resource was not written to.");
	}

	Vulkan::RenderPassInfo rp_info;
	rp_info.num_color_attachments = 1;
	rp_info.clear_attachments = 0;
	rp_info.store_attachments = 1;
	rp_info.color_attachments[0] = swapchain_attachment;

	cmd->begin_render_pass(rp_info);
	enqueue_scaled_requests(*cmd, {{ 0, index }});
	cmd->end_render_pass();

	// Set a write-after-read barrier on this resource.
	physical_events[index].to_flush_access = 0;
	for (auto &e : physical_events[index].invalidated_in_stage)
		e = 0;
	physical_events[index].invalidated_in_stage[trailing_zeroes(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)] = VK_ACCESS_SHADER_READ_BIT;

	cmd->end_region();
	if (physical_dimensions[index].uses_semaphore())
	{
		Vulkan::Semaphore semaphores[2];
		device_.submit(cmd, nullptr, 2, semaphores);
		physical_events[index].wait_graphics_semaphore = semaphores[0];
		physical_events[index].wait_compute_semaphore = semaphores[1];
	}
	else
	{
		physical_events[index].event = cmd->signal_event(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
		device_.submit(cmd);
	}

	if (Vulkan::ImplementationQuirks::get().queue_wait_on_submission)
		device_.flush_frame();
}

void RenderGraph::enqueue_render_passes(Vulkan::Device &device_, TaskComposer &composer)
{
	pass_submission_state.clear();
	size_t count = physical_passes.size();
	pass_submission_state.resize(count);
	auto &thread_group = composer.get_thread_group();

	for (size_t i = 0; i < count; i++)
		enqueue_render_pass(device_, physical_passes[i], pass_submission_state[i], composer);

	for (size_t i = 0; i < count; i++)
	{
		// Could be run in parallel.
		if (pass_submission_state[i].active)
			physical_pass_handle_gpu_timeline(thread_group, device_, physical_passes[i], pass_submission_state[i]);
	}

	for (auto &state : pass_submission_state)
	{
		auto &group = composer.begin_pipeline_stage();
		group.set_desc("render-graph-submit");
		if (state.rendering_dependency)
		{
			thread_group.add_dependency(group, *state.rendering_dependency);
			state.rendering_dependency.reset();
		}

		group.enqueue_task([&state]() {
			state.submit();
		});
	}

	// Scale to swapchain if needed.
	if (swapchain_physical_index == RenderResource::Unused)
	{
		auto &group = composer.begin_pipeline_stage();
		group.set_desc("render-queue-swapchain-scale");
		group.enqueue_task([this, &device_]() {
			enqueue_swapchain_scale_pass(device_);
			device_.flush_frame();
		});
	}
	else
	{
		auto &group = composer.begin_pipeline_stage();
		group.set_desc("render-queue-flush");
		group.enqueue_task([&device_]() {
			device_.flush_frame();
		});
	}
}

void RenderGraph::setup_physical_buffer(Vulkan::Device &device_, unsigned attachment)
{
	auto &att = physical_dimensions[attachment];

	Vulkan::BufferCreateInfo info = {};
	info.size = att.buffer_info.size;
	info.usage = att.buffer_info.usage;
	info.domain = Vulkan::BufferDomain::Device;

	// Zero-initialize buffers. TODO: Make this configurable.
	info.misc = Vulkan::BUFFER_MISC_ZERO_INITIALIZE_BIT;

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
		physical_buffers[attachment] = device_.create_buffer(info, nullptr);
		device_.set_name(*physical_buffers[attachment], att.name.c_str());
		physical_events[attachment] = {};
	}
}

void RenderGraph::setup_physical_image(Vulkan::Device &device_, unsigned attachment)
{
	auto &att = physical_dimensions[attachment];

	if (physical_aliases[attachment] != RenderResource::Unused)
	{
		physical_image_attachments[attachment] = physical_image_attachments[physical_aliases[attachment]];
		physical_attachments[attachment] = &physical_image_attachments[attachment]->get_view();
		physical_events[attachment] = {};
		return;
	}

	bool need_image = true;
	VkImageUsageFlags usage = att.image_usage;
	Vulkan::ImageMiscFlags misc = 0;
	VkImageCreateFlags flags = 0;

	if (att.unorm_srgb)
		misc |= Vulkan::IMAGE_MISC_MUTABLE_SRGB_BIT;
	if (att.is_storage_image())
		flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;

	if (physical_image_attachments[attachment])
	{
		if (att.persistent &&
		    physical_image_attachments[attachment]->get_create_info().format == att.format &&
		    physical_image_attachments[attachment]->get_create_info().width == att.width &&
		    physical_image_attachments[attachment]->get_create_info().height == att.height &&
		    physical_image_attachments[attachment]->get_create_info().depth == att.depth &&
		    physical_image_attachments[attachment]->get_create_info().samples == att.samples &&
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
		info.type = att.depth > 1 ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
		info.width = att.width;
		info.height = att.height;
		info.depth = att.depth;
		info.domain = Vulkan::ImageDomain::Physical;
		info.levels = att.levels;
		info.layers = att.layers;
		info.usage = usage;
		info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		info.samples = static_cast<VkSampleCountFlagBits>(att.samples);
		info.flags = flags;

		if (Vulkan::format_has_depth_or_stencil_aspect(info.format))
			info.usage &= ~VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

		info.misc = misc;
		if (att.queues & (RENDER_GRAPH_QUEUE_GRAPHICS_BIT | RENDER_GRAPH_QUEUE_COMPUTE_BIT))
			info.misc |= Vulkan::IMAGE_MISC_CONCURRENT_QUEUE_GRAPHICS_BIT;
		if (att.queues & RENDER_GRAPH_QUEUE_ASYNC_COMPUTE_BIT)
			info.misc |= Vulkan::IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_COMPUTE_BIT;
		if (att.queues & RENDER_GRAPH_QUEUE_ASYNC_GRAPHICS_BIT)
			info.misc |= Vulkan::IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_GRAPHICS_BIT;

		physical_image_attachments[attachment] = device_.create_image(info, nullptr);
		physical_image_attachments[attachment]->set_surface_transform(att.transform);

		// Just keep storage images in GENERAL layout.
		// There is no reason to try enabling compression.
		if (!physical_image_attachments[attachment])
			LOGE("Failed to create render graph image!\n");
		if (att.is_storage_image())
			physical_image_attachments[attachment]->set_layout(Vulkan::Layout::General);
		device_.set_name(*physical_image_attachments[attachment], att.name.c_str());
		physical_events[attachment] = {};
	}

	physical_attachments[attachment] = &physical_image_attachments[attachment]->get_view();
}

void RenderGraph::setup_attachments(Vulkan::Device &device_, Vulkan::ImageView *swapchain)
{
	physical_attachments.clear();
	physical_attachments.resize(physical_dimensions.size());

	// Try to reuse the buffers if possible.
	physical_buffers.resize(physical_dimensions.size());

	// Try to reuse render targets if possible.
	physical_image_attachments.resize(physical_dimensions.size());
	physical_history_image_attachments.resize(physical_dimensions.size());
	physical_events.resize(physical_dimensions.size());
	physical_history_events.resize(physical_dimensions.size());

	swapchain_attachment = swapchain;

	unsigned num_attachments = physical_dimensions.size();
	for (unsigned i = 0; i < num_attachments; i++)
	{
		// Move over history attachments and events.
		if (physical_image_has_history[i])
		{
			swap(physical_history_image_attachments[i], physical_image_attachments[i]);
			swap(physical_history_events[i], physical_events[i]);
		}

		auto &att = physical_dimensions[i];
		if (att.buffer_info.size != 0)
		{
			setup_physical_buffer(device_, i);
		}
		else
		{
			if (att.is_storage_image())
				setup_physical_image(device_, i);
			else if (i == swapchain_physical_index)
				physical_attachments[i] = swapchain;
			else if (att.transient)
				physical_attachments[i] = &device_.get_transient_attachment(att.width, att.height, att.format, i, att.samples, att.layers);
			else
				setup_physical_image(device_, i);
		}
	}

	// Assign concrete ImageViews to the render pass.
	for (auto &physical_pass : physical_passes)
	{
		unsigned layers = ~0u;

		unsigned num_color_attachments = physical_pass.physical_color_attachments.size();
		for (unsigned i = 0; i < num_color_attachments; i++)
		{
			auto &att = physical_pass.render_pass_info.color_attachments[i];
			att = physical_attachments[physical_pass.physical_color_attachments[i]];
			if (att->get_image().get_create_info().domain == Vulkan::ImageDomain::Physical)
				layers = std::min(layers, att->get_image().get_create_info().layers);
		}

		if (physical_pass.physical_depth_stencil_attachment != RenderResource::Unused)
		{
			auto &ds = physical_pass.render_pass_info.depth_stencil;
			ds = physical_attachments[physical_pass.physical_depth_stencil_attachment];
			if (ds->get_image().get_create_info().domain == Vulkan::ImageDomain::Physical)
				layers = std::min(layers, ds->get_image().get_create_info().layers);
		}
		else
			physical_pass.render_pass_info.depth_stencil = nullptr;

		physical_pass.layers = layers;
	}
}

void RenderGraph::traverse_dependencies(const RenderPass &pass, unsigned stack_count)
{
	// For these kinds of resources,
	// make sure that we pull in the dependency right away so we can merge render passes if possible.
	if (pass.get_depth_stencil_input())
	{
		depend_passes_recursive(pass, pass.get_depth_stencil_input()->get_write_passes(),
		                        stack_count, false, false, true);
	}

	for (auto *input : pass.get_attachment_inputs())
	{
		bool self_dependency = pass.get_depth_stencil_output() == input;
		if (find(begin(pass.get_color_outputs()), end(pass.get_color_outputs()), input) != end(pass.get_color_outputs()))
			self_dependency = true;

		if (!self_dependency)
			depend_passes_recursive(pass, input->get_write_passes(), stack_count, false, false, true);
	}

	for (auto *input : pass.get_color_inputs())
	{
		if (input)
			depend_passes_recursive(pass, input->get_write_passes(), stack_count, false, false, true);
	}

	for (auto *input : pass.get_color_scale_inputs())
	{
		if (input)
			depend_passes_recursive(pass, input->get_write_passes(), stack_count, false, false, false);
	}

	for (auto *input : pass.get_blit_texture_inputs())
	{
		if (input)
			depend_passes_recursive(pass, input->get_write_passes(), stack_count, false, false, false);
	}

	for (auto &input : pass.get_generic_texture_inputs())
		depend_passes_recursive(pass, input.texture->get_write_passes(), stack_count, false, false, false);

	for (auto *input : pass.get_storage_inputs())
	{
		if (input)
		{
			// There might be no writers of this resource if it's used in a feedback fashion.
			depend_passes_recursive(pass, input->get_write_passes(), stack_count, true, false, false);
			// Deal with write-after-read hazards if a storage buffer is read in other passes
			// (feedback) before being updated.
			depend_passes_recursive(pass, input->get_read_passes(), stack_count, true, true, false);
		}
	}

	for (auto *input : pass.get_storage_texture_inputs())
	{
		if (input)
			depend_passes_recursive(pass, input->get_write_passes(), stack_count, false, false, false);
	}

	for (auto &input : pass.get_generic_buffer_inputs())
	{
		// There might be no writers of this resource if it's used in a feedback fashion.
		depend_passes_recursive(pass, input.buffer->get_write_passes(), stack_count, true, false, false);
	}
}

void RenderGraph::depend_passes_recursive(const RenderPass &self, const std::unordered_set<unsigned> &written_passes,
                                          unsigned stack_count, bool no_check, bool ignore_self, bool merge_dependency)
{
	if (!no_check && written_passes.empty())
		throw logic_error("No pass exists which writes to resource.");

	if (stack_count > passes.size())
		throw logic_error("Cycle detected.");

	for (auto &pass : written_passes)
		if (pass != self.get_index())
			pass_dependencies[self.get_index()].insert(pass);

	if (merge_dependency)
		for (auto &pass : written_passes)
			if (pass != self.get_index())
				pass_merge_dependencies[self.get_index()].insert(pass);

	stack_count++;

	for (auto &pushed_pass : written_passes)
	{
		if (ignore_self && pushed_pass == self.get_index())
			continue;
		else if (pushed_pass == self.get_index())
			throw logic_error("Pass depends on itself.");

		pass_stack.push_back(pushed_pass);
		auto &pass = *passes[pushed_pass];
		traverse_dependencies(pass, stack_count);
	}
}

void RenderGraph::reorder_passes(std::vector<unsigned> &flattened_passes)
{
	// If a pass depends on an earlier pass via merge dependencies,
	// copy over dependencies to the dependees to avoid cases which can break subpass merging.
	// This is a "soft" dependency. If we ignore it, it's not a real problem.
	for (auto &pass_merge_deps : pass_merge_dependencies)
	{
		auto pass_index = unsigned(&pass_merge_deps - pass_merge_dependencies.data());
		auto &pass_deps = pass_dependencies[pass_index];

		for (auto &merge_dep : pass_merge_deps)
		{
			for (auto &dependee : pass_deps)
			{
				// Avoid cycles.
				if (depends_on_pass(dependee, merge_dep))
					continue;

				if (merge_dep != dependee)
					pass_dependencies[merge_dep].insert(dependee);
			}
		}
	}

	// TODO: This is very inefficient, but should work okay for a reasonable amount of passes ...
	// But, reasonable amounts are always one more than what you'd think ...
	// Clarity in the algorithm is pretty important, because these things tend to be very annoying to debug.

	if (flattened_passes.size() <= 2)
		return;

	vector<unsigned> unscheduled_passes;
	unscheduled_passes.reserve(passes.size());
	swap(flattened_passes, unscheduled_passes);

	const auto schedule = [&](unsigned index) {
		// Need to preserve the order of remaining elements.
		flattened_passes.push_back(unscheduled_passes[index]);
		move(unscheduled_passes.begin() + index + 1,
		     unscheduled_passes.end(),
		     unscheduled_passes.begin() + index);
		unscheduled_passes.pop_back();
	};

	schedule(0);
	while (!unscheduled_passes.empty())
	{
		// Find the next pass to schedule.
		// We can pick any pass N, if the pass does not depend on anything left in unscheduled_passes.
		// unscheduled_passes[0] is always okay as a fallback, so unless we find something better,
		// we will at least pick that.

		// Ideally, we pick a pass which does not introduce any hard barrier.
		// A "hard barrier" here is where a pass depends directly on the pass before it forcing something ala vkCmdPipelineBarrier,
		// we would like to avoid this if possible.

		// Find the pass which has the optimal overlap factor which means the number of passes can be scheduled in-between
		// the depender, and the dependee.

		unsigned best_candidate = 0;
		unsigned best_overlap_factor = 0;

		for (unsigned i = 0; i < unscheduled_passes.size(); i++)
		{
			unsigned overlap_factor = 0;

			// Always try to merge passes if possible on tilers.
			// This might not make sense on desktop however,
			// so we can conditionally enable this path depending on our GPU.
			if (pass_merge_dependencies[unscheduled_passes[i]].count(flattened_passes.back()))
			{
				overlap_factor = ~0u;
			}
			else
			{
				for (auto itr = flattened_passes.rbegin(); itr != flattened_passes.rend(); ++itr)
				{
					if (depends_on_pass(unscheduled_passes[i], *itr))
						break;
					overlap_factor++;
				}
			}

			if (overlap_factor <= best_overlap_factor)
				continue;

			bool possible_candidate = true;
			for (unsigned j = 0; j < i; j++)
			{
				if (depends_on_pass(unscheduled_passes[i], unscheduled_passes[j]))
				{
					possible_candidate = false;
					break;
				}
			}

			if (!possible_candidate)
				continue;

			best_candidate = i;
			best_overlap_factor = overlap_factor;
		}

		schedule(best_candidate);
	}
}

bool RenderGraph::depends_on_pass(unsigned dst_pass, unsigned src_pass)
{
	if (dst_pass == src_pass)
		return true;

	for (auto &dep : pass_dependencies[dst_pass])
	{
		if (depends_on_pass(dep, src_pass))
			return true;
	}

	return false;
}

void RenderGraph::bake()
{
	// First, validate that the graph is sane.
	validate_passes();

	auto itr = resource_to_index.find(backbuffer_source);
	if (itr == end(resource_to_index))
		throw logic_error("Backbuffer source does not exist.");

	pass_stack.clear();

	pass_dependencies.clear();
	pass_merge_dependencies.clear();
	pass_dependencies.resize(passes.size());
	pass_merge_dependencies.resize(passes.size());

	// Work our way back from the backbuffer, and sort out all the dependencies.
	auto &backbuffer_resource = *resources[itr->second];

	if (backbuffer_resource.get_write_passes().empty())
		throw logic_error("No pass exists which writes to resource.");

	for (auto &pass : backbuffer_resource.get_write_passes())
		pass_stack.push_back(pass);

	auto tmp_pass_stack = pass_stack;
	for (auto &pushed_pass : tmp_pass_stack)
	{
		auto &pass = *passes[pushed_pass];
		traverse_dependencies(pass, 0);
	}

	reverse(begin(pass_stack), end(pass_stack));
	filter_passes(pass_stack);

	// Now, reorder passes to extract better pipelining.
	reorder_passes(pass_stack);

	// Now, we have a linear list of passes to submit in-order which would obey the dependencies.

	// Figure out which physical resources we need. Here we will alias resources which can trivially alias via renaming.
	// E.g. depth input -> depth output is just one physical attachment, similar with color.
	build_physical_resources();

	// Next, try to merge adjacent passes together.
	build_physical_passes();

	// After merging physical passes and resources, if an image resource is only used in a single physical pass, make it transient.
	build_transients();

	// Now that we are done, we can make render passes.
	build_render_pass_info();

	// For each render pass in isolation, figure out the barriers required.
	build_barriers();

	// Check if the swapchain needs to be blitted to in case the geometry does not match the backbuffer,
	// or the usage of the image makes that impossible.
	swapchain_physical_index = resources[resource_to_index[backbuffer_source]]->get_physical_index();

	auto &backbuffer_dim = physical_dimensions[swapchain_physical_index];

	// If resource is touched in async-compute, we cannot alias with swapchain.
	// If resource is not transient, it's being used in multiple physical passes,
	// we can't use the implicit subpass dependencies for dealing with swapchain.
	bool can_alias_backbuffer = (backbuffer_dim.queues & compute_queues) == 0 &&
	                            backbuffer_dim.transient;

	// Resources which do not alias with the backbuffer should not be pre-rotated.
	for (auto &dim : physical_dimensions)
		if (&dim != &backbuffer_dim)
			dim.transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;

	LOGI("Backbuffer transform: %u\n", backbuffer_dim.transform);
	if (Vulkan::surface_transform_swaps_xy(backbuffer_dim.transform))
		std::swap(backbuffer_dim.width, backbuffer_dim.height);

	backbuffer_dim.transient = false;
	backbuffer_dim.persistent = swapchain_dimensions.persistent;
	if (!can_alias_backbuffer || backbuffer_dim != swapchain_dimensions)
	{
		LOGW("Cannot alias with backbuffer, requires extra blit pass!\n");
		LOGW("  Backbuffer: %u x %u, fmt: %u, transform: %u\n",
		     backbuffer_dim.width, backbuffer_dim.height,
		     backbuffer_dim.format, backbuffer_dim.transform);
		LOGW("  Swapchain: %u x %u, fmt: %u, transform: %u\n",
		     swapchain_dimensions.width, swapchain_dimensions.height,
		     swapchain_dimensions.format, swapchain_dimensions.transform);

		swapchain_physical_index = RenderResource::Unused;
		if ((backbuffer_dim.queues & RENDER_GRAPH_QUEUE_GRAPHICS_BIT) == 0)
			backbuffer_dim.queues |= RENDER_GRAPH_QUEUE_ASYNC_GRAPHICS_BIT;
		else
			backbuffer_dim.queues |= RENDER_GRAPH_QUEUE_GRAPHICS_BIT;

		// We will need to sample from the image to blit to backbuffer.
		backbuffer_dim.image_usage |= VK_IMAGE_USAGE_SAMPLED_BIT;

		// Don't use pre-transform if we can't alias anyways.
		if (Vulkan::surface_transform_swaps_xy(backbuffer_dim.transform))
			std::swap(backbuffer_dim.width, backbuffer_dim.height);
		backbuffer_dim.transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	}
	else
		physical_dimensions[swapchain_physical_index].transient = true;

	// Based on our render graph, figure out the barriers we actually need.
	// Some barriers are implicit (transients), and some are redundant, i.e. same texture read in multiple passes.
	build_physical_barriers();

	// Figure out which images can alias with each other.
	// Also build virtual "transfer" barriers. These things only copy events over to other physical resources.
	build_aliases();
}

ResourceDimensions RenderGraph::get_resource_dimensions(const RenderBufferResource &resource) const
{
	ResourceDimensions dim;
	auto &info = resource.get_buffer_info();
	dim.buffer_info = info;
	dim.buffer_info.usage |= resource.get_buffer_usage();
	dim.persistent = info.persistent;
	dim.name = resource.get_name();
	return dim;
}

ResourceDimensions RenderGraph::get_resource_dimensions(const RenderTextureResource &resource) const
{
	ResourceDimensions dim;
	auto &info = resource.get_attachment_info();
	dim.layers = info.layers;
	dim.samples = info.samples;
	dim.format = info.format;
	dim.transient = resource.get_transient_state();
	dim.persistent = info.persistent;
	dim.unorm_srgb = info.unorm_srgb_alias;
	dim.queues = resource.get_used_queues();
	dim.image_usage = info.aux_usage | resource.get_image_usage();
	dim.name = resource.get_name();

	// Mark the resource as potentially supporting pre-rotate.
	// If this resource ends up aliasing with the swapchain, it might go through.
	if (info.supports_prerotate)
		dim.transform = swapchain_dimensions.transform;

	switch (info.size_class)
	{
	case SizeClass::SwapchainRelative:
		dim.width = std::max(unsigned(muglm::ceil(info.size_x * swapchain_dimensions.width)), 1u);
		dim.height = std::max(unsigned(muglm::ceil(info.size_y * swapchain_dimensions.height)), 1u);
		dim.depth = std::max(unsigned(muglm::ceil(info.size_z)), 1u);
		if (Vulkan::surface_transform_swaps_xy(swapchain_dimensions.transform))
			std::swap(dim.width, dim.height);
		break;

	case SizeClass::Absolute:
		dim.width = std::max(unsigned(info.size_x), 1u);
		dim.height = std::max(unsigned(info.size_y), 1u);
		dim.depth = std::max(unsigned(info.size_z), 1u);
		break;

	case SizeClass::InputRelative:
	{
		auto itr = resource_to_index.find(info.size_relative_name);
		if (itr == end(resource_to_index))
			throw logic_error("Resource does not exist.");
		auto &input = static_cast<RenderTextureResource &>(*resources[itr->second]);
		auto input_dim = get_resource_dimensions(input);

		dim.width = std::max(unsigned(muglm::ceil(input_dim.width * info.size_x)), 1u);
		dim.height = std::max(unsigned(muglm::ceil(input_dim.height * info.size_y)), 1u);
		dim.depth = std::max(unsigned(muglm::ceil(input_dim.depth * info.size_z)), 1u);
		break;
	}
	}

	if (dim.format == VK_FORMAT_UNDEFINED)
		dim.format = swapchain_dimensions.format;

	const auto num_levels = [](unsigned width, unsigned height, unsigned depth) -> unsigned {
		unsigned levels = 0;
		unsigned max_dim = std::max(std::max(width, height), depth);
		while (max_dim)
		{
			levels++;
			max_dim >>= 1;
		}
		return levels;
	};

	dim.levels = std::min(num_levels(dim.width, dim.height, dim.depth), info.levels == 0 ? ~0u : info.levels);
	return dim;
}

void RenderGraph::build_physical_barriers()
{
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
		VkAccessFlags invalidated_types = 0;
		VkAccessFlags flushed_types = 0;

		VkPipelineStageFlags invalidated_stages = 0;
		VkPipelineStageFlags flushed_stages = 0;
	};

	// To handle state inside a physical pass.
	vector<ResourceState> resource_state;
	resource_state.reserve(physical_dimensions.size());

	for (auto &physical_pass : physical_passes)
	{
		resource_state.clear();
		resource_state.resize(physical_dimensions.size());

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
				auto &res = resource_state[invalidate.resource_index];

				// Transients and swapchain images are handled implicitly.
				if (physical_dimensions[invalidate.resource_index].transient ||
					invalidate.resource_index == swapchain_physical_index)
				{
					continue;
				}

				if (invalidate.history)
				{
					auto itr = find_if(begin(physical_pass.invalidate), end(physical_pass.invalidate), [&](const Barrier &b) -> bool {
						return b.resource_index == invalidate.resource_index && b.history;
					});

					if (itr == end(physical_pass.invalidate))
					{
						// Storage images should just be in GENERAL all the time instead of SHADER_READ_ONLY_OPTIMAL.
						auto layout = physical_dimensions[invalidate.resource_index].is_storage_image() ?
						              VK_IMAGE_LAYOUT_GENERAL :
						              invalidate.layout;

						// Special case history barriers. They are a bit different from other barriers.
						// We just need to ensure the layout is right and that we avoid write-after-read.
						// Even if we see these barriers in multiple render passes, they will not emit multiple barriers.
						physical_pass.invalidate.push_back(
							{ invalidate.resource_index, layout, invalidate.access, invalidate.stages, true });
						physical_pass.flush.push_back(
							{ invalidate.resource_index, layout, 0, invalidate.stages, true });
					}

					continue;
				}

				// Only the first use of a resource in a physical pass needs to be handled externally.
				if (res.initial_layout == VK_IMAGE_LAYOUT_UNDEFINED)
				{
					res.invalidated_types |= invalidate.access;
					res.invalidated_stages |= invalidate.stages;

					// Storage images should just be in GENERAL all the time instead of SHADER_READ_ONLY_OPTIMAL.
					if (physical_dimensions[invalidate.resource_index].is_storage_image())
						res.initial_layout = VK_IMAGE_LAYOUT_GENERAL;
					else
						res.initial_layout = invalidate.layout;
				}

				// A read-only invalidation can change the layout.
				if (physical_dimensions[invalidate.resource_index].is_storage_image())
					res.final_layout = VK_IMAGE_LAYOUT_GENERAL;
				else
					res.final_layout = invalidate.layout;

				// All pending flushes have been invalidated in the appropriate stages already.
				// This is relevant if the invalidate happens in subpass #1 and beyond.
				res.flushed_types = 0;
				res.flushed_stages = 0;
			}

			for (auto &flush : flushes)
			{
				auto &res = resource_state[flush.resource_index];

				// Transients are handled implicitly.
				if (physical_dimensions[flush.resource_index].transient ||
				    flush.resource_index == swapchain_physical_index)
				{
					continue;
				}

				// The last use of a resource in a physical pass needs to be handled externally.
				res.flushed_types |= flush.access;
				res.flushed_stages |= flush.stages;

				// Storage images should just be in GENERAL all the time instead of SHADER_READ_ONLY_OPTIMAL.
				if (physical_dimensions[flush.resource_index].is_storage_image())
					res.final_layout = VK_IMAGE_LAYOUT_GENERAL;
				else
					res.final_layout = flush.layout;

				// If we didn't have an invalidation before first flush, we must invalidate first.
				// Only first flush in a render pass needs a matching invalidation.
				if (res.initial_layout == VK_IMAGE_LAYOUT_UNDEFINED)
				{
					// If we end in TRANSFER_SRC_OPTIMAL, we actually start in COLOR_ATTACHMENT_OPTIMAL.
					if (flush.layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
					{
						res.initial_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
						res.invalidated_stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
						res.invalidated_types = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
					}
					else
					{
						res.initial_layout = flush.layout;
						res.invalidated_stages = flush.stages;
						res.invalidated_types = flush_access_to_invalidate(flush.access);
					}

					// We're not reading the resource in this pass, so we might as well transition from UNDEFINED to discard the resource.
					physical_pass.discards.push_back(flush.resource_index);
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

			VK_ASSERT(resource.final_layout != VK_IMAGE_LAYOUT_UNDEFINED);

			unsigned index = unsigned(&resource - resource_state.data());

			physical_pass.invalidate.push_back(
					{ index, resource.initial_layout, resource.invalidated_types, resource.invalidated_stages, false });

			if (resource.flushed_types)
			{
				// Did the pass write anything in this pass which needs to be flushed?
				physical_pass.flush.push_back({ index, resource.final_layout, resource.flushed_types, resource.flushed_stages, false });
			}
			else if (resource.invalidated_types)
			{
				// Did the pass read anything in this pass which needs to be protected before it can be written?
				// Implement this as a flush with 0 access bits.
				// This is how Vulkan essentially implements a write-after-read hazard.
				// The only purpose of this flush barrier is to set the last pass which the resource was used as a stage.
				// Do not clear last_invalidate_pass, because we can still keep tacking on new access flags, etc.
				physical_pass.flush.push_back({ index, resource.final_layout, 0, resource.invalidated_stages, false });
			}

			// If we end in TRANSFER_SRC_OPTIMAL, this is a sentinel for needing mipmapping, so enqueue that up here.
			if (resource.final_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
			{
				physical_pass.mipmap_requests.push_back({ index, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				                                          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
			}
		}
	}
}

void RenderGraph::build_barriers()
{
	pass_barriers.clear();
	pass_barriers.reserve(pass_stack.size());

	const auto get_access = [&](vector<Barrier> &barriers, unsigned index, bool history) -> Barrier & {
		auto itr = find_if(begin(barriers), end(barriers), [index, history](const Barrier &b) {
			return index == b.resource_index && history == b.history;
		});
		if (itr != end(barriers))
			return *itr;
		else
		{
			barriers.push_back({ index, VK_IMAGE_LAYOUT_UNDEFINED, 0, 0, history });
			return barriers.back();
		}
	};

	for (auto &index : pass_stack)
	{
		auto &pass = *passes[index];
		Barriers barriers;

		const auto get_invalidate_access = [&](unsigned i, bool history) -> Barrier & {
			return get_access(barriers.invalidate, i, history);
		};

		const auto get_flush_access = [&](unsigned i) -> Barrier & {
			return get_access(barriers.flush, i, false);
		};

		for (auto &input : pass.get_generic_buffer_inputs())
		{
			auto &barrier = get_invalidate_access(input.buffer->get_physical_index(), false);
			barrier.access |= input.access;
			barrier.stages |= input.stages;
			if (barrier.layout != VK_IMAGE_LAYOUT_UNDEFINED)
				throw logic_error("Layout mismatch.");
			barrier.layout = input.layout;
		}

		for (auto &input : pass.get_generic_texture_inputs())
		{
			auto &barrier = get_invalidate_access(input.texture->get_physical_index(), false);
			barrier.access |= input.access;
			barrier.stages |= input.stages;
			if (barrier.layout != VK_IMAGE_LAYOUT_UNDEFINED)
				throw logic_error("Layout mismatch.");
			barrier.layout = input.layout;
		}

		for (auto *input : pass.get_history_inputs())
		{
			auto &barrier = get_invalidate_access(input->get_physical_index(), true);
			barrier.access |= VK_ACCESS_SHADER_READ_BIT;

			if ((pass.get_queue() & compute_queues) == 0)
				barrier.stages |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; // TODO: Pick appropriate stage.
			else
				barrier.stages |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

			if (barrier.layout != VK_IMAGE_LAYOUT_UNDEFINED)
				throw logic_error("Layout mismatch.");
			barrier.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}

		for (auto *input : pass.get_attachment_inputs())
		{
			if (pass.get_queue() & compute_queues)
				throw logic_error("Only graphics passes can have input attachments.");

			auto &barrier = get_invalidate_access(input->get_physical_index(), false);
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

			auto &barrier = get_invalidate_access(input->get_physical_index(), false);
			barrier.access |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			if ((pass.get_queue() & compute_queues) == 0)
				barrier.stages |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; // TODO: Pick appropriate stage.
			else
				barrier.stages |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

			if (barrier.layout != VK_IMAGE_LAYOUT_UNDEFINED)
				throw logic_error("Layout mismatch.");
			barrier.layout = VK_IMAGE_LAYOUT_GENERAL;
		}

		for (auto *input : pass.get_storage_texture_inputs())
		{
			if (!input)
				continue;

			auto &barrier = get_invalidate_access(input->get_physical_index(), false);
			barrier.access |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			if ((pass.get_queue() & compute_queues) == 0)
				barrier.stages |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; // TODO: Pick appropriate stage.
			else
				barrier.stages |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

			if (barrier.layout != VK_IMAGE_LAYOUT_UNDEFINED)
				throw logic_error("Layout mismatch.");
			barrier.layout = VK_IMAGE_LAYOUT_GENERAL;
		}

		for (auto *input : pass.get_blit_texture_inputs())
		{
			if (!input)
				continue;

			auto &barrier = get_invalidate_access(input->get_physical_index(), false);
			barrier.access |= VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.stages |= VK_PIPELINE_STAGE_TRANSFER_BIT;
			if (barrier.layout != VK_IMAGE_LAYOUT_UNDEFINED)
				throw logic_error("Layout mismatch.");
			barrier.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		}

		for (auto *input : pass.get_color_inputs())
		{
			if (!input)
				continue;

			if (pass.get_queue() & compute_queues)
				throw logic_error("Only graphics passes can have color inputs.");

			auto &barrier = get_invalidate_access(input->get_physical_index(), false);
			barrier.access |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
			barrier.stages |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

			// If the attachment is also bound as an input attachment (programmable blending)
			// we need VK_IMAGE_LAYOUT_GENERAL.
			if (barrier.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
				barrier.layout = VK_IMAGE_LAYOUT_GENERAL;
			else if (barrier.layout != VK_IMAGE_LAYOUT_UNDEFINED)
				throw logic_error("Layout mismatch.");
			else
				barrier.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}

		for (auto *input : pass.get_color_scale_inputs())
		{
			if (!input)
				continue;

			if (pass.get_queue() & compute_queues)
				throw logic_error("Only graphics passes can have scaled color inputs.");

			auto &barrier = get_invalidate_access(input->get_physical_index(), false);
			barrier.access |= VK_ACCESS_SHADER_READ_BIT;
			barrier.stages |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			if (barrier.layout != VK_IMAGE_LAYOUT_UNDEFINED)
				throw logic_error("Layout mismatch.");
			barrier.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}

		for (auto *output : pass.get_color_outputs())
		{
			if (pass.get_queue() & compute_queues)
				throw logic_error("Only graphics passes can have color outputs.");

			auto &barrier = get_flush_access(output->get_physical_index());

			if (physical_dimensions[output->get_physical_index()].levels > 1)
			{
				// access should be 0 here. generate_mipmaps will take care of invalidation needed.
				barrier.access |= VK_ACCESS_TRANSFER_READ_BIT; // Validation layers complain without this.
				barrier.stages |= VK_PIPELINE_STAGE_TRANSFER_BIT;
				if (barrier.layout != VK_IMAGE_LAYOUT_UNDEFINED)
					throw logic_error("Layout mismatch.");
				barrier.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			}
			else
			{
				barrier.access |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				barrier.stages |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

				// If the attachment is also bound as an input attachment (programmable blending)
				// we need VK_IMAGE_LAYOUT_GENERAL.
				if (barrier.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
				    barrier.layout == VK_IMAGE_LAYOUT_GENERAL)
				{
					barrier.layout = VK_IMAGE_LAYOUT_GENERAL;
				}
				else if (barrier.layout != VK_IMAGE_LAYOUT_UNDEFINED)
					throw logic_error("Layout mismatch.");
				else
					barrier.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			}
		}

		for (auto *output : pass.get_resolve_outputs())
		{
			if (pass.get_queue() & compute_queues)
				throw logic_error("Only graphics passes can resolve outputs.");

			auto &barrier = get_flush_access(output->get_physical_index());
			barrier.access |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			barrier.stages |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			if (barrier.layout != VK_IMAGE_LAYOUT_UNDEFINED)
				throw logic_error("Layout mismatch.");
			barrier.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}

		for (auto *output : pass.get_blit_texture_outputs())
		{
			auto &barrier = get_invalidate_access(output->get_physical_index(), false);
			barrier.access |= VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.stages |= VK_PIPELINE_STAGE_TRANSFER_BIT;
			if (barrier.layout != VK_IMAGE_LAYOUT_UNDEFINED)
				throw logic_error("Layout mismatch.");
			barrier.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		}

		for (auto *output : pass.get_storage_outputs())
		{
			auto &barrier = get_flush_access(output->get_physical_index());
			barrier.access |= VK_ACCESS_SHADER_WRITE_BIT;

			if ((pass.get_queue() & compute_queues) == 0)
				barrier.stages |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; // TODO: Pick appropriate stage.
			else
				barrier.stages |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

			if (barrier.layout != VK_IMAGE_LAYOUT_UNDEFINED)
				throw logic_error("Layout mismatch.");
			barrier.layout = VK_IMAGE_LAYOUT_GENERAL;
		}

		for (auto *output : pass.get_transfer_outputs())
		{
			auto &barrier = get_flush_access(output->get_physical_index());
			barrier.access |= VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.stages |= VK_PIPELINE_STAGE_TRANSFER_BIT;
			if (barrier.layout != VK_IMAGE_LAYOUT_UNDEFINED)
				throw logic_error("Layout mismatch.");
			barrier.layout = VK_IMAGE_LAYOUT_GENERAL;
		}

		for (auto *output : pass.get_storage_texture_outputs())
		{
			auto &barrier = get_flush_access(output->get_physical_index());
			barrier.access |= VK_ACCESS_SHADER_WRITE_BIT;

			if ((pass.get_queue() & compute_queues) == 0)
				barrier.stages |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; // TODO: Pick appropriate stage.
			else
				barrier.stages |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

			if (barrier.layout != VK_IMAGE_LAYOUT_UNDEFINED)
				throw logic_error("Layout mismatch.");
			barrier.layout = VK_IMAGE_LAYOUT_GENERAL;
		}

		auto *output = pass.get_depth_stencil_output();
		auto *input = pass.get_depth_stencil_input();

		if ((output || input) && (pass.get_queue() & compute_queues))
			throw logic_error("Only graphics passes can have depth attachments.");

		if (output && input)
		{
			auto &dst_barrier = get_invalidate_access(input->get_physical_index(), false);
			auto &src_barrier = get_flush_access(output->get_physical_index());

			if (dst_barrier.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
				dst_barrier.layout = VK_IMAGE_LAYOUT_GENERAL;
			else if (dst_barrier.layout != VK_IMAGE_LAYOUT_UNDEFINED)
				throw logic_error("Layout mismatch.");
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
			auto &dst_barrier = get_invalidate_access(input->get_physical_index(), false);

			if (dst_barrier.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
				dst_barrier.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
			else if (dst_barrier.layout != VK_IMAGE_LAYOUT_UNDEFINED)
				throw logic_error("Layout mismatch.");
			else
				dst_barrier.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

			dst_barrier.access |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
			dst_barrier.stages |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		}
		else if (output)
		{
			auto &src_barrier = get_flush_access(output->get_physical_index());

			if (src_barrier.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
				src_barrier.layout = VK_IMAGE_LAYOUT_GENERAL;
			else if (src_barrier.layout != VK_IMAGE_LAYOUT_UNDEFINED)
				throw logic_error("Layout mismatch.");
			else
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

void RenderGraph::enable_timestamps(bool enable)
{
	enabled_timestamps = enable;
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
	physical_image_attachments.clear();
	physical_events.clear();
	physical_history_events.clear();
	physical_history_image_attachments.clear();
}

}
