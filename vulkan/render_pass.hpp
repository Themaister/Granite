#pragma once

#include "cookie.hpp"
#include "hashmap.hpp"
#include "image.hpp"
#include "intrusive.hpp"
#include "limits.hpp"
#include "object_pool.hpp"
#include "temporary_hashmap.hpp"
#include "vulkan.hpp"

namespace Vulkan
{
enum RenderPassOp
{
	RENDER_PASS_OP_CLEAR_COLOR_BIT = 1 << 0,
	RENDER_PASS_OP_LOAD_COLOR_BIT = 1 << 1,
	RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT = 1 << 2,
	RENDER_PASS_OP_LOAD_DEPTH_STENCIL_BIT = 1 << 3,

	RENDER_PASS_OP_STORE_COLOR_BIT = 1 << 4,
	RENDER_PASS_OP_STORE_DEPTH_STENCIL_BIT = 1 << 5,

	RENDER_PASS_OP_COLOR_OPTIMAL_BIT = 1 << 6,
	RENDER_PASS_OP_DEPTH_STENCIL_OPTIMAL_BIT = 1 << 7,

	RENDER_PASS_OP_COLOR_FEEDBACK_BIT = 1 << 8,

	RENDER_PASS_OP_CLEAR_ALL_BIT = RENDER_PASS_OP_CLEAR_COLOR_BIT | RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT,

	RENDER_PASS_OP_LOAD_ALL_BIT = RENDER_PASS_OP_LOAD_COLOR_BIT | RENDER_PASS_OP_LOAD_DEPTH_STENCIL_BIT,

	RENDER_PASS_OP_STORE_ALL_BIT = RENDER_PASS_OP_STORE_COLOR_BIT | RENDER_PASS_OP_STORE_DEPTH_STENCIL_BIT,
};
using RenderPassOpFlags = uint32_t;

class ImageView;
struct RenderPassInfo
{
	ImageView *color_attachments[VULKAN_NUM_ATTACHMENTS];
	ImageView *depth_stencil = nullptr;
	unsigned num_color_attachments = 0;
	RenderPassOpFlags op_flags = 0;

	// Render area will be clipped to the actual framebuffer.
	VkRect2D render_area = { { 0, 0 }, { UINT32_MAX, UINT32_MAX } };

	VkClearColorValue clear_color[VULKAN_NUM_ATTACHMENTS] = {};
	VkClearDepthStencilValue clear_depth_stencil = { 1.0f, 0 };

	struct Subpass
	{
		uint32_t color_attachments[VULKAN_NUM_ATTACHMENTS];
		uint32_t input_attachments[VULKAN_NUM_ATTACHMENTS];
		unsigned num_color_attachments = 0;
		unsigned num_input_attachments = 0;
		bool read_depth_stencil = false;
		bool write_depth_stencil = false;
	};
	// If 0/nullptr, assume a default subpass.
	const Subpass *subpasses = nullptr;
	unsigned num_subpasses = 0;
};

class RenderPass : public Cookie, public NoCopyNoMove
{
public:
	struct SubpassInfo
	{
		VkAttachmentReference color_attachments[VULKAN_NUM_ATTACHMENTS];
		unsigned num_color_attachments;
		VkAttachmentReference input_attachments[VULKAN_NUM_ATTACHMENTS];
		unsigned num_input_attachments;
		VkAttachmentReference depth_stencil_attachment;
	};

	RenderPass(Device *device, const RenderPassInfo &info);
	~RenderPass();

	unsigned get_num_subpasses() const
	{
		return subpasses.size();
	}

	VkRenderPass get_render_pass() const
	{
		return render_pass;
	}

	uint32_t get_sample_count() const
	{
		return 1;
	}

	unsigned get_num_color_attachments(unsigned subpass) const
	{
		assert(subpass < subpasses.size());
		return subpasses[subpass].num_color_attachments;
	}

