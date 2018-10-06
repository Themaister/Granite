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

void ImageViewDeleter::operator()(Vulkan::ImageView *view)
{
	view->device->handle_pool.image_views.free(view);
}

void ImageDeleter::operator()(Vulkan::Image *image)
{
	image->device->handle_pool.images.free(image);
}
}
