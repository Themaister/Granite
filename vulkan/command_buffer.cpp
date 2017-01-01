#include "command_buffer.hpp"
#include "device.hpp"
#include "format.hpp"
#include <string.h>

using namespace std;

namespace Vulkan
{
CommandBuffer::CommandBuffer(Device *device, VkCommandBuffer cmd, VkPipelineCache cache)
    : device(device)
    , cmd(cmd)
    , cache(cache)
{
	begin_compute();
	set_opaque_state();
}

void CommandBuffer::copy_buffer(const Buffer &dst, VkDeviceSize dst_offset, const Buffer &src, VkDeviceSize src_offset,
                                VkDeviceSize size)
{
	const VkBufferCopy region = {
		src_offset, dst_offset, size,
	};
	vkCmdCopyBuffer(cmd, src.get_buffer(), dst.get_buffer(), 1, &region);
}

void CommandBuffer::copy_buffer(const Buffer &dst, const Buffer &src)
{
	VK_ASSERT(dst.get_create_info().size == src.get_create_info().size);
	copy_buffer(dst, 0, src, 0, dst.get_create_info().size);
}

void CommandBuffer::copy_buffer_to_image(const Image &image, const Buffer &src, VkDeviceSize buffer_offset,
                                         const VkOffset3D &offset, const VkExtent3D &extent, unsigned row_length,
                                         unsigned slice_height, const VkImageSubresourceLayers &subresource)
{
	const VkBufferImageCopy region = {
		buffer_offset, row_length, slice_height, subresource, offset, extent,
	};
	vkCmdCopyBufferToImage(cmd, src.get_buffer(), image.get_image(), image.get_layout(), 1, &region);
}

void CommandBuffer::copy_image_to_buffer(const Buffer &buffer, const Image &image, VkDeviceSize buffer_offset,
                                         const VkOffset3D &offset, const VkExtent3D &extent, unsigned row_length,
                                         unsigned slice_height, const VkImageSubresourceLayers &subresource)
{
	const VkBufferImageCopy region = {
		buffer_offset, row_length, slice_height, subresource, offset, extent,
	};
	vkCmdCopyImageToBuffer(cmd, image.get_image(), image.get_layout(), buffer.get_buffer(), 1, &region);
}

void CommandBuffer::clear_image(const Image &image, const VkClearValue &value)
{
	VK_ASSERT(!framebuffer);
	VK_ASSERT(!render_pass);

	auto aspect = format_to_aspect_mask(image.get_format());
	VkImageSubresourceRange range = {};
	range.aspectMask = aspect;
	range.baseArrayLayer = 0;
	range.baseMipLevel = 0;
	range.levelCount = image.get_create_info().levels;
	range.layerCount = image.get_create_info().layers;
	if (aspect & VK_IMAGE_ASPECT_COLOR_BIT)
		vkCmdClearColorImage(cmd, image.get_image(), image.get_layout(), &value.color, 1, &range);
	else
		vkCmdClearDepthStencilImage(cmd, image.get_image(), image.get_layout(), &value.depthStencil, 1, &range);
}

void CommandBuffer::clear_quad(unsigned attachment, const VkClearRect &rect, const VkClearValue &value,
                               VkImageAspectFlags aspect)
{
	VK_ASSERT(framebuffer);
	VK_ASSERT(render_pass);
	VkClearAttachment att = {};
	att.clearValue = value;
	att.colorAttachment = attachment;
	att.aspectMask = aspect;
	vkCmdClearAttachments(cmd, 1, &att, 1, &rect);
}

void CommandBuffer::full_barrier()
{
	VK_ASSERT(!render_pass);
	VK_ASSERT(!framebuffer);
	barrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
	        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT |
	            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
	        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
	        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
	            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
	            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
}

void CommandBuffer::pixel_barrier()
{
	VK_ASSERT(render_pass);
	VK_ASSERT(framebuffer);
	VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
	barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
	                     VK_DEPENDENCY_BY_REGION_BIT, 1, &barrier, 0, nullptr, 0, nullptr);
}

void CommandBuffer::barrier(VkPipelineStageFlags src_stages, VkAccessFlags src_access, VkPipelineStageFlags dst_stages,
                            VkAccessFlags dst_access)
{
	VK_ASSERT(!render_pass);
	VK_ASSERT(!framebuffer);
	VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
	barrier.srcAccessMask = src_access;
	barrier.dstAccessMask = dst_access;
	vkCmdPipelineBarrier(cmd, src_stages, dst_stages, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void CommandBuffer::buffer_barrier(const Buffer &buffer, VkPipelineStageFlags src_stages, VkAccessFlags src_access,
                                   VkPipelineStageFlags dst_stages, VkAccessFlags dst_access)
{
	VK_ASSERT(!render_pass);
	VK_ASSERT(!framebuffer);
	VkBufferMemoryBarrier barrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
	barrier.srcAccessMask = src_access;
	barrier.dstAccessMask = dst_access;
	barrier.buffer = buffer.get_buffer();
	barrier.offset = 0;
	barrier.size = buffer.get_create_info().size;

	vkCmdPipelineBarrier(cmd, src_stages, dst_stages, 0, 0, nullptr, 1, &barrier, 0, nullptr);
}

void CommandBuffer::image_barrier(const Image &image, VkImageLayout old_layout, VkImageLayout new_layout,
                                  VkPipelineStageFlags src_stages, VkAccessFlags src_access,
                                  VkPipelineStageFlags dst_stages, VkAccessFlags dst_access)
{
	VK_ASSERT(!render_pass);
	VK_ASSERT(!framebuffer);
	VK_ASSERT(image.get_create_info().domain == ImageDomain::Physical);

	VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
	barrier.srcAccessMask = src_access;
	barrier.dstAccessMask = dst_access;
	barrier.oldLayout = old_layout;
	barrier.newLayout = new_layout;
	barrier.image = image.get_image();
	barrier.subresourceRange.aspectMask = format_to_aspect_mask(image.get_create_info().format);
	barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
	barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

	vkCmdPipelineBarrier(cmd, src_stages, dst_stages, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void CommandBuffer::image_barrier(const Image &image, VkPipelineStageFlags src_stages, VkAccessFlags src_access,
                                  VkPipelineStageFlags dst_stages, VkAccessFlags dst_access)
{
	image_barrier(image, image.get_layout(), image.get_layout(), src_stages, src_access, dst_stages, dst_access);
}

void CommandBuffer::generate_mipmap(const Image &image)
{
	auto &create_info = image.get_create_info();
	VkOffset3D size = { int(create_info.width), int(create_info.height), int(create_info.depth) };
	const VkOffset3D origin = { 0, 0, 0 };

	for (unsigned i = 1; i < create_info.levels; i++)
	{
		VkOffset3D src_size = size;
		size.x = max(size.x >> 1, 1);
		size.y = max(size.y >> 1, 1);
		size.z = max(size.z >> 1, 1);

		blit_image(image, image, origin, size, origin, src_size, i, i - 1, 0, 0, create_info.layers, VK_FILTER_LINEAR);

		if (i + 1 < create_info.levels)
		{
			image_barrier(image, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
			              VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
		}
	}
}

void CommandBuffer::blit_image(const Image &dst, const Image &src, const VkOffset3D &dst_offset,
                               const VkOffset3D &dst_extent, const VkOffset3D &src_offset, const VkOffset3D &src_extent,
                               unsigned dst_level, unsigned src_level, unsigned dst_base_layer, unsigned src_base_layer,
                               unsigned num_layers, VkFilter filter)
{
	const auto add_offset = [](const VkOffset3D &a, const VkOffset3D &b) -> VkOffset3D {
		return { a.x + b.x, a.y + b.y, a.z + b.z };
	};

	const VkImageBlit blit = {
		{ format_to_aspect_mask(src.get_create_info().format), src_level, src_base_layer, num_layers },
		{ src_offset, add_offset(src_offset, src_extent) },
		{ format_to_aspect_mask(dst.get_create_info().format), dst_level, dst_base_layer, num_layers },
		{ dst_offset, add_offset(dst_offset, dst_extent) },
	};

	vkCmdBlitImage(cmd, src.get_image(), src.get_layout(), dst.get_image(), dst.get_layout(), 1, &blit, filter);
}

void CommandBuffer::begin_context()
{
	dirty = ~0u;
	dirty_sets = ~0u;
	dirty_vbos = ~0u;
	current_pipeline = VK_NULL_HANDLE;
	current_pipeline_layout = VK_NULL_HANDLE;
	current_layout = nullptr;
	current_program = nullptr;
	memset(cookies, 0, sizeof(cookies));
	memset(secondary_cookies, 0, sizeof(secondary_cookies));
	memset(&index, 0, sizeof(index));
	memset(vbo_buffers, 0, sizeof(vbo_buffers));
}

void CommandBuffer::begin_compute()
{
	is_compute = true;
	begin_context();
}

void CommandBuffer::begin_graphics()
{
	is_compute = false;
	begin_context();
}

void CommandBuffer::begin_render_pass(const RenderPassInfo &info)
{
	VK_ASSERT(!framebuffer);
	VK_ASSERT(!render_pass);

	framebuffer = &device->request_framebuffer(info);
	render_pass = &framebuffer->get_render_pass();

	VkRect2D rect = info.render_area;
	rect.offset.x = min(framebuffer->get_width(), uint32_t(rect.offset.x));
	rect.offset.y = min(framebuffer->get_height(), uint32_t(rect.offset.y));
	rect.extent.width = min(framebuffer->get_width() - rect.offset.x, rect.extent.width);
	rect.extent.height = min(framebuffer->get_height() - rect.offset.y, rect.extent.height);

	VkClearValue clear_values[VULKAN_NUM_ATTACHMENTS + 1];
	unsigned num_clear_values = 0;

	for (unsigned i = 0; i < info.num_color_attachments; i++)
	{
		if (info.color_attachments[i])
		{
			clear_values[num_clear_values++].color = info.clear_color[i];
			if (info.color_attachments[i]->get_image().is_swapchain_image())
				uses_swapchain = true;
		}
	}

	if (info.depth_stencil)
		clear_values[num_clear_values++].depthStencil = info.clear_depth_stencil;

	VkRenderPassBeginInfo begin_info = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
	begin_info.renderPass = render_pass->get_render_pass();
	begin_info.framebuffer = framebuffer->get_framebuffer();
	begin_info.renderArea = rect;
	begin_info.clearValueCount = num_clear_values;
	begin_info.pClearValues = clear_values;

	vkCmdBeginRenderPass(cmd, &begin_info, VK_SUBPASS_CONTENTS_INLINE);

	viewport = { 0.0f, 0.0f, float(framebuffer->get_width()), float(framebuffer->get_height()), 0.0f, 1.0f };
	scissor = rect;
	begin_graphics();

	render_pass_info = info;
}

void CommandBuffer::end_render_pass()
{
	VK_ASSERT(framebuffer);
	VK_ASSERT(render_pass);

	vkCmdEndRenderPass(cmd);

	framebuffer = nullptr;
	render_pass = nullptr;
	begin_compute();
}

VkPipeline CommandBuffer::build_graphics_pipeline(Hash hash)
{
	// Viewport state
	VkPipelineViewportStateCreateInfo vp = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
	vp.viewportCount = 1;
	vp.scissorCount = 1;

	// Dynamic state
	VkPipelineDynamicStateCreateInfo dyn = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
	dyn.dynamicStateCount = 2;
	VkDynamicState states[7] = {
		VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_VIEWPORT,
	};
	dyn.pDynamicStates = states;

	if (static_state.state.depth_bias_enable)
		states[dyn.dynamicStateCount++] = VK_DYNAMIC_STATE_DEPTH_BIAS;
	if (static_state.state.stencil_test)
	{
		states[dyn.dynamicStateCount++] = VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK;
		states[dyn.dynamicStateCount++] = VK_DYNAMIC_STATE_STENCIL_REFERENCE;
		states[dyn.dynamicStateCount++] = VK_DYNAMIC_STATE_STENCIL_WRITE_MASK;
	}

	dynamic_state.depth_bias_enable = static_state.state.depth_bias_enable != 0;
	dynamic_state.stencil_enable = static_state.state.stencil_test != 0;

	// Blend state
	VkPipelineColorBlendAttachmentState blend_attachments[VULKAN_NUM_ATTACHMENTS];
	VkPipelineColorBlendStateCreateInfo blend = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
	blend.attachmentCount = render_pass_info.num_color_attachments;
	blend.pAttachments = blend_attachments;
	for (unsigned i = 0; i < blend.attachmentCount; i++)
	{
		auto &att = blend_attachments[i];
		att = {};
		att.colorWriteMask = (static_state.state.write_mask >> (4 * i)) & 0xf;
		att.blendEnable = static_state.state.blend_enable;
		if (att.blendEnable)
		{
			att.alphaBlendOp = static_cast<VkBlendOp>(static_state.state.alpha_blend_op);
			att.colorBlendOp = static_cast<VkBlendOp>(static_state.state.color_blend_op);
			att.dstAlphaBlendFactor = static_cast<VkBlendFactor>(static_state.state.dst_alpha_blend);
			att.srcAlphaBlendFactor = static_cast<VkBlendFactor>(static_state.state.src_alpha_blend);
			att.dstColorBlendFactor = static_cast<VkBlendFactor>(static_state.state.dst_color_blend);
			att.srcColorBlendFactor = static_cast<VkBlendFactor>(static_state.state.src_color_blend);
		}
	}
	memcpy(blend.blendConstants, potential_static_state.blend_constants, sizeof(blend.blendConstants));

	// Depth state
	VkPipelineDepthStencilStateCreateInfo ds = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
	ds.stencilTestEnable = render_pass->has_stencil() && static_state.state.stencil_test;
	ds.depthTestEnable = render_pass->has_depth() && static_state.state.depth_test;
	ds.depthWriteEnable = render_pass->has_depth() && static_state.state.depth_write;
	if (ds.depthTestEnable)
		ds.depthCompareOp = static_cast<VkCompareOp>(static_state.state.depth_compare);

	if (ds.stencilTestEnable)
	{
		ds.front.compareOp = static_cast<VkCompareOp>(static_state.state.stencil_front_compare_op);
		ds.front.passOp = static_cast<VkStencilOp>(static_state.state.stencil_front_pass);
		ds.front.failOp = static_cast<VkStencilOp>(static_state.state.stencil_front_fail);
		ds.front.depthFailOp = static_cast<VkStencilOp>(static_state.state.stencil_front_depth_fail);
		ds.back.passOp = static_cast<VkStencilOp>(static_state.state.stencil_back_pass);
		ds.back.failOp = static_cast<VkStencilOp>(static_state.state.stencil_back_fail);
		ds.back.depthFailOp = static_cast<VkStencilOp>(static_state.state.stencil_back_depth_fail);
	}

	// Vertex input
	VkPipelineVertexInputStateCreateInfo vi = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
	VkVertexInputAttributeDescription vi_attribs[VULKAN_NUM_VERTEX_ATTRIBS];
	vi.pVertexAttributeDescriptions = vi_attribs;
	uint32_t attr_mask = current_layout->get_resource_layout().attribute_mask;
	uint32_t binding_mask = 0;
	for_each_bit(attr_mask, [&](uint32_t bit) {
		auto &attr = vi_attribs[vi.vertexAttributeDescriptionCount++];
		attr.location = bit;
		attr.binding = attribs[bit].binding;
		attr.format = attribs[bit].format;
		attr.offset = attribs[bit].offset;
		binding_mask |= 1u << attr.binding;
	});

	VkVertexInputBindingDescription vi_bindings[VULKAN_NUM_VERTEX_BUFFERS];
	vi.pVertexBindingDescriptions = vi_bindings;
	for_each_bit(binding_mask, [&](uint32_t bit) {
		auto &bind = vi_bindings[vi.vertexBindingDescriptionCount++];
		bind.binding = bit;
		bind.inputRate = vbo_input_rates[bit];
		bind.stride = vbo_strides[bit];
	});

	// Input assembly
	VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
	ia.primitiveRestartEnable = static_state.state.primitive_restart;
	ia.topology = static_cast<VkPrimitiveTopology>(static_state.state.topology);

	// Multisample
	VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
	// TODO: Support more
	ms.rasterizationSamples = static_cast<VkSampleCountFlagBits>(render_pass->get_sample_count());
	ms.alphaToCoverageEnable = static_state.state.alpha_to_coverage;
	ms.alphaToOneEnable = static_state.state.alpha_to_one;
	ms.sampleShadingEnable = static_state.state.sample_shading;
	ms.minSampleShading = 1.0f;

	// Raster
	VkPipelineRasterizationStateCreateInfo raster = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
	raster.cullMode = static_cast<VkCullModeFlags>(static_state.state.cull_mode);
	raster.frontFace = static_cast<VkFrontFace>(static_state.state.front_face);
	raster.lineWidth = 1.0f;
	raster.polygonMode = VK_POLYGON_MODE_FILL;

	// Stages
	VkPipelineShaderStageCreateInfo stages[static_cast<unsigned>(ShaderStage::Count)];
	unsigned num_stages = 0;

	for (unsigned i = 0; i < static_cast<unsigned>(ShaderStage::Count); i++)
	{
		auto stage = static_cast<ShaderStage>(i);
		if (current_program->get_shader(stage))
		{
			auto &s = stages[num_stages++];
			s = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
			s.module = current_program->get_shader(stage)->get_module();
			s.pName = "main";
			s.stage = static_cast<VkShaderStageFlagBits>(1u << i);
		}
	}

	VkGraphicsPipelineCreateInfo pipe = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
	pipe.layout = current_pipeline_layout;
	pipe.renderPass = render_pass->get_render_pass();
	pipe.subpass = 0;

	pipe.pViewportState = &vp;
	pipe.pDynamicState = &dyn;
	pipe.pColorBlendState = &blend;
	pipe.pDepthStencilState = &ds;
	pipe.pVertexInputState = &vi;
	pipe.pInputAssemblyState = &ia;
	pipe.pMultisampleState = &ms;
	pipe.pRasterizationState = &raster;
	pipe.pStages = stages;
	pipe.stageCount = num_stages;

	VkResult res = vkCreateGraphicsPipelines(device->get_device(), cache, 1, &pipe, nullptr, &current_pipeline);
	if (res != VK_SUCCESS)
		LOG("Failed to create graphics pipeline!\n");

	current_program->add_graphics_pipeline(hash, current_pipeline);
	return current_pipeline;
}

void CommandBuffer::flush_graphics_pipeline()
{
	Hasher h;
	active_vbos = 0;
	auto &layout = current_layout->get_resource_layout();
	for_each_bit(layout.attribute_mask, [&](uint32_t bit) {
		h.u32(bit);
		active_vbos |= 1u << attribs[bit].binding;
		h.u32(attribs[bit].binding);
		h.u32(attribs[bit].format);
		h.u32(attribs[bit].offset);
	});

	for_each_bit(active_vbos, [&](uint32_t bit) {
		h.u32(vbo_input_rates[bit]);
		h.u32(vbo_strides[bit]);
	});

	h.u64(render_pass->get_cookie());
	h.u64(current_program->get_cookie());
	h.data(static_state.words, sizeof(static_state.words));

	if (static_state.state.blend_enable)
	{
		const auto needs_blend_constant = [](VkBlendFactor factor) {
			return factor == VK_BLEND_FACTOR_CONSTANT_COLOR || factor == VK_BLEND_FACTOR_CONSTANT_ALPHA;
		};
		bool b0 = needs_blend_constant(static_cast<VkBlendFactor>(static_state.state.src_color_blend));
		bool b1 = needs_blend_constant(static_cast<VkBlendFactor>(static_state.state.src_alpha_blend));
		bool b2 = needs_blend_constant(static_cast<VkBlendFactor>(static_state.state.dst_color_blend));
		bool b3 = needs_blend_constant(static_cast<VkBlendFactor>(static_state.state.dst_alpha_blend));
		if (b0 || b1 || b2 || b3)
			h.data(reinterpret_cast<uint32_t *>(potential_static_state.blend_constants),
			       sizeof(potential_static_state.blend_constants));
	}

	auto hash = h.get();
	current_pipeline = current_program->get_graphics_pipeline(hash);
	if (current_pipeline == VK_NULL_HANDLE)
		current_pipeline = build_graphics_pipeline(hash);
}

void CommandBuffer::flush_compute_state()
{
	VK_ASSERT(current_layout);
	VK_ASSERT(current_program);

	if (get_and_clear(COMMAND_BUFFER_DIRTY_PIPELINE_BIT))
	{
		VkPipeline old_pipe = current_pipeline;
		current_pipeline = current_program->get_compute_pipeline();
		if (old_pipe != current_pipeline)
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, current_pipeline);
	}

	flush_descriptor_sets();

	if (get_and_clear(COMMAND_BUFFER_DIRTY_PUSH_CONSTANTS_BIT))
	{
		uint32_t num_ranges = current_layout->get_resource_layout().num_ranges;
		for (unsigned i = 0; i < num_ranges; i++)
		{
			auto &range = current_layout->get_resource_layout().ranges[i];
			vkCmdPushConstants(cmd, current_pipeline_layout, range.stageFlags, range.offset, range.size,
			                   push_constant_data + range.offset);
		}
	}
}

void CommandBuffer::flush_render_state()
{
	VK_ASSERT(current_layout);
	VK_ASSERT(current_program);

	// We've invalidated pipeline state, update the VkPipeline.
	if (get_and_clear(COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT | COMMAND_BUFFER_DIRTY_PIPELINE_BIT |
	                  COMMAND_BUFFER_DIRTY_STATIC_VERTEX_BIT))
	{
		VkPipeline old_pipe = current_pipeline;
		flush_graphics_pipeline();
		if (old_pipe != current_pipeline)
		{
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, current_pipeline);
			set_dirty(COMMAND_BUFFER_DYNAMIC_BITS);
		}
	}

	flush_descriptor_sets();

	if (get_and_clear(COMMAND_BUFFER_DIRTY_PUSH_CONSTANTS_BIT))
	{
		uint32_t num_ranges = current_layout->get_resource_layout().num_ranges;
		for (unsigned i = 0; i < num_ranges; i++)
		{
			auto &range = current_layout->get_resource_layout().ranges[i];
			vkCmdPushConstants(cmd, current_pipeline_layout, range.stageFlags, range.offset, range.size,
			                   push_constant_data + range.offset);
		}
	}

	if (get_and_clear(COMMAND_BUFFER_DIRTY_VIEWPORT_BIT))
		vkCmdSetViewport(cmd, 0, 1, &viewport);
	if (get_and_clear(COMMAND_BUFFER_DIRTY_SCISSOR_BIT))
		vkCmdSetScissor(cmd, 0, 1, &scissor);
	if (dynamic_state.depth_bias_enable && get_and_clear(COMMAND_BUFFER_DIRTY_DEPTH_BIAS_BIT))
		vkCmdSetDepthBias(cmd, dynamic_state.depth_bias_constant, 0.0f, dynamic_state.depth_bias_slope);
	if (dynamic_state.stencil_enable && get_and_clear(COMMAND_BUFFER_DIRTY_STENCIL_REFERENCE_BIT))
	{
		vkCmdSetStencilCompareMask(cmd, VK_STENCIL_FACE_FRONT_BIT, dynamic_state.front_compare_mask);
		vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_BIT, dynamic_state.front_reference);
		vkCmdSetStencilWriteMask(cmd, VK_STENCIL_FACE_FRONT_BIT, dynamic_state.front_write_mask);
		vkCmdSetStencilCompareMask(cmd, VK_STENCIL_FACE_BACK_BIT, dynamic_state.back_compare_mask);
		vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_BACK_BIT, dynamic_state.back_reference);
		vkCmdSetStencilWriteMask(cmd, VK_STENCIL_FACE_BACK_BIT, dynamic_state.back_write_mask);
	}

	uint32_t update_vbo_mask = dirty_vbos & active_vbos;
	for_each_bit_range(update_vbo_mask, [&](uint32_t binding, uint32_t binding_count) {
#ifdef VULKAN_DEBUG
		for (unsigned i = binding; i < binding + binding_count; i++)
			VK_ASSERT(vbo_buffers[i] != VK_NULL_HANDLE);
#endif
		vkCmdBindVertexBuffers(cmd, binding, binding_count, vbo_buffers + binding, vbo_offsets + binding);
	});
	dirty_vbos &= ~update_vbo_mask;
}

void CommandBuffer::set_vertex_attrib(uint32_t attrib, uint32_t binding, VkFormat format, VkDeviceSize offset)
{
	VK_ASSERT(attrib < VULKAN_NUM_VERTEX_ATTRIBS);
	VK_ASSERT(framebuffer);

	auto &attr = attribs[attrib];

	if (attr.binding != binding || attr.format != format || attr.offset != offset)
		set_dirty(COMMAND_BUFFER_DIRTY_STATIC_VERTEX_BIT);

	VK_ASSERT(binding < VULKAN_NUM_VERTEX_BUFFERS);

	attr.binding = binding;
	attr.format = format;
	attr.offset = offset;
}

void CommandBuffer::set_index_buffer(const Buffer &buffer, VkDeviceSize offset, VkIndexType index_type)
{
	if (index.buffer == buffer.get_buffer() && index.offset == offset && index.index_type == index_type)
		return;

	index.buffer = buffer.get_buffer();
	index.offset = offset;
	index.index_type = index_type;
	vkCmdBindIndexBuffer(cmd, buffer.get_buffer(), offset, index_type);
}

void CommandBuffer::set_vertex_binding(uint32_t binding, const Buffer &buffer, VkDeviceSize offset, VkDeviceSize stride,
                                       VkVertexInputRate step_rate)
{
	VK_ASSERT(binding < VULKAN_NUM_VERTEX_BUFFERS);
	VK_ASSERT(framebuffer);

	VkBuffer vkbuffer = buffer.get_buffer();
	if (vbo_buffers[binding] != vkbuffer || vbo_offsets[binding] != offset)
		dirty_vbos |= 1u << binding;
	if (vbo_strides[binding] != stride || vbo_input_rates[binding] != step_rate)
		set_dirty(COMMAND_BUFFER_DIRTY_STATIC_VERTEX_BIT);

	vbo_buffers[binding] = vkbuffer;
	vbo_offsets[binding] = offset;
	vbo_strides[binding] = stride;
	vbo_input_rates[binding] = step_rate;
}

void CommandBuffer::set_viewport(const VkViewport &viewport)
{
	VK_ASSERT(framebuffer);
	this->viewport = viewport;
	set_dirty(COMMAND_BUFFER_DIRTY_VIEWPORT_BIT);
}

void CommandBuffer::set_scissor(const VkRect2D &rect)
{
	VK_ASSERT(framebuffer);
	scissor = rect;
	set_dirty(COMMAND_BUFFER_DIRTY_SCISSOR_BIT);
}

void CommandBuffer::push_constants(const void *data, VkDeviceSize offset, VkDeviceSize range)
{
	VK_ASSERT(offset + range <= VULKAN_PUSH_CONSTANT_SIZE);
	memcpy(push_constant_data + offset, data, range);
	set_dirty(COMMAND_BUFFER_DIRTY_PUSH_CONSTANTS_BIT);
}

void CommandBuffer::set_program(Program &program)
{
	if (current_program && current_program->get_cookie() == program.get_cookie())
		return;

	current_program = &program;
	current_pipeline = VK_NULL_HANDLE;

	VK_ASSERT((framebuffer && current_program->get_shader(ShaderStage::Vertex)) ||
	          (!framebuffer && current_program->get_shader(ShaderStage::Compute)));

	set_dirty(COMMAND_BUFFER_DIRTY_PIPELINE_BIT | COMMAND_BUFFER_DYNAMIC_BITS);

	if (!current_layout)
	{
		dirty_sets = ~0u;
		set_dirty(COMMAND_BUFFER_DIRTY_PUSH_CONSTANTS_BIT);

		current_layout = program.get_pipeline_layout();
		current_pipeline_layout = current_layout->get_layout();
	}
	else if (program.get_pipeline_layout()->get_cookie() != current_layout->get_cookie())
	{
		auto &new_layout = program.get_pipeline_layout()->get_resource_layout();
		auto &old_layout = current_layout->get_resource_layout();

		// If the push constant layout changes, all descriptor sets
		// are invalidated.
		if (new_layout.push_constant_layout_hash != old_layout.push_constant_layout_hash)
		{
			dirty_sets = ~0u;
			set_dirty(COMMAND_BUFFER_DIRTY_PUSH_CONSTANTS_BIT);
		}
		else
		{
			// Find the first set whose descriptor set layout differs.
			auto *new_pipe_layout = program.get_pipeline_layout();
			for (unsigned set = 0; set < VULKAN_NUM_DESCRIPTOR_SETS; set++)
			{
				if (new_pipe_layout->get_allocator(set) != current_layout->get_allocator(set))
				{
					dirty_sets |= ~((1u << set) - 1);
					break;
				}
			}
		}
		current_layout = program.get_pipeline_layout();
		current_pipeline_layout = current_layout->get_layout();
	}
}

void *CommandBuffer::allocate_constant_data(unsigned set, unsigned binding, VkDeviceSize size)
{
	auto data = device->allocate_constant_data(size);
	set_uniform_buffer(set, binding, *data.buffer, data.offset, size);
	return data.data;
}

void *CommandBuffer::allocate_index_data(VkDeviceSize size, VkIndexType index_type)
{
	auto data = device->allocate_index_data(size);
	set_index_buffer(*data.buffer, data.offset, index_type);
	return data.data;
}

void *CommandBuffer::update_buffer(const Buffer &buffer, VkDeviceSize offset, VkDeviceSize size)
{
	auto data = device->allocate_staging_data(size);
	copy_buffer(buffer, offset, *data.buffer, data.offset, size);
	return data.data;
}

void *CommandBuffer::update_image(const Image &image, const VkOffset3D &offset, const VkExtent3D &extent,
                                  uint32_t row_length, uint32_t image_height,
                                  const VkImageSubresourceLayers &subresource)
{
	auto &create_info = image.get_create_info();
	uint32_t width = max(image.get_width() >> subresource.mipLevel, 1u);
	uint32_t height = max(image.get_height() >> subresource.mipLevel, 1u);
	uint32_t depth = max(image.get_depth() >> subresource.mipLevel, 1u);

	if (!row_length)
		row_length = width;
	if (!image_height)
		image_height = height;

	VkDeviceSize size =
	    format_pixel_size(create_info.format) * subresource.layerCount * depth * row_length * image_height;

	auto data = device->allocate_staging_data(size);
	copy_buffer_to_image(image, *data.buffer, data.offset, offset, extent, row_length, image_height, subresource);
	return data.data;
}

void *CommandBuffer::update_image(const Image &image, uint32_t row_length, uint32_t image_height)
{
	const VkImageSubresourceLayers subresource = {
		format_to_aspect_mask(image.get_format()), 0, 0, 1,
	};
	return update_image(image, { 0, 0, 0 }, { image.get_width(), image.get_height(), image.get_depth() }, row_length,
	                    image_height, subresource);
}

void *CommandBuffer::allocate_vertex_data(unsigned binding, VkDeviceSize size, VkDeviceSize stride,
                                          VkVertexInputRate step_rate)
{
	auto data = device->allocate_vertex_data(size);
	set_vertex_binding(binding, *data.buffer, data.offset, stride, step_rate);
	return data.data;
}

void CommandBuffer::set_uniform_buffer(unsigned set, unsigned binding, const Buffer &buffer, VkDeviceSize offset,
                                       VkDeviceSize range)
{
	VK_ASSERT(set < VULKAN_NUM_DESCRIPTOR_SETS);
	VK_ASSERT(binding < VULKAN_NUM_BINDINGS);
	VK_ASSERT(buffer.get_create_info().usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
	auto &b = bindings[set][binding];

	if (buffer.get_cookie() == cookies[set][binding] && b.buffer.offset == offset && b.buffer.range == range)
		return;

	b.buffer = { buffer.get_buffer(), offset, range };
	cookies[set][binding] = buffer.get_cookie();
	dirty_sets |= 1u << set;
}

void CommandBuffer::set_storage_buffer(unsigned set, unsigned binding, const Buffer &buffer, VkDeviceSize offset,
                                       VkDeviceSize range)
{
	VK_ASSERT(set < VULKAN_NUM_DESCRIPTOR_SETS);
	VK_ASSERT(binding < VULKAN_NUM_BINDINGS);
	VK_ASSERT(buffer.get_create_info().usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
	auto &b = bindings[set][binding];

	if (buffer.get_cookie() == cookies[set][binding] && b.buffer.offset == offset && b.buffer.range == range)
		return;

	b.buffer = { buffer.get_buffer(), offset, range };
	cookies[set][binding] = buffer.get_cookie();
	dirty_sets |= 1u << set;
}

void CommandBuffer::set_uniform_buffer(unsigned set, unsigned binding, const Buffer &buffer)
{
	set_uniform_buffer(set, binding, buffer, 0, buffer.get_create_info().size);
}

void CommandBuffer::set_storage_buffer(unsigned set, unsigned binding, const Buffer &buffer)
{
	set_storage_buffer(set, binding, buffer, 0, buffer.get_create_info().size);
}

void CommandBuffer::set_sampler(unsigned set, unsigned binding, const Sampler &sampler)
{
	VK_ASSERT(set < VULKAN_NUM_DESCRIPTOR_SETS);
	VK_ASSERT(binding < VULKAN_NUM_BINDINGS);
	if (sampler.get_cookie() == secondary_cookies[set][binding])
		return;

	auto &b = bindings[set][binding];
	b.image.sampler = sampler.get_sampler();
	dirty_sets |= 1u << set;
	secondary_cookies[set][binding] = sampler.get_cookie();
}

void CommandBuffer::set_buffer_view(unsigned set, unsigned binding, const BufferView &view)
{
	VK_ASSERT(set < VULKAN_NUM_DESCRIPTOR_SETS);
	VK_ASSERT(binding < VULKAN_NUM_BINDINGS);
	VK_ASSERT(view.get_buffer().get_create_info().usage & VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT);
	if (view.get_cookie() == cookies[set][binding])
		return;
	auto &b = bindings[set][binding];
	b.buffer_view = view.get_view();
	cookies[set][binding] = view.get_cookie();
	dirty_sets |= 1u << set;
}

void CommandBuffer::set_input_attachment(unsigned set, unsigned binding, const ImageView &view)
{
	VK_ASSERT(set < VULKAN_NUM_DESCRIPTOR_SETS);
	VK_ASSERT(binding < VULKAN_NUM_BINDINGS);
	VK_ASSERT(view.get_image().get_create_info().usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT);
	if (view.get_cookie() == cookies[set][binding] &&
	    bindings[set][binding].image.imageLayout == view.get_image().get_layout())
		return;

	auto &b = bindings[set][binding];
	b.image.imageLayout = view.get_image().get_layout();
	b.image.imageView = view.get_view();
	cookies[set][binding] = view.get_cookie();
	dirty_sets |= 1u << set;
}

void CommandBuffer::set_texture(unsigned set, unsigned binding, const ImageView &view)
{
	VK_ASSERT(set < VULKAN_NUM_DESCRIPTOR_SETS);
	VK_ASSERT(binding < VULKAN_NUM_BINDINGS);
	VK_ASSERT(view.get_image().get_create_info().usage & VK_IMAGE_USAGE_SAMPLED_BIT);
	if (view.get_cookie() == cookies[set][binding] &&
	    bindings[set][binding].image.imageLayout == view.get_image().get_layout())
		return;

	auto &b = bindings[set][binding];
	b.image.imageLayout = view.get_image().get_layout();
	b.image.imageView = view.get_view();
	cookies[set][binding] = view.get_cookie();
	dirty_sets |= 1u << set;
}

void CommandBuffer::set_texture(unsigned set, unsigned binding, const ImageView &view, const Sampler &sampler)
{
	VK_ASSERT(set < VULKAN_NUM_DESCRIPTOR_SETS);
	VK_ASSERT(binding < VULKAN_NUM_BINDINGS);
	VK_ASSERT(view.get_image().get_create_info().usage & VK_IMAGE_USAGE_SAMPLED_BIT);
	if (view.get_cookie() == cookies[set][binding] &&
	    bindings[set][binding].image.imageLayout == view.get_image().get_layout() &&
	    sampler.get_cookie() == secondary_cookies[set][binding])
		return;

	auto &b = bindings[set][binding];
	b.image.imageLayout = view.get_image().get_layout();
	b.image.imageView = view.get_view();
	b.image.sampler = sampler.get_sampler();
	cookies[set][binding] = view.get_cookie();
	secondary_cookies[set][binding] = sampler.get_cookie();
	dirty_sets |= 1u << set;
}

void CommandBuffer::set_texture(unsigned set, unsigned binding, const ImageView &view, StockSampler stock)
{
	VK_ASSERT(set < VULKAN_NUM_DESCRIPTOR_SETS);
	VK_ASSERT(binding < VULKAN_NUM_BINDINGS);
	VK_ASSERT(view.get_image().get_create_info().usage & VK_IMAGE_USAGE_SAMPLED_BIT);
	const auto &sampler = device->get_stock_sampler(stock);
	set_texture(set, binding, view, sampler);
}

void CommandBuffer::set_storage_texture(unsigned set, unsigned binding, const ImageView &view)
{
	VK_ASSERT(set < VULKAN_NUM_DESCRIPTOR_SETS);
	VK_ASSERT(binding < VULKAN_NUM_BINDINGS);
	VK_ASSERT(view.get_image().get_create_info().usage & VK_IMAGE_USAGE_STORAGE_BIT);

	if (view.get_cookie() == cookies[set][binding] &&
	    bindings[set][binding].image.imageLayout == view.get_image().get_layout())
		return;

	auto &b = bindings[set][binding];
	b.image.imageLayout = view.get_image().get_layout();
	b.image.imageView = view.get_view();
	cookies[set][binding] = view.get_cookie();
	dirty_sets |= 1u << set;
}

void CommandBuffer::flush_descriptor_set(uint32_t set)
{
	auto &layout = current_layout->get_resource_layout();
	auto &set_layout = layout.sets[set];
	uint32_t num_dynamic_offsets = 0;
	uint32_t dynamic_offsets[VULKAN_NUM_BINDINGS];
	Hasher h;

	// UBOs
	for_each_bit(set_layout.uniform_buffer_mask, [&](uint32_t binding) {
		h.u64(cookies[set][binding]);
		h.u32(bindings[set][binding].buffer.range);
		VK_ASSERT(bindings[set][binding].buffer.buffer != VK_NULL_HANDLE);

		dynamic_offsets[num_dynamic_offsets++] = bindings[set][binding].buffer.offset;
	});

	// SSBOs
	for_each_bit(set_layout.storage_buffer_mask, [&](uint32_t binding) {
		h.u64(cookies[set][binding]);
		h.u32(bindings[set][binding].buffer.offset);
		h.u32(bindings[set][binding].buffer.range);
		VK_ASSERT(bindings[set][binding].buffer.buffer != VK_NULL_HANDLE);
	});

	// Sampled buffers
	for_each_bit(set_layout.sampled_buffer_mask, [&](uint32_t binding) {
		h.u64(cookies[set][binding]);
		VK_ASSERT(bindings[set][binding].buffer_view != VK_NULL_HANDLE);
	});

	// Sampled images
	for_each_bit(set_layout.sampled_image_mask, [&](uint32_t binding) {
		h.u64(cookies[set][binding]);
		h.u64(secondary_cookies[set][binding]);
		h.u32(bindings[set][binding].image.imageLayout);
		VK_ASSERT(bindings[set][binding].image.imageView != VK_NULL_HANDLE);
		VK_ASSERT(bindings[set][binding].image.sampler != VK_NULL_HANDLE);
	});

	// Storage images
	for_each_bit(set_layout.storage_image_mask, [&](uint32_t binding) {
		h.u64(cookies[set][binding]);
		h.u32(bindings[set][binding].image.imageLayout);
		VK_ASSERT(bindings[set][binding].image.imageView != VK_NULL_HANDLE);
	});

	// Input attachments
	for_each_bit(set_layout.input_attachment_mask, [&](uint32_t binding) {
		h.u64(cookies[set][binding]);
		h.u32(bindings[set][binding].image.imageLayout);
		VK_ASSERT(bindings[set][binding].image.imageView != VK_NULL_HANDLE);
	});

	Hash hash = h.get();
	auto allocated = current_layout->get_allocator(set)->find(hash);

	// The descriptor set was not successfully cached, rebuild.
	if (!allocated.second)
	{
		uint32_t write_count = 0;
		uint32_t buffer_info_count = 0;
		VkWriteDescriptorSet writes[VULKAN_NUM_BINDINGS];
		VkDescriptorBufferInfo buffer_info[VULKAN_NUM_BINDINGS];

		for_each_bit(set_layout.uniform_buffer_mask, [&](uint32_t binding) {
			auto &write = writes[write_count++];
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.pNext = nullptr;
			write.descriptorCount = 1;
			write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
			write.dstArrayElement = 0;
			write.dstBinding = binding;
			write.dstSet = allocated.first;

			// Offsets are applied dynamically.
			auto &buffer = buffer_info[buffer_info_count++];
			buffer = bindings[set][binding].buffer;
			buffer.offset = 0;
			write.pBufferInfo = &buffer;
		});

		for_each_bit(set_layout.storage_buffer_mask, [&](uint32_t binding) {
			auto &write = writes[write_count++];
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.pNext = nullptr;
			write.descriptorCount = 1;
			write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			write.dstArrayElement = 0;
			write.dstBinding = binding;
			write.dstSet = allocated.first;
			write.pBufferInfo = &bindings[set][binding].buffer;
		});

		for_each_bit(set_layout.sampled_buffer_mask, [&](uint32_t binding) {
			auto &write = writes[write_count++];
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.pNext = nullptr;
			write.descriptorCount = 1;
			write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
			write.dstArrayElement = 0;
			write.dstBinding = binding;
			write.dstSet = allocated.first;
			write.pTexelBufferView = &bindings[set][binding].buffer_view;
		});

		for_each_bit(set_layout.sampled_image_mask, [&](uint32_t binding) {
			auto &write = writes[write_count++];
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.pNext = nullptr;
			write.descriptorCount = 1;
			write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			write.dstArrayElement = 0;
			write.dstBinding = binding;
			write.dstSet = allocated.first;
			write.pImageInfo = &bindings[set][binding].image;
		});

		for_each_bit(set_layout.storage_image_mask, [&](uint32_t binding) {
			auto &write = writes[write_count++];
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.pNext = nullptr;
			write.descriptorCount = 1;
			write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
			write.dstArrayElement = 0;
			write.dstBinding = binding;
			write.dstSet = allocated.first;
			write.pImageInfo = &bindings[set][binding].image;
		});

		for_each_bit(set_layout.input_attachment_mask, [&](uint32_t binding) {
			auto &write = writes[write_count++];
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.pNext = nullptr;
			write.descriptorCount = 1;
			write.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
			write.dstArrayElement = 0;
			write.dstBinding = binding;
			write.dstSet = allocated.first;
			write.pImageInfo = &bindings[set][binding].image;
		});

		vkUpdateDescriptorSets(device->get_device(), write_count, writes, 0, nullptr);
	}

	vkCmdBindDescriptorSets(cmd, render_pass ? VK_PIPELINE_BIND_POINT_GRAPHICS : VK_PIPELINE_BIND_POINT_COMPUTE,
	                        current_pipeline_layout, set, 1, &allocated.first, num_dynamic_offsets, dynamic_offsets);
}

void CommandBuffer::flush_descriptor_sets()
{
	auto &layout = current_layout->get_resource_layout();
	uint32_t set_update = layout.descriptor_set_mask & dirty_sets;
	for_each_bit(set_update, [&](uint32_t set) { flush_descriptor_set(set); });
	dirty_sets &= ~set_update;
}

void CommandBuffer::draw(uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance)
{
	VK_ASSERT(current_program);
	VK_ASSERT(!is_compute);
	flush_render_state();
	vkCmdDraw(cmd, vertex_count, instance_count, first_vertex, first_instance);
}

void CommandBuffer::draw_indexed(uint32_t index_count, uint32_t instance_count, uint32_t first_index,
                                 int32_t vertex_offset, uint32_t first_instance)
{
	VK_ASSERT(current_program);
	VK_ASSERT(!is_compute);
	VK_ASSERT(index.buffer != VK_NULL_HANDLE);
	flush_render_state();
	vkCmdDrawIndexed(cmd, index_count, instance_count, first_index, vertex_offset, first_instance);
}

void CommandBuffer::dispatch(uint32_t groups_x, uint32_t groups_y, uint32_t groups_z)
{
	VK_ASSERT(current_program);
	VK_ASSERT(is_compute);
	flush_compute_state();
	vkCmdDispatch(cmd, groups_x, groups_y, groups_z);
}

void CommandBuffer::set_opaque_state()
{
	auto &state = static_state.state;
	state = {};
	state.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	state.cull_mode = VK_CULL_MODE_BACK_BIT;
	state.blend_enable = false;
	state.depth_test = true;
	state.depth_compare = VK_COMPARE_OP_LESS_OR_EQUAL;
	state.depth_write = true;
	state.depth_bias_enable = false;
	state.primitive_restart = false;
	state.stencil_test = false;
	state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	state.write_mask = ~0u;

	set_dirty(COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT);
}

void CommandBuffer::set_quad_state()
{
	auto &state = static_state.state;
	state = {};
	state.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	state.cull_mode = VK_CULL_MODE_NONE;
	state.blend_enable = false;
	state.depth_test = false;
	state.depth_write = false;
	state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
	state.write_mask = ~0u;
	set_dirty(COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT);
}
}