	unsigned get_num_input_attachments(unsigned subpass) const
	{
		assert(subpass < subpasses.size());
		return subpasses[subpass].num_input_attachments;
	}

	const VkAttachmentReference &get_color_attachment(unsigned subpass, unsigned index) const
	{
		assert(subpass < subpasses.size());
		assert(index < subpasses[subpass].num_color_attachments);
		return subpasses[subpass].color_attachments[index];
	}

	const VkAttachmentReference &get_input_attachment(unsigned subpass, unsigned index) const
	{
		assert(subpass < subpasses.size());
		assert(index < subpasses[subpass].num_input_attachments);
		return subpasses[subpass].input_attachments[index];
	}

	bool has_depth(unsigned subpass) const
	{
		assert(subpass < subpasses.size());
		return
			subpasses[subpass].depth_stencil_attachment.attachment != VK_ATTACHMENT_UNUSED &&
			format_is_depth(depth_stencil);
	}

	bool has_stencil(unsigned subpass) const
	{
		assert(subpass < subpasses.size());
		return
			subpasses[subpass].depth_stencil_attachment.attachment != VK_ATTACHMENT_UNUSED &&
			format_is_stencil(depth_stencil);
	}

private:
	Device *device;
	VkRenderPass render_pass = VK_NULL_HANDLE;

	VkFormat color_attachments[VULKAN_NUM_ATTACHMENTS];
	VkFormat depth_stencil;
	unsigned num_color_attachments;

	void add_subpass(const SubpassInfo &info)
	{
		subpasses.push_back(info);
	}

	std::vector<SubpassInfo> subpasses;
};

class Framebuffer : public Cookie, public NoCopyNoMove
{
public:
	Framebuffer(Device *device, const RenderPass &rp, const RenderPassInfo &info);
	~Framebuffer();

	VkFramebuffer get_framebuffer() const
	{
		return framebuffer;
	}

	ImageView *get_attachment(unsigned index) const
	{
		assert(index < attachments.size());
		return attachments[index];
	}

	uint32_t get_width() const
	{
		return width;
	}

	uint32_t get_height() const
	{
		return height;
	}

	const RenderPass &get_render_pass() const
	{
		return render_pass;
	}

private:
	Device *device;
	VkFramebuffer framebuffer = VK_NULL_HANDLE;
	const RenderPass &render_pass;
	RenderPassInfo info;
	uint32_t width = 0;
	uint32_t height = 0;

	std::vector<ImageView *> attachments;
};

static const unsigned VULKAN_FRAMEBUFFER_RING_SIZE = 4;
class FramebufferAllocator
{
public:
	FramebufferAllocator(Device *device);
	Framebuffer &request_framebuffer(const RenderPassInfo &info);

	void begin_frame();
	void clear();

private:
	struct FramebufferNode : Util::TemporaryHashmapEnabled<FramebufferNode>,
	                         Util::IntrusiveListEnabled<FramebufferNode>,
	                         Framebuffer
	{
		FramebufferNode(Device *device, const RenderPass &rp, const RenderPassInfo &info)
		    : Framebuffer(device, rp, info)
		{
		}
	};

	Device *device;
	Util::TemporaryHashmap<FramebufferNode, VULKAN_FRAMEBUFFER_RING_SIZE, false> framebuffers;
};

class TransientAllocator
{
public:
	TransientAllocator(Device *device);
	ImageView &request_attachment(unsigned width, unsigned height, VkFormat format, unsigned index = 0);

	void begin_frame();
	void clear();

private:
	struct TransientNode : Util::TemporaryHashmapEnabled<TransientNode>, Util::IntrusiveListEnabled<TransientNode>
	{
		TransientNode(ImageHandle handle)
		    : handle(handle)
		{
		}

		ImageHandle handle;
	};

	Device *device;
	Util::TemporaryHashmap<TransientNode, VULKAN_FRAMEBUFFER_RING_SIZE, false> transients;
};
}
