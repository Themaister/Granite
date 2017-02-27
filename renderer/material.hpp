#pragma once

#include "image.hpp"
#include "math.hpp"
#include "intrusive.hpp"
#include "texture_manager.hpp"

namespace Granite
{
struct Material : public Util::IntrusivePtrEnabled<Material>
{
	enum class Textures : unsigned
	{
		Albedo = 0,
		Normal = 1,
		Roughness = 2,
		Metallic = 3,
		Count
	};
	Vulkan::Texture *textures[static_cast<unsigned>(Textures::Count)];
	vec4 albedo;
	float emissive = 0.0f;
	float roughness = 0.0f;
	float metallic = 0.0f;
};
using MaterialHandle = Util::IntrusivePtr<Material>;
}