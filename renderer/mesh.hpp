#pragma once

#include "buffer.hpp"
#include "hashmap.hpp"
#include "material.hpp"
#include "aabb.hpp"

namespace Granite
{
struct StaticMesh
{
	Vulkan::BufferHandle vbo;
	Vulkan::BufferHandle ibo;
	uint32_t ibo_offset = 0;
	int32_t vertex_offset = 0;
	uint32_t index_count = 0;

	enum class Attribute : unsigned
	{
		Position = 0,
		UV = 1,
		Normal = 2,
		Tangent = 3,
		Count
	};

	struct Layout
	{
		VkFormat format = VK_FORMAT_UNDEFINED;
		uint32_t offset = 0;
	};
	Layout attributes[static_cast<unsigned>(Attribute::Count)];

	MaterialHandle material;
	Util::Hash get_instance_key() const;

	AABB aabb;
};
}