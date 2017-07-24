/* Copyright (c) 2017 Hans-Kristian Arntzen
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

#include "image.hpp"
#include "math.hpp"
#include "intrusive.hpp"
#include "texture_manager.hpp"
#include "enum_cast.hpp"
#include "sampler.hpp"
#include "abstract_renderable.hpp"
#include "hashmap.hpp"

namespace Granite
{
struct Material : public Util::IntrusivePtrEnabled<Material>
{
	virtual ~Material() = default;

	enum class Textures : unsigned
	{
		BaseColor = 0,
		Normal = 1,
		MetallicRoughness = 2,
		Count
	};

	Vulkan::Texture *textures[Util::ecast(Textures::Count)];
	vec4 base_color;
	vec3 emissive;
	float roughness = 1.0f;
	float metallic = 1.0f;
	float lod_bias = 0.0f;
	DrawPipeline pipeline = DrawPipeline::Opaque;
	Vulkan::StockSampler sampler = Vulkan::StockSampler::TrilinearWrap;
	bool two_sided = false;

	void bake_hash()
	{
		Util::Hasher h;
		for (auto &tex : textures)
			h.pointer(tex);
		for (unsigned i = 0; i < 4; i++)
			h.f32(base_color[i]);
		for (unsigned i = 0; i < 3; i++)
			h.f32(emissive[i]);
		h.f32(roughness);
		h.f32(metallic);
		h.f32(lod_bias);
		h.u32(Util::ecast(pipeline));
		h.u32(Util::ecast(sampler));
		h.u32(two_sided);
		hash = h.get();
	}

	uint64_t get_hash() const
	{
		assert(hash);
		return hash;
	}

private:
	uint64_t hash = 0;
};

enum MaterialTextureFlagBits
{
	MATERIAL_TEXTURE_BASE_COLOR_BIT = 1u << Util::ecast(Material::Textures::BaseColor),
	MATERIAL_TEXTURE_NORMAL_BIT = 1u << Util::ecast(Material::Textures::Normal),
	MATERIAL_TEXTURE_METALLIC_ROUGHNESS_BIT = 1u << Util::ecast(Material::Textures::MetallicRoughness)
};

using MaterialHandle = Util::IntrusivePtr<Material>;
}