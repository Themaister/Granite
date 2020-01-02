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

#pragma once

#include "device.hpp"

namespace Granite
{
Vulkan::ImageHandle convert_equirect_to_cube(Vulkan::Device &device, Vulkan::ImageView &view, float scale);
Vulkan::ImageHandle convert_cube_to_ibl_diffuse(Vulkan::Device &device, Vulkan::ImageView &view);
Vulkan::ImageHandle convert_cube_to_ibl_specular(Vulkan::Device &device, Vulkan::ImageView &view);

struct ImageReadback
{
	Vulkan::Fence fence;
	Vulkan::BufferHandle buffer;
	Vulkan::ImageCreateInfo create_info;
	Vulkan::TextureFormatLayout layout;
};
ImageReadback save_image_to_cpu_buffer(Vulkan::Device &device, const Vulkan::Image &image, Vulkan::CommandBuffer::Type type);
bool save_image_buffer_to_gtx(Vulkan::Device &device, ImageReadback &readback, const char *path);
}
