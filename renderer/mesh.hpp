#pragma once

#include "abstract_renderable.hpp"
#include "buffer.hpp"
#include "hashmap.hpp"
#include "material.hpp"
#include "aabb.hpp"
#include "render_queue.hpp"
#include "limits.hpp"

namespace Granite
{
enum class MeshDrawPipeline : unsigned
{
	Opaque,
	AlphaTest,
	AlphaBlend,
};

enum class MeshAttribute : unsigned
{
	Position = 0,
	UV = 1,
	Normal = 2,
	Tangent = 3,
	BoneIndex = 4,
	BoneWeights = 5,
	Count
};

struct MeshAttributeLayout
{
	VkFormat format = VK_FORMAT_UNDEFINED;
	uint32_t offset = 0;
};

struct StaticMeshVertex
{
	mat4 MVP;
	mat4 Normal;
	enum { max_instances = 256 };
};

struct StaticMeshFragment
{
	vec4 albedo;
	float emissive;
	float roughness;
	float metallic;
};

struct StaticMeshInfo : RenderInfo
{
	Vulkan::Buffer *vbo_position;
	Vulkan::Buffer *vbo_attributes;
	Vulkan::Buffer *ibo;
	Vulkan::ImageView *views[ecast(Material::Textures::Count)];
	Vulkan::Sampler *sampler;
	Vulkan::Program *program;

	MeshAttributeLayout attributes[ecast(MeshAttribute::Count)];

	StaticMeshVertex vertex;
	StaticMeshFragment fragment;

	uint32_t ibo_offset = 0;
	int32_t vertex_offset = 0;
	uint32_t count = 0;

	uint32_t position_stride;
	uint32_t attribute_stride;
	VkIndexType index_type;
};

namespace RenderFunctions
{
void static_mesh_render(Vulkan::CommandBuffer &cmd, const RenderInfo **render, unsigned instances);
}

struct StaticMesh : AbstractRenderable
{
	Vulkan::BufferHandle vbo_position;
	Vulkan::BufferHandle vbo_attributes;
	Vulkan::BufferHandle ibo;
	uint32_t ibo_offset = 0;
	int32_t vertex_offset = 0;
	uint32_t count = 0;
	uint32_t position_stride = 0;
	uint32_t attribute_stride = 0;
	VkIndexType index_type = VK_INDEX_TYPE_UINT16;

	MeshAttributeLayout attributes[ecast(MeshAttribute::Count)];

	MaterialHandle material;
	Util::Hash get_instance_key() const;
	MeshDrawPipeline pipeline;

	AABB static_aabb;

	void get_render_info(const RenderContext &context, RenderQueue &queue) override final;
};
}