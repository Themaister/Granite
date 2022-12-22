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

#include "material_util.hpp"
#include "device.hpp"
#include "texture_manager.hpp"
#include <string.h>

using namespace Vulkan;

namespace Granite
{
StockMaterials &StockMaterials::get()
{
	static StockMaterials stock;
	return stock;
}

StockMaterials::StockMaterials()
{
	checkerboard = Util::make_handle<Material>();
	checkerboard->textures[Util::ecast(Material::Textures::BaseColor)] =
			GRANITE_ASSET_MANAGER()->register_image_resource(*GRANITE_FILESYSTEM(),
			                                                 "builtin://textures/checkerboard.png",
			                                                 ImageClass::Color);
	checkerboard->emissive = vec3(0.0f);
	checkerboard->metallic = 0.0f;
	checkerboard->roughness = 1.0f;
	checkerboard->base_color = vec4(1.0f);
	checkerboard->bake();
}

MaterialHandle StockMaterials::get_checkerboard()
{
	return checkerboard;
}
}
