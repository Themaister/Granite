/* Copyright (c) 2021-2022 Hans-Kristian Arntzen
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

#include "decal_volume.hpp"
#include "device.hpp"
#include <random>

namespace Granite
{
VolumetricDecal::VolumetricDecal()
{
	EVENT_MANAGER_REGISTER_LATCH(VolumetricDecal, on_device_created, on_device_destroyed, Vulkan::DeviceCreatedEvent);
}

const Vulkan::ImageView *VolumetricDecal::get_decal_view() const
{
	return tex ? &tex->get_image()->get_view() : nullptr;
}

const AABB &VolumetricDecal::get_static_aabb()
{
	static AABB aabb(vec3(-0.5f), vec3(0.5f));
	return aabb;
}

void VolumetricDecal::on_device_destroyed(const Vulkan::DeviceCreatedEvent &)
{
	tex = nullptr;
}

void VolumetricDecal::on_device_created(const Vulkan::DeviceCreatedEvent &e)
{
	tex = e.get_device().get_texture_manager().request_texture("builtin://textures/decal.png", VK_FORMAT_R8G8B8A8_SRGB);
}
}
