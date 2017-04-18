#pragma once

#include <vector>
#include <stdint.h>
#include "mesh.hpp"
#include "enum_cast.hpp"

namespace Granite
{
struct Mesh
{
	// Attributes
	std::vector<uint8_t> positions;
	std::vector<uint8_t> attributes;
	uint32_t position_stride = 0;
	uint32_t attribute_stride = 0;
	MeshAttributeLayout attribute_layout[Util::ecast(MeshAttribute::Count)] = {};

	// Index buffer
	std::vector<uint8_t> indices;
	VkIndexType index_type;
	VkPrimitiveTopology topology;

	// Material
	uint32_t material_index = 0;

	// AABB
	Granite::AABB static_aabb;

	uint32_t count = 0;
};

struct MaterialInfo
{
	std::string base_color;
	std::string normal;
	std::string metallic_roughness;
	vec4 uniform_base_color;
	float uniform_metallic;
	float uniform_roughness;
};

}