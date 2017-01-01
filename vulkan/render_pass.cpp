#include "render_pass.hpp"
#include "device.hpp"
#include <utility>

using namespace std;

namespace Vulkan
{
RenderPass::RenderPass(Device *device, const RenderPassInfo &info)
    : Cookie(device)
    , device(device)
{
	fill(begin(color_attachments), end(color_attachments), VK_FORMAT_UNDEFINED);
	num_color_attachments = info.num_color_attachments;

	VkAttachmentDescription attachments[VULKAN_NUM_ATTACHMENTS + 1];
	unsigned num_attachments = 0;
	bool implicit_ds_transition = false;
	bool implicit_color_transition = false;

	VkAttachmentReference color_ref[VULKAN_NUM_ATTACHMENTS];
	VkAttachmentReference ds_ref = { VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED };

	VkAttachmentLoadOp color_load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	VkAttachmentStoreOp color_store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	VkAttachmentLoadOp ds_load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	VkAttachmentStoreOp ds_store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE;

	if (info.op_flags & RENDER_PASS_OP_CLEAR_COLOR_BIT)
		color_load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
	else if (info.op_flags & RENDER_PASS_OP_LOAD_COLOR_BIT)
		color_load_op = VK_ATTACHMENT_LOAD_OP_LOAD;

	if (info.op_flags & RENDER_PASS_OP_STORE_COLOR_BIT)
		color_store_op = VK_ATTACHMENT_STORE_OP_STORE;

	if (info.op_flags & RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT)
		ds_load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
	else if (info.op_flags & RENDER_PASS_OP_LOAD_DEPTH_STENCIL_BIT)
		ds_load_op = VK_ATTACHMENT_LOAD_OP_LOAD;

	if (info.op_flags & RENDER_PASS_OP_STORE_DEPTH_STENCIL_BIT)
		ds_store_op = VK_ATTACHMENT_STORE_OP_STORE;

	VkImageLayout color_layout = info.op_flags & RENDER_PASS_OP_COLOR_OPTIMAL_BIT ?
	                                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL :
	                                 VK_IMAGE_LAYOUT_GENERAL;
	VkImageLayout depth_stencil_layout = info.op_flags & RENDER_PASS_OP_DEPTH_STENCIL_OPTIMAL_BIT ?
	                                         VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL :
	                                         VK_IMAGE_LAYOUT_GENERAL;

	for (unsigned i = 0; i < info.num_color_attachments; i++)
	{
		color_attachments[i] =
		    info.color_attachments[i] ? info.color_attachments[i]->get_format() : VK_FORMAT_UNDEFINED;

		if (info.color_attachments[i])
		{
			auto &image = info.color_attachments[i]->get_image();
			auto &att = attachments[num_attachments];
			att.flags = 0;
			att.format = color_attachments[i];
			att.samples = image.get_create_info().samples;
			att.loadOp = color_load_op;
			att.storeOp = color_store_op;
			att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

			if (image.get_create_info().domain == ImageDomain::Transient)
			{
				if (att.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD)
					att.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
				if (att.storeOp == VK_ATTACHMENT_STORE_OP_STORE)
					att.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

				att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				att.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				color_ref[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				implicit_color_transition = true;
			}
			else if (image.is_swapchain_image())
			{
				if (att.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD)
					att.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
				att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
				color_ref[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				implicit_color_transition = true;
			}
			else
			{
				att.initialLayout = color_layout;
				att.finalLayout = color_layout;
				color_ref[i].layout = color_layout;
			}
			color_ref[i].attachment = num_attachments;
			num_attachments++;
		}
		else
		{
			color_ref[i].attachment = VK_ATTACHMENT_UNUSED;
			color_ref[i].layout = VK_IMAGE_LAYOUT_UNDEFINED;
		}
	}

	depth_stencil = info.depth_stencil ? info.depth_stencil->get_format() : VK_FORMAT_UNDEFINED;
	if (info.depth_stencil)
	{
		auto &image = info.depth_stencil->get_image();
		auto &att = attachments[num_attachments];
		att.flags = 0;
		att.format = depth_stencil;
		att.samples = image.get_create_info().samples;
		att.loadOp = ds_load_op;
		att.storeOp = ds_store_op;

		if (format_to_aspect_mask(depth_stencil) & VK_IMAGE_ASPECT_STENCIL_BIT)
		{
			att.stencilLoadOp = ds_load_op;
			att.stencilStoreOp = ds_store_op;
		}
		else
		{
			att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		}

		if (image.get_create_info().domain == ImageDomain::Transient)
		{
			if (att.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD)
				att.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			if (att.storeOp == VK_ATTACHMENT_STORE_OP_STORE)
				att.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			if (att.stencilLoadOp == VK_ATTACHMENT_LOAD_OP_LOAD)
				att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			if (att.stencilStoreOp == VK_ATTACHMENT_STORE_OP_STORE)
				att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

			// For transient attachments we force the layouts.
			att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			att.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			ds_ref.layout = att.finalLayout;
			implicit_ds_transition = true;
		}
		else
		{
			att.initialLayout = depth_stencil_layout;
			att.finalLayout = depth_stencil_layout;
			ds_ref.layout = depth_stencil_layout;
		}

		ds_ref.attachment = num_attachments;
		num_attachments++;
	}

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = num_color_attachments;
	subpass.pColorAttachments = color_ref;
	subpass.pDepthStencilAttachment = &ds_ref;

	if (info.op_flags & RENDER_PASS_OP_COLOR_FEEDBACK_BIT)
	{
		subpass.inputAttachmentCount = num_color_attachments;
		subpass.pInputAttachments = color_ref;
#ifdef VULKAN_DEBUG
		for (unsigned i = 0; i < num_color_attachments; i++)
			VK_ASSERT(color_ref[i].layout == VK_IMAGE_LAYOUT_GENERAL);
#endif
	}

	VkRenderPassCreateInfo rp_info = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
	rp_info.subpassCount = 1;
	rp_info.pSubpasses = &subpass;
	rp_info.pAttachments = attachments;
	rp_info.attachmentCount = num_attachments;

	VkSubpassDependency external_dependencies[2] = {};
	unsigned num_external_deps = 0;

	// For transient attachments and/or swapchain, implicitly perform image layout changes.
	if (implicit_color_transition || implicit_ds_transition)
	{
		auto &external_dependency = external_dependencies[num_external_deps++];
		if (implicit_color_transition)
		{
			external_dependency.srcStageMask |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			external_dependency.dstStageMask |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			external_dependency.srcAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			external_dependency.dstAccessMask |=
			    VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		}

		if (implicit_ds_transition)
		{
			external_dependency.srcStageMask |=
			    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			external_dependency.dstStageMask |=
			    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			external_dependency.srcAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			external_dependency.dstAccessMask |=
			    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
		}

		external_dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		external_dependency.dstSubpass = 0;
	}

	if (info.op_flags & RENDER_PASS_OP_COLOR_FEEDBACK_BIT)
	{
		auto &dep = external_dependencies[num_external_deps++];
		dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dep.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dep.dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
		dep.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
		dep.srcSubpass = 0;
		dep.dstSubpass = 0;
	}

	rp_info.dependencyCount = num_external_deps;
	rp_info.pDependencies = external_dependencies;

	if (vkCreateRenderPass(device->get_device(), &rp_info, nullptr, &render_pass) != VK_SUCCESS)
		LOG("Failed to create render pass.");
}

RenderPass::~RenderPass()
{
	if (render_pass != VK_NULL_HANDLE)
		vkDestroyRenderPass(device->get_device(), render_pass, nullptr);
}

Framebuffer::Framebuffer(Device *device, const RenderPass &rp, const RenderPassInfo &info)
    : Cookie(device)
    , device(device)
    , render_pass(rp)
    , info(info)
{
	width = UINT32_MAX;
	height = UINT32_MAX;
	VkImageView views[VULKAN_NUM_ATTACHMENTS + 1];
	unsigned num_views = 0;

	for (unsigned i = 0; i < info.num_color_attachments; i++)
	{
		if (info.color_attachments[i])
		{
			unsigned lod = info.color_attachments[i]->get_create_info().base_level;
			width = min(width, info.color_attachments[i]->get_image().get_width(lod));
			height = min(height, info.color_attachments[i]->get_image().get_height(lod));
			views[num_views++] = info.color_attachments[i]->get_view();
		}
	}

	if (info.depth_stencil)
	{
		unsigned lod = info.depth_stencil->get_create_info().base_level;
		width = min(width, info.depth_stencil->get_image().get_width(lod));
		height = min(height, info.depth_stencil->get_image().get_height(lod));
		views[num_views++] = info.depth_stencil->get_view();
	}

	VkFramebufferCreateInfo fb_info = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
	fb_info.renderPass = rp.get_render_pass();
	fb_info.attachmentCount = num_views;
	fb_info.pAttachments = views;
	fb_info.width = width;
	fb_info.height = height;
	fb_info.layers = 1;

	if (vkCreateFramebuffer(device->get_device(), &fb_info, nullptr, &framebuffer) != VK_SUCCESS)
		LOG("Failed to create framebuffer.");
}

Framebuffer::~Framebuffer()
{
	if (framebuffer != VK_NULL_HANDLE)
		device->destroy_framebuffer(framebuffer);
}

FramebufferAllocator::FramebufferAllocator(Device *device)
    : device(device)
{
}

void FramebufferAllocator::clear()
{
	framebuffers.clear();
}

void FramebufferAllocator::begin_frame()
{
	framebuffers.begin_frame();
}

Framebuffer &FramebufferAllocator::request_framebuffer(const RenderPassInfo &info)
{
	auto &rp = device->request_render_pass(info);
	Hasher h;
	h.u64(rp.get_cookie());

	for (unsigned i = 0; i < info.num_color_attachments; i++)
		if (info.color_attachments[i])
			h.u64(info.color_attachments[i]->get_cookie());

	if (info.depth_stencil)
		h.u64(info.depth_stencil->get_cookie());

	auto hash = h.get();
	auto *node = framebuffers.request(hash);
	if (node)
		return *node;
	return *framebuffers.emplace(hash, device, rp, info);
}

TransientAllocator::TransientAllocator(Device *device)
    : device(device)
{
}

void TransientAllocator::clear()
{
	transients.clear();
}

void TransientAllocator::begin_frame()
{
	transients.begin_frame();
}

ImageView &TransientAllocator::request_attachment(unsigned width, unsigned height, VkFormat format, unsigned index)
{
	Hasher h;
	h.u32(width);
	h.u32(height);
	h.u32(format);
	h.u32(index);

	auto hash = h.get();
	auto *node = transients.request(hash);
	if (node)
		return node->handle->get_view();

	auto image_info = ImageCreateInfo::transient_render_target(width, height, format);
	node = transients.emplace(hash, device->create_image(image_info, nullptr));
	return node->handle->get_view();
}
}
