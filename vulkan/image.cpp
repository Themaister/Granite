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

#include "image.hpp"
#include "device.hpp"
#include "buffer.hpp"

namespace Vulkan
{
ImageView::ImageView(Device *device_, VkImageView view_, const ImageViewCreateInfo &info_, VkImageUsageFlags usage_)
    : Cookie(device_)
    , device(device_)
	, usage(usage_)
    , view({ view_ })
    , info(info_)
{
}

const ImageView::CachedView &ImageView::get_render_target_view(unsigned layer) const
{
	// Transient images just have one layer.
	if (info.image->get_create_info().domain == ImageDomain::Transient)
		return view;

	VK_ASSERT(layer < get_create_info().layers);

	if (render_target_views.empty())
		return view;
	else
	{
		VK_ASSERT(layer < render_target_views.size());
		return render_target_views[layer];
	}
}

const ImageView::CachedView &ImageView::get_mip_view(unsigned level) const
{
	VK_ASSERT(level < get_create_info().levels);

	if (mip_views.empty())
		return view;
	else
	{
		VK_ASSERT(level < mip_views.size());
		return mip_views[level];
	}
}

void ImageView::free_cached_view_payloads(CachedView &cached)
{
	if (internal_sync)
	{
		if (cached.sampled)
			device->free_cached_descriptor_payload_nolock(cached.sampled);
		if (cached.storage)
			device->free_cached_descriptor_payload_nolock(cached.storage);
	}
	else
	{
		if (cached.sampled)
			device->free_cached_descriptor_payload(cached.sampled);
		if (cached.storage)
			device->free_cached_descriptor_payload(cached.storage);
	}

	cached.sampled = {};
	cached.storage = {};
}

void ImageView::free_cached_view(CachedView &cached)
{
	if (internal_sync)
	{
		if (cached.view != VK_NULL_HANDLE)
			device->destroy_image_view_nolock(cached.view);
	}
	else
	{
		if (cached.view != VK_NULL_HANDLE)
			device->destroy_image_view(cached.view);
	}

	free_cached_view_payloads(cached);
}

ImageView::~ImageView()
{
	free_cached_view(view);
	free_cached_view(depth_view);
	free_cached_view(stencil_view);
	free_cached_view(unorm_view);
	free_cached_view(srgb_view);
	for (auto &v : render_target_views)
		free_cached_view(v);
	for (auto &v : mip_views)
		free_cached_view(v);
}

unsigned ImageView::get_view_width() const
{
	unsigned width = info.image->get_width(info.base_level);

	if (info.aspect == VK_IMAGE_ASPECT_PLANE_1_BIT || info.aspect == VK_IMAGE_ASPECT_PLANE_2_BIT)
	{
		unsigned h = 0;
		format_ycbcr_downsample_dimensions(info.image->get_format(), info.aspect, width, h);
	}

	return width;
}

unsigned ImageView::get_view_height() const
{
	unsigned height = info.image->get_height(info.base_level);

	if (info.aspect == VK_IMAGE_ASPECT_PLANE_1_BIT || info.aspect == VK_IMAGE_ASPECT_PLANE_2_BIT)
	{
		unsigned w = 0;
		format_ycbcr_downsample_dimensions(info.image->get_format(), info.aspect, w, height);
	}

	return height;
}

unsigned ImageView::get_view_depth() const
{
	return info.image->get_depth(info.base_level);
}

void ImageView::rebuild_cached_descriptor_payloads(CachedView &v, VkImageLayout sampled_layout,
                                                   VkImageUsageFlags specific_usage)
{
	VkDescriptorGetInfoEXT get_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT };
	VkDescriptorImageInfo image_info = {};
	image_info.imageView = v.view;
	free_cached_view_payloads(v);

	if (v.view == VK_NULL_HANDLE)
		return;

	if (specific_usage & VK_IMAGE_USAGE_SAMPLED_BIT)
	{
		image_info.imageLayout = sampled_layout;

		get_info.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		get_info.data.pSampledImage = &image_info;

		v.sampled = device->managers.descriptor_buffer.alloc_sampled_image();
		device->get_device_table().vkGetDescriptorEXT(
				device->get_device(), &get_info,
				device->get_device_features().descriptor_buffer_properties.sampledImageDescriptorSize,
				v.sampled.ptr);
	}

	if (specific_usage & VK_IMAGE_USAGE_STORAGE_BIT)
	{
		image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

		get_info.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		get_info.data.pStorageImage = &image_info;

		v.storage = device->managers.descriptor_buffer.alloc_storage_image();
		device->get_device_table().vkGetDescriptorEXT(
				device->get_device(), &get_info,
				device->get_device_features().descriptor_buffer_properties.storageImageDescriptorSize,
				v.storage.ptr);
	}
}

