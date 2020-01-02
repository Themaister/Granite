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

#include "vulkan_headers.hpp"
#include "libretro.h"
#include "libretro_vulkan.h"
#include "wsi.hpp"

// Various utilities to make writing a libretro Vulkan interface easier.
// The heavy lifting of WSI interfacing with the libretro frontend is implemented here.

namespace Granite
{
extern retro_log_printf_t libretro_log;

bool libretro_create_device(
		struct retro_vulkan_context *context,
		VkInstance instance,
		VkPhysicalDevice gpu,
		VkSurfaceKHR surface,
		PFN_vkGetInstanceProcAddr get_instance_proc_addr,
		const char **required_device_extensions,
		unsigned num_required_device_extensions,
		const char **required_device_layers,
		unsigned num_required_device_layers,
		const VkPhysicalDeviceFeatures *required_features);

// Takes effect next time the swapchain is recreated, on context_reset.
void libretro_set_swapchain_size(unsigned width, unsigned height);

// Used in get_application_info.
void libretro_set_application_info(const char *name, unsigned version);

// Called on context_reset HW_RENDER callback.
bool libretro_context_reset(retro_hw_render_interface_vulkan *vulkan, Vulkan::WSI &wsi);

// Called on context_destroy HW_RENDER callback.
void libretro_context_destroy(Vulkan::WSI *wsi);

// Called at the start of the frame.
void libretro_begin_frame(Vulkan::WSI &wsi, retro_usec_t frame_time);

// Called at the end of the frame.
void libretro_end_frame(retro_video_refresh_t video_cb, Vulkan::WSI &wsi);

// Called on retro_load_game.
bool libretro_load_game(retro_environment_t environ_cb);
// Called on retro_unload_game.
void libretro_unload_game();
}
