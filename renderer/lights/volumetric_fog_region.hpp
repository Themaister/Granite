/* Copyright (c) 2021-2024 Hans-Kristian Arntzen
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

#include "event.hpp"
#include "application_wsi_events.hpp"
#include "image.hpp"

namespace Granite
{
class VolumetricFogRegion : public EventHandler
{
public:
	VolumetricFogRegion();

	void set_volume(Vulkan::ImageHandle handle);
	const Vulkan::ImageView *get_volume_view() const;

	static const AABB &get_static_aabb();

private:
	void on_device_destroyed(const Vulkan::DeviceCreatedEvent &e);
	void on_device_created(const Vulkan::DeviceCreatedEvent &e);
	Vulkan::ImageHandle handle;
};
}