void ImageView::rebuild_cached_descriptor_payloads(VkImageLayout sampled_layout)
{
	rebuild_cached_descriptor_payloads(view, sampled_layout, usage);
	rebuild_cached_descriptor_payloads(depth_view, sampled_layout, usage);
	rebuild_cached_descriptor_payloads(stencil_view, sampled_layout, usage);
	rebuild_cached_descriptor_payloads(unorm_view, sampled_layout, info.image->get_create_info().usage);
	rebuild_cached_descriptor_payloads(srgb_view, sampled_layout, usage & ~VK_IMAGE_USAGE_STORAGE_BIT);
	for (auto &l : mip_views)
		rebuild_cached_descriptor_payloads(l, sampled_layout, usage & ~VK_IMAGE_USAGE_SAMPLED_BIT);
}

Image::Image(Device *device_, VkImage image_, VkImageView default_view, const DeviceAllocation &alloc_,
             const ImageCreateInfo &create_info_, VkImageViewType view_type)
    : Cookie(device_)
    , device(device_)
    , image(image_)
    , alloc(alloc_)
    , create_info(create_info_)
{
	if (default_view != VK_NULL_HANDLE)
	{
		ImageViewCreateInfo info;
		info.image = this;
		info.view_type = view_type;
		info.format = create_info.format;
		info.base_level = 0;
		info.levels = create_info.levels;
		info.base_layer = 0;
		info.layers = create_info.layers;

		VkImageUsageFlags usage = create_info.usage;
		VkFormatProperties3 props3 = { VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3 };
		device->get_format_properties(create_info.format, &props3);
		if ((props3.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) == 0)
			usage &= ~VK_IMAGE_USAGE_SAMPLED_BIT;
		if ((props3.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) == 0)
			usage &= ~VK_IMAGE_USAGE_STORAGE_BIT;

		view = ImageViewHandle(device->handle_pool.image_views.allocate(device, default_view, info, usage));
	}
}

DeviceAllocation Image::take_allocation_ownership()
{
	DeviceAllocation ret = {};
	std::swap(ret, alloc);
	return ret;
}

ExternalHandle Image::export_handle()
{
	return alloc.export_handle(*device);
}

void Image::disown_image()
{
	owns_image = false;
}

void Image::disown_memory_allocation()
{
	owns_memory_allocation = false;
}

Image::~Image()
{
	if (owns_image)
	{
		if (internal_sync)
			device->destroy_image_nolock(image);
		else
			device->destroy_image(image);
	}

	if (alloc.get_memory() && owns_memory_allocation)
	{
		if (internal_sync)
			device->free_memory_nolock(alloc);
		else
			device->free_memory(alloc);
	}
}

const Buffer &LinearHostImage::get_host_visible_buffer() const
{
	return *cpu_image;
}

bool LinearHostImage::need_staging_copy() const
{
	return gpu_image->get_create_info().domain != ImageDomain::LinearHostCached &&
	       gpu_image->get_create_info().domain != ImageDomain::LinearHost;
}

const DeviceAllocation &LinearHostImage::get_host_visible_allocation() const
{
	return need_staging_copy() ? cpu_image->get_allocation() : gpu_image->get_allocation();
}

const ImageView &LinearHostImage::get_view() const
{
	return gpu_image->get_view();
}

const Image &LinearHostImage::get_image() const
{
	return *gpu_image;
}

size_t LinearHostImage::get_offset() const
{
	return row_offset;
}

size_t LinearHostImage::get_row_pitch_bytes() const
{
	return row_pitch;
}

VkPipelineStageFlags2 LinearHostImage::get_used_pipeline_stages() const
{
	return stages;
}

LinearHostImage::LinearHostImage(Device *device_, ImageHandle gpu_image_, BufferHandle cpu_image_, VkPipelineStageFlags2 stages_)
	: device(device_), gpu_image(std::move(gpu_image_)), cpu_image(std::move(cpu_image_)), stages(stages_)
{
	if (gpu_image->get_create_info().domain == ImageDomain::LinearHostCached ||
	    gpu_image->get_create_info().domain == ImageDomain::LinearHost)
	{
		VkImageSubresource sub = {};
		sub.aspectMask = format_to_aspect_mask(gpu_image->get_format());
		VkSubresourceLayout layout;

		auto &table = device_->get_device_table();
		table.vkGetImageSubresourceLayout(device->get_device(), gpu_image->get_image(), &sub, &layout);
		row_pitch = layout.rowPitch;
		row_offset = layout.offset;
	}
	else
	{
		row_pitch = gpu_image->get_width() * TextureFormatLayout::format_block_size(gpu_image->get_format(),
		                                                                            format_to_aspect_mask(gpu_image->get_format()));
		row_offset = 0;
	}
}

void ImageViewDeleter::operator()(ImageView *view)
{
	view->device->handle_pool.image_views.free(view);
}

void ImageDeleter::operator()(Image *image)
{
	image->device->handle_pool.images.free(image);
}

void LinearHostImageDeleter::operator()(LinearHostImage *image)
{
	image->device->handle_pool.linear_images.free(image);
}
}
