/* Copyright (c) 2017-2024 Hans-Kristian Arntzen
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
#include "asset_manager.hpp"
#include "enum_cast.hpp"
#include "sampler.hpp"
#include "abstract_renderable.hpp"
#include "hash.hpp"

namespace Granite
{
enum class TextureKind : unsigned
{
	BaseColor = 0,
	Normal = 1,
	MetallicRoughness = 2,
	Occlusion = 3,
	Emissive = 4,
	Count
};

struct MaterialInfo
{
	std::string paths[Util::ecast(TextureKind::Count)];
	vec4 uniform_base_color = vec4(1.0f);
	vec3 uniform_emissive_color = vec3(0.0f);
	float uniform_metallic = 1.0f;
	float uniform_roughness = 1.0f;
	float normal_scale = 1.0f;
	DrawPipeline pipeline = DrawPipeline::Opaque;
	Vulkan::StockSampler sampler = Vulkan::StockSampler::TrilinearWrap;
	uint32_t shader_variant = 0;
	bool two_sided = false;
};

enum MaterialTextureFlagBits
{
	MATERIAL_TEXTURE_BASE_COLOR_BIT = 1u << Util::ecast(TextureKind::BaseColor),
	MATERIAL_TEXTURE_NORMAL_BIT = 1u << Util::ecast(TextureKind::Normal),
	MATERIAL_TEXTURE_METALLIC_ROUGHNESS_BIT = 1u << Util::ecast(TextureKind::MetallicRoughness),
	MATERIAL_TEXTURE_OCCLUSION_BIT = 1u << Util::ecast(TextureKind::Occlusion),
	MATERIAL_TEXTURE_EMISSIVE_BIT = 1u << Util::ecast(TextureKind::Emissive),
	MATERIAL_EMISSIVE_BIT = 1u << 5,
	MATERIAL_EMISSIVE_REFRACTION_BIT = 1u << 6,
	MATERIAL_EMISSIVE_REFLECTION_BIT = 1u << 7
};

enum MaterialShaderVariantFlagBits
{
	MATERIAL_SHADER_VARIANT_BANDLIMITED_PIXEL_BIT = 1 << 0
};

struct Material
{
	Material() = default;
	explicit Material(MaterialInfo info_)
	{
		set_info(std::move(info_));
	}

	void set_info(MaterialInfo info_)
	{
		info = std::move(info_);

		static const AssetClass image_classes[] = {
			AssetClass::ImageColor,
			AssetClass::ImageNormal,
			AssetClass::ImageMetallicRoughness,
			AssetClass::ImageColor,
			AssetClass::ImageColor,
		};

		for (unsigned i = 0; i < Util::ecast(TextureKind::Count); i++)
		{
			if (!info.paths[i].empty())
			{
				textures[i] = GRANITE_ASSET_MANAGER()->register_asset(
						*GRANITE_FILESYSTEM(), info.paths[i], image_classes[i]);
			}
		}

		bake();
	}

	uint64_t get_hash() const
	{
		assert(hash);
		return hash;
	}

	const MaterialInfo &get_info() const
	{
		return info;
	}

	AssetID textures[Util::ecast(TextureKind::Count)];
	bool needs_emissive = false;
	uint32_t shader_variant = 0;

private:
	MaterialInfo info;
	uint64_t hash = 0;

	void bake()
	{
		Util::Hasher h;
		for (auto &tex : textures)
			h.u32(tex.id);
		for (unsigned i = 0; i < 4; i++)
			h.f32(info.uniform_base_color[i]);
		for (unsigned i = 0; i < 3; i++)
			h.f32(info.uniform_emissive_color[i]);
		h.f32(info.uniform_roughness);
		h.f32(info.uniform_metallic);
		h.f32(info.normal_scale);
		h.u32(Util::ecast(info.pipeline));
		h.u32(Util::ecast(info.sampler));
		h.u32(info.two_sided);
		h.u32(info.shader_variant);
		hash = h.get();
		needs_emissive = any(notEqual(info.uniform_emissive_color, vec3(0.0f)));
	}
};
}
