/* Copyright (c) 2017-2022 Hans-Kristian Arntzen
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

#include "vulkan_headers.hpp"

namespace Vulkan
{
// FIXME: Also consider that we might have to flip X or Y w.r.t. dimensions,
// but that only matters for partial rendering ...
static inline bool surface_transform_swaps_xy(VkSurfaceTransformFlagBitsKHR transform)
{
	return (transform & (
			VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_90_BIT_KHR |
			VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_270_BIT_KHR |
			VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR |
			VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR)) != 0;
}

static inline void viewport_transform_xy(VkViewport &vp, VkSurfaceTransformFlagBitsKHR transform,
										 uint32_t fb_width, uint32_t fb_height)
{
	switch (transform)
	{
	case VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR:
	{
		float new_y = vp.x;
		float new_x = float(fb_width) - (vp.y + vp.height);
		vp.x = new_x;
		vp.y = new_y;
		std::swap(vp.width, vp.height);
		break;
	}

	case VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR:
	{
		// Untested. Cannot make Android trigger this mode.
		float new_left = float(fb_width) - (vp.x + vp.width);
		float new_top = float(fb_height) - (vp.y + vp.height);
		vp.x = new_left;
		vp.y = new_top;
		break;
	}

	case VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR:
	{
		float new_x = vp.y;
		float new_y = float(fb_height) - (vp.x + vp.width);
		vp.x = new_x;
		vp.y = new_y;
		std::swap(vp.width, vp.height);
		break;
	}

	default:
		break;
	}
}

static inline void rect2d_clip(VkRect2D &rect)
{
	if (rect.offset.x < 0)
	{
		rect.extent.width += rect.offset.x;
		rect.offset.x = 0;
	}

	if (rect.offset.y < 0)
	{
		rect.extent.height += rect.offset.y;
		rect.offset.y = 0;
	}

	rect.extent.width = std::min(rect.extent.width, 0x7fffffffu - rect.offset.x);
	rect.extent.height = std::min(rect.extent.height, 0x7fffffffu - rect.offset.y);
}

static inline void rect2d_transform_xy(VkRect2D &rect, VkSurfaceTransformFlagBitsKHR transform,
                                       uint32_t fb_width, uint32_t fb_height)
{
	switch (transform)
	{
	case VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR:
	{
		int new_y = rect.offset.x;
		int new_x = int(fb_width) - int(rect.offset.y + rect.extent.height);
		rect.offset = { new_x, new_y };
		std::swap(rect.extent.width, rect.extent.height);
		break;
	}

	case VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR:
	{
		// Untested. Cannot make Android trigger this mode.
		int new_left = int(fb_width) - int(rect.offset.x + rect.extent.width);
		int new_top = int(fb_height) - int(rect.offset.y + rect.extent.height);
		rect.offset = { new_left, new_top };
		break;
	}

	case VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR:
	{
		int new_x = rect.offset.y;
		int new_y = int(fb_height) - int(rect.offset.x + rect.extent.width);
		rect.offset = { new_x, new_y };
		std::swap(rect.extent.width, rect.extent.height);
		break;
	}

	default:
		break;
	}
}

static inline void build_prerotate_matrix_2x2(VkSurfaceTransformFlagBitsKHR pre_rotate, float mat[4])
{
	// TODO: HORIZONTAL_MIRROR.
	switch (pre_rotate)
	{
	default:
		mat[0] = 1.0f;
		mat[1] = 0.0f;
		mat[2] = 0.0f;
		mat[3] = 1.0f;
		break;

	case VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR:
		mat[0] = 0.0f;
		mat[1] = 1.0f;
		mat[2] = -1.0f;
		mat[3] = 0.0f;
		break;

	case VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR:
		mat[0] = 0.0f;
		mat[1] = -1.0f;
		mat[2] = 1.0f;
		mat[3] = 0.0f;
		break;

	case VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR:
		mat[0] = -1.0f;
		mat[1] = 0.0f;
		mat[2] = 0.0f;
		mat[3] = -1.0f;
		break;
	}
}
}
