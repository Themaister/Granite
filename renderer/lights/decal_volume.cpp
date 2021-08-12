/* Copyright (c) 2021 Hans-Kristian Arntzen
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

namespace Granite
{
VolumetricDecal::VolumetricDecal()
{
	EVENT_MANAGER_REGISTER_LATCH(VolumetricDecal, on_device_created, on_device_destroyed, Vulkan::DeviceCreatedEvent);
}

void VolumetricDecal::set_decal(Vulkan::ImageHandle handle_)
{
	handle = std::move(handle_);
}

const Vulkan::ImageView *VolumetricDecal::get_decal_view() const
{
	return handle ? &handle->get_view() : nullptr;
}

const AABB &VolumetricDecal::get_static_aabb()
{
	static AABB aabb(vec3(-0.5f), vec3(0.5f));
	return aabb;
}

void VolumetricDecal::on_device_destroyed(const Vulkan::DeviceCreatedEvent &)
{
	handle.reset();
}

void VolumetricDecal::on_device_created(const Vulkan::DeviceCreatedEvent &e)
{
	auto info = Vulkan::ImageCreateInfo::immutable_3d_image(1, 1, 1, VK_FORMAT_R8_UNORM);
	const uint8_t one = 0x0f;
	Vulkan::ImageInitialData initial = { &one, 0, 0 };
	handle = e.get_device().create_image(info, &initial);
}

}