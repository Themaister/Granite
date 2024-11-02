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

#include "decal_volume.hpp"
#include "device.hpp"
#include "resource_manager.hpp"
#include <random>

namespace Granite
{
VolumetricDecal::VolumetricDecal()
{
	tex = GRANITE_ASSET_MANAGER()->register_asset(*GRANITE_FILESYSTEM(),
	                                              "builtin://textures/decal.png",
	                                              AssetClass::ImageColor);
}

const Vulkan::ImageView *VolumetricDecal::get_decal_view(Vulkan::Device &device) const
{
	return device.get_resource_manager().get_image_view(tex);
}

const AABB &VolumetricDecal::get_static_aabb()
{
	static AABB aabb(vec3(-0.5f), vec3(0.5f));
	return aabb;
}
}
