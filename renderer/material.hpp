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