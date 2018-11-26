/* Copyright (c) 2017-2018 Hans-Kristian Arntzen
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

using namespace std;

namespace Vulkan
{

ImageView::ImageView(Device *device, VkImageView view, const ImageViewCreateInfo &info)
    : Cookie(device)
    , device(device)
    , view(view)
    , info(info)
{
}

VkImageView ImageView::get_render_target_view(unsigned layer) const
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

ImageView::~ImageView()
{
	if (internal_sync)
	{
		device->destroy_image_view_nolock(view);
		if (depth_view != VK_NULL_HANDLE)
			device->destroy_image_view_nolock(depth_view);
		if (stencil_view != VK_NULL_HANDLE)
			device->destroy_image_view_nolock(stencil_view);
		if (unorm_view != VK_NULL_HANDLE)
			device->destroy_image_view_nolock(unorm_view);
		if (srgb_view != VK_NULL_HANDLE)
			device->destroy_image_view_nolock(srgb_view);

		for (auto &view : render_target_views)
			device->destroy_image_view_nolock(view);
	}
	else
	{
		device->destroy_image_view(view);
		if (depth_view != VK_NULL_HANDLE)
			device->destroy_image_view(depth_view);
		if (stencil_view != VK_NULL_HANDLE)
			device->destroy_image_view(stencil_view);
		if (unorm_view != VK_NULL_HANDLE)
			device->destroy_image_view(unorm_view);
		if (srgb_view != VK_NULL_HANDLE)
			device->destroy_image_view(srgb_view);

		for (auto &view : render_target_views)
			device->destroy_image_view(view);
	}
}

Image::Image(Device *device, VkImage image, VkImageView default_view, const DeviceAllocation &alloc,
             const ImageCreateInfo &create_info)
    : Cookie(device)
    , device(device)
    , image(image)
    , alloc(alloc)
    , create_info(create_info)
{
	if (default_view != VK_NULL_HANDLE)
	{
		ImageViewCreateInfo info;
		info.image = this;
		info.format = create_info.format;
		info.base_level = 0;
		info.levels = create_info.levels;
		info.base_layer = 0;
		info.layers = create_info.layers;
		view = ImageViewHandle(device->handle_pool.image_views.allocate(device, default_view, info));
	}
}

Image::~Image()
{
	if (alloc.get_memory())
	{
		if (internal_sync)
		{
			device->destroy_image_nolock(image);
			device->free_memory_nolock(alloc);
		}
		else
		{
			device->destroy_image(image);
			device->free_memory(alloc);
		}
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

VkPipelineStageFlags LinearHostImage::get_used_pipeline_stages() const
{
	return stages;
}

LinearHostImage::LinearHostImage(Device *device, ImageHandle gpu_image_, BufferHandle cpu_image_, VkPipelineStageFlags stages)
	: device(device), gpu_image(move(gpu_image_)), cpu_image(move(cpu_image_)), stages(stages)
{
	if (gpu_image->get_create_info().domain == ImageDomain::LinearHostCached ||
	    gpu_image->get_create_info().domain == ImageDomain::LinearHost)
	{
		VkImageSubresource sub = {};
		sub.aspectMask = format_to_aspect_mask(gpu_image->get_format());
		VkSubresourceLayout layout;
		vkGetImageSubresourceLayout(device->get_device(), gpu_image->get_image(), &sub, &layout);
		row_pitch = layout.rowPitch;
		row_offset = layout.offset;
	}
	else
	{
		row_pitch = gpu_image->get_width() * TextureFormatLayout::format_block_size(gpu_image->get_format());
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
