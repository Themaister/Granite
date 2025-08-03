/* Copyright (c) 2017-2024 Hans-Kristian Arntzen
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

#pragma once

#include "cookie.hpp"
#include "format.hpp"
#include "vulkan_common.hpp"
#include "memory_allocator.hpp"
#include "vulkan_headers.hpp"
#include <algorithm>

namespace Vulkan
{
class Device;

static inline uint32_t image_num_miplevels(const VkExtent3D &extent)
{
	uint32_t size = std::max<uint32_t>(std::max<uint32_t>(extent.width, extent.height), extent.depth);
	return Util::floor_log2(size) + 1;
}

static inline VkFormatFeatureFlags image_usage_to_features(VkImageUsageFlags usage)
{
	VkFormatFeatureFlags flags = 0;
	if (usage & VK_IMAGE_USAGE_SAMPLED_BIT)
		flags |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
	if (usage & VK_IMAGE_USAGE_STORAGE_BIT)
		flags |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
	if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
		flags |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
	if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
		flags |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;

	return flags;
}

struct ImageInitialData
{
	const void *data;
	unsigned row_length;
	unsigned image_height;
};

enum ImageMiscFlagBits
{
	IMAGE_MISC_GENERATE_MIPS_BIT = 1 << 0,
	IMAGE_MISC_FORCE_ARRAY_BIT = 1 << 1,
	IMAGE_MISC_MUTABLE_SRGB_BIT = 1 << 2,
	IMAGE_MISC_CONCURRENT_QUEUE_GRAPHICS_BIT = 1 << 3,
	IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_COMPUTE_BIT = 1 << 4,
	IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_TRANSFER_BIT = 1 << 6,
	IMAGE_MISC_CONCURRENT_QUEUE_VIDEO_DECODE_BIT = 1 << 7,
	IMAGE_MISC_VERIFY_FORMAT_FEATURE_SAMPLED_LINEAR_FILTER_BIT = 1 << 8,
	IMAGE_MISC_LINEAR_IMAGE_IGNORE_DEVICE_LOCAL_BIT = 1 << 9,
	IMAGE_MISC_FORCE_NO_DEDICATED_BIT = 1 << 10,
	IMAGE_MISC_NO_DEFAULT_VIEWS_BIT = 1 << 11,
	IMAGE_MISC_EXTERNAL_MEMORY_BIT = 1 << 12,
	IMAGE_MISC_CONCURRENT_QUEUE_VIDEO_ENCODE_BIT = 1 << 13,
	IMAGE_MISC_CONCURRENT_QUEUE_VIDEO_DUPLEX =
		IMAGE_MISC_CONCURRENT_QUEUE_VIDEO_DECODE_BIT |
		IMAGE_MISC_CONCURRENT_QUEUE_VIDEO_ENCODE_BIT,
	IMAGE_MISC_CREATE_PER_MIP_LEVEL_VIEWS_BIT = 1 << 14
};
using ImageMiscFlags = uint32_t;

enum ImageViewMiscFlagBits
{
	IMAGE_VIEW_MISC_FORCE_ARRAY_BIT = 1 << 0
};
using ImageViewMiscFlags = uint32_t;

class Image;
class ImmutableYcbcrConversion;

struct ImageViewCreateInfo
{
	const Image *image = nullptr;
	VkFormat format = VK_FORMAT_UNDEFINED;
	unsigned base_level = 0;
	unsigned levels = VK_REMAINING_MIP_LEVELS;
	unsigned base_layer = 0;
	unsigned layers = VK_REMAINING_ARRAY_LAYERS;
	VkImageViewType view_type = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
	ImageViewMiscFlags misc = 0;
	VkComponentMapping swizzle = {
		VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
	};
	VkImageAspectFlags aspect = 0;
	const ImmutableYcbcrConversion *ycbcr_conversion = nullptr;
};

class ImageView;

struct ImageViewDeleter
{
	void operator()(ImageView *view);
};

class ImageView : public Util::IntrusivePtrEnabled<ImageView, ImageViewDeleter, HandleCounter>,
                  public Cookie, public InternalSyncEnabled
{
public:
	friend struct ImageViewDeleter;

	ImageView(Device *device, VkImageView view, const ImageViewCreateInfo &info, VkImageUsageFlags usage);

	~ImageView();

	struct CachedView
	{
		VkImageView view;
		CachedDescriptorPayload sampled;
		CachedDescriptorPayload storage;
	};

	void set_alt_views(VkImageView depth, VkImageView stencil)
	{
		VK_ASSERT(depth_view.view == VK_NULL_HANDLE);
		VK_ASSERT(stencil_view.view == VK_NULL_HANDLE);
		depth_view = { depth };
		stencil_view = { stencil };
	}

	void set_render_target_views(const std::vector<VkImageView> &views)
	{
		VK_ASSERT(render_target_views.empty());
		render_target_views.reserve(views.size());
		for (auto &v : views)
			render_target_views.push_back({ v });
	}

	void set_mip_views(const std::vector<VkImageView> &views)
	{
		VK_ASSERT(mip_views.empty());
		mip_views.reserve(views.size());
		for (auto &v : views)
			mip_views.push_back({ v });
	}

	void set_unorm_view(VkImageView view_)
	{
		VK_ASSERT(unorm_view.view == VK_NULL_HANDLE);
		unorm_view = { view_ };
	}

	void set_srgb_view(VkImageView view_)
	{
		VK_ASSERT(srgb_view.view == VK_NULL_HANDLE);
		srgb_view = { view_ };
	}

	// By default, gets a combined view which includes all aspects in the image.
	// This would be used mostly for render targets.
	const CachedView &get_view() const
	{
		return view;
	}

	const CachedView &get_render_target_view(unsigned layer) const;
	const CachedView &get_mip_view(unsigned level) const;

	// Gets an image view which only includes floating point domains.
	// Takes effect when we want to sample from an image which is Depth/Stencil,
	// but we only want to sample depth.
	const CachedView &get_float_view() const
	{
		return depth_view.view != VK_NULL_HANDLE ? depth_view : view;
	}

	// Gets an image view which only includes integer domains.
	// Takes effect when we want to sample from an image which is Depth/Stencil,
	// but we only want to sample stencil.
	const CachedView &get_integer_view() const
	{
		return stencil_view.view != VK_NULL_HANDLE ? stencil_view : view;
	}

	const CachedView &get_unorm_view() const
	{
		return unorm_view.view != VK_NULL_HANDLE ? unorm_view : view;
	}

	const CachedView &get_srgb_view() const
	{
		return srgb_view.view != VK_NULL_HANDLE ? srgb_view : view;
	}

	VkFormat get_format() const
	{
		return info.format;
	}

	const Image &get_image() const
	{
		return *info.image;
	}

	const ImageViewCreateInfo &get_create_info() const
	{
		return info;
	}

	unsigned get_view_width() const;
	unsigned get_view_height() const;
	unsigned get_view_depth() const;

	void rebuild_cached_descriptor_payloads(VkImageLayout sampled_layout);

private:
	Device *device;
	VkImageUsageFlags usage;
	CachedView view = {};
	std::vector<CachedView> render_target_views;
	std::vector<CachedView> mip_views;
	CachedView depth_view = {};
	CachedView stencil_view = {};
	CachedView unorm_view = {};
	CachedView srgb_view = {};
	ImageViewCreateInfo info;

	void free_cached_view(CachedView &cached);
	void free_cached_view_payloads(CachedView &cached);
	void rebuild_cached_descriptor_payloads(CachedView &view, VkImageLayout sampled_layout, VkImageUsageFlags usage);
};

using ImageViewHandle = Util::IntrusivePtr<ImageView>;

enum class ImageDomain
{
	Physical,
	Transient,
	LinearHostCached,
	LinearHost,
	HostCopy
};

struct ImageCreateInfo
{
	ImageDomain domain = ImageDomain::Physical;
	unsigned width = 0;
	unsigned height = 0;
	unsigned depth = 1;
	unsigned levels = 1;
	VkFormat format = VK_FORMAT_UNDEFINED;
	VkImageType type = VK_IMAGE_TYPE_2D;
	unsigned layers = 1;
	VkImageUsageFlags usage = 0;
	VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
	VkImageCreateFlags flags = 0;
	ImageMiscFlags misc = 0;
	VkImageLayout initial_layout = VK_IMAGE_LAYOUT_GENERAL;
	VkComponentMapping swizzle = {
		VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
	};
	const DeviceAllocation **memory_aliases = nullptr;
	unsigned num_memory_aliases = 0;
	const ImmutableYcbcrConversion *ycbcr_conversion = nullptr;
	void *pnext = nullptr;
	ExternalHandle external;

	static ImageCreateInfo immutable_image(const TextureFormatLayout &layout)
	{
		Vulkan::ImageCreateInfo info;
		info.width = layout.get_width();
		info.height = layout.get_height();
		info.type = layout.get_image_type();
		info.depth = layout.get_depth();
		info.format = layout.get_format();
		info.layers = layout.get_layers();
		info.levels = layout.get_levels();
		info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
		info.initial_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		info.samples = VK_SAMPLE_COUNT_1_BIT;
		info.domain = ImageDomain::Physical;
		return info;
	}

	static ImageCreateInfo immutable_2d_image(unsigned width, unsigned height, VkFormat format, bool mipmapped = false)
	{
		ImageCreateInfo info;
		info.width = width;
		info.height = height;
		info.depth = 1;
		info.levels = mipmapped ? 0u : 1u;
		info.format = format;
		info.type = VK_IMAGE_TYPE_2D;
		info.layers = 1;
		info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
		info.samples = VK_SAMPLE_COUNT_1_BIT;
		info.flags = 0;
		info.misc = mipmapped ? unsigned(IMAGE_MISC_GENERATE_MIPS_BIT) : 0u;
		info.initial_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		return info;
	}

	static ImageCreateInfo
	immutable_3d_image(unsigned width, unsigned height, unsigned depth, VkFormat format, bool mipmapped = false)
	{
		ImageCreateInfo info = immutable_2d_image(width, height, format, mipmapped);
		info.depth = depth;
		info.type = VK_IMAGE_TYPE_3D;
		return info;
	}

	static ImageCreateInfo render_target(unsigned width, unsigned height, VkFormat format)
	{
		ImageCreateInfo info;
		info.width = width;
		info.height = height;
		info.depth = 1;
		info.levels = 1;
		info.format = format;
		info.type = VK_IMAGE_TYPE_2D;
		info.layers = 1;
		info.usage = (format_has_depth_or_stencil_aspect(format) ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT :
		              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) |
		             VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

		info.samples = VK_SAMPLE_COUNT_1_BIT;
		info.flags = 0;
		info.misc = 0;
		info.initial_layout = format_has_depth_or_stencil_aspect(format) ?
		                      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL :
		                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		return info;
	}

	static ImageCreateInfo transient_render_target(unsigned width, unsigned height, VkFormat format)
	{
		ImageCreateInfo info;
		info.domain = ImageDomain::Transient;
		info.width = width;
		info.height = height;
		info.depth = 1;
		info.levels = 1;
		info.format = format;
		info.type = VK_IMAGE_TYPE_2D;
		info.layers = 1;
		info.usage = (format_has_depth_or_stencil_aspect(format) ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT :
		              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) |
		             VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
		info.samples = VK_SAMPLE_COUNT_1_BIT;
		info.flags = 0;
		info.misc = 0;
		info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		return info;
	}

	static uint32_t compute_view_formats(const ImageCreateInfo &info, VkFormat *formats)
	{
		if ((info.misc & IMAGE_MISC_MUTABLE_SRGB_BIT) == 0)
			return 0;

		switch (info.format)
		{
		case VK_FORMAT_R8G8B8A8_UNORM:
		case VK_FORMAT_R8G8B8A8_SRGB:
			formats[0] = VK_FORMAT_R8G8B8A8_UNORM;
			formats[1] = VK_FORMAT_R8G8B8A8_SRGB;
			return 2;

		case VK_FORMAT_B8G8R8A8_UNORM:
		case VK_FORMAT_B8G8R8A8_SRGB:
			formats[0] = VK_FORMAT_B8G8R8A8_UNORM;
			formats[1] = VK_FORMAT_B8G8R8A8_SRGB;
			return 2;

		case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
		case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
			formats[0] = VK_FORMAT_A8B8G8R8_UNORM_PACK32;
			formats[1] = VK_FORMAT_A8B8G8R8_SRGB_PACK32;
			return 2;

		default:
			return 0;
		}
	}
};

class Image;

struct ImageDeleter
{
	void operator()(Image *image);
};

enum class Layout
{
	Optimal,
	General
};

class Image : public Util::IntrusivePtrEnabled<Image, ImageDeleter, HandleCounter>,
              public Cookie, public InternalSyncEnabled
{
public:
	friend struct ImageDeleter;

	~Image();

	Image(Image &&) = delete;

	Image &operator=(Image &&) = delete;

	const ImageView &get_view() const
	{
		VK_ASSERT(view);
		return *view;
	}

	ImageView &get_view()
	{
		VK_ASSERT(view);
		return *view;
	}

	VkImage get_image() const
	{
		return image;
	}

	VkFormat get_format() const
	{
		return create_info.format;
	}

	uint32_t get_width(uint32_t lod = 0) const
	{
		return std::max<uint32_t>(1u, create_info.width >> lod);
	}

	uint32_t get_height(uint32_t lod = 0) const
	{
		return std::max<uint32_t>(1u, create_info.height >> lod);
	}

	uint32_t get_depth(uint32_t lod = 0) const
	{
		return std::max<uint32_t>(1u, create_info.depth >> lod);
	}

	const ImageCreateInfo &get_create_info() const
	{
		return create_info;
	}

	VkImageLayout get_layout(VkImageLayout optimal) const
	{
		return layout_type == Layout::Optimal ? optimal : VK_IMAGE_LAYOUT_GENERAL;
	}

	Layout get_layout_type() const
	{
		return layout_type;
	}

	void set_layout(Layout layout)
	{
		if (layout_type != layout && view)
		{
			view->rebuild_cached_descriptor_payloads(
					layout == Layout::Optimal ?
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL :
					VK_IMAGE_LAYOUT_GENERAL);
		}

		layout_type = layout;
	}

	bool is_swapchain_image() const
	{
		return swapchain_layout != VK_IMAGE_LAYOUT_UNDEFINED;
	}

	VkImageLayout get_swapchain_layout() const
	{
		return swapchain_layout;
	}

	void set_swapchain_layout(VkImageLayout layout)
	{
		swapchain_layout = layout;
	}

	const DeviceAllocation &get_allocation() const
	{
		return alloc;
	}

	void disown_image();
	void disown_memory_allocation();
	DeviceAllocation take_allocation_ownership();

	void set_surface_transform(VkSurfaceTransformFlagBitsKHR transform)
	{
		surface_transform = transform;
		if (transform != VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
		{
			const VkImageUsageFlags safe_usage_flags =
					VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT |
					VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
					VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
					VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;

			if ((create_info.usage & ~safe_usage_flags) != 0)
			{
				LOGW("Using surface transform for non-pure render target image (usage: %u). This can lead to weird results.\n",
				     create_info.usage);
			}
		}
	}

	VkSurfaceTransformFlagBitsKHR get_surface_transform() const
	{
		return surface_transform;
	}

	ExternalHandle export_handle();

private:
	friend class Util::ObjectPool<Image>;

	Image(Device *device, VkImage image, VkImageView default_view, const DeviceAllocation &alloc,
	      const ImageCreateInfo &info, VkImageViewType view_type);

	Device *device;
	VkImage image;
	ImageViewHandle view;
	DeviceAllocation alloc;
	ImageCreateInfo create_info;

	Layout layout_type = Layout::Optimal;
	VkImageLayout swapchain_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkSurfaceTransformFlagBitsKHR surface_transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	bool owns_image = true;
	bool owns_memory_allocation = true;
};

using ImageHandle = Util::IntrusivePtr<Image>;

class LinearHostImage;
struct LinearHostImageDeleter
{
	void operator()(LinearHostImage *image);
};

class Buffer;

enum LinearHostImageCreateInfoFlagBits
{
	LINEAR_HOST_IMAGE_HOST_CACHED_BIT = 1 << 0,
	LINEAR_HOST_IMAGE_REQUIRE_LINEAR_FILTER_BIT = 1 << 1,
	LINEAR_HOST_IMAGE_IGNORE_DEVICE_LOCAL_BIT = 1 << 2
};
using LinearHostImageCreateInfoFlags = uint32_t;

struct LinearHostImageCreateInfo
{
	unsigned width = 0;
	unsigned height = 0;
	VkFormat format = VK_FORMAT_UNDEFINED;
	VkImageUsageFlags usage = 0;
	VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	LinearHostImageCreateInfoFlags flags = 0;
};

// Special image type which supports direct CPU mapping.
// Useful optimization for UMA implementations of Vulkan where we don't necessarily need
// to perform staging copies. It gracefully falls back to staging buffer as needed.
// Only usage flag SAMPLED_BIT is currently supported.
class LinearHostImage : public Util::IntrusivePtrEnabled<LinearHostImage, LinearHostImageDeleter, HandleCounter>
{
public:
	friend struct LinearHostImageDeleter;

	size_t get_row_pitch_bytes() const;
	size_t get_offset() const;
	const ImageView &get_view() const;
	const Image &get_image() const;
	const DeviceAllocation &get_host_visible_allocation() const;
	const Buffer &get_host_visible_buffer() const;
	bool need_staging_copy() const;
	VkPipelineStageFlags2 get_used_pipeline_stages() const;

private:
	friend class Util::ObjectPool<LinearHostImage>;
	LinearHostImage(Device *device, ImageHandle gpu_image, Util::IntrusivePtr<Buffer> cpu_image,
	                VkPipelineStageFlags2 stages);
	Device *device;
	ImageHandle gpu_image;
	Util::IntrusivePtr<Buffer> cpu_image;
	VkPipelineStageFlags2 stages;
	size_t row_pitch;
	size_t row_offset;
};
using LinearHostImageHandle = Util::IntrusivePtr<LinearHostImage>;
}
