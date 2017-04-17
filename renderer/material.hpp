#pragma once

#include "image.hpp"
#include "math.hpp"
#include "intrusive.hpp"
#include "texture_manager.hpp"
#include "enum_cast.hpp"

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
	float emissive = 0.0f;
	float roughness = 0.0f;
	float metallic = 0.0f;
};
using MaterialHandle = Util::IntrusivePtr<Material>;
}