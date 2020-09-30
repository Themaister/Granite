/* Copyright (c) 2017-2020 Hans-Kristian Arntzen
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

#include "abstract_renderable.hpp"
#include "buffer.hpp"
#include "hash.hpp"
#include "material.hpp"
#include "aabb.hpp"
#include "render_queue.hpp"
#include "limits.hpp"

namespace Granite
{
struct RenderQueueData;

enum class MeshAttribute : unsigned
{
	Position = 0,
	UV = 1,
	Normal = 2,
	Tangent = 3,
	BoneIndex = 4,
	BoneWeights = 5,
	VertexColor = 6,
	Count,
	None
};

enum MeshAttributeFlagBits
{
	MESH_ATTRIBUTE_POSITION_BIT = 1u << Util::ecast(MeshAttribute::Position),
	MESH_ATTRIBUTE_UV_BIT = 1u << Util::ecast(MeshAttribute::UV),
	MESH_ATTRIBUTE_NORMAL_BIT = 1u << Util::ecast(MeshAttribute::Normal),
	MESH_ATTRIBUTE_TANGENT_BIT = 1u << Util::ecast(MeshAttribute::Tangent),
	MESH_ATTRIBUTE_BONE_INDEX_BIT = 1u << Util::ecast(MeshAttribute::BoneIndex),
	MESH_ATTRIBUTE_BONE_WEIGHTS_BIT = 1u << Util::ecast(MeshAttribute::BoneWeights),
	MESH_ATTRIBUTE_VERTEX_COLOR_BIT = 1u << Util::ecast(MeshAttribute::VertexColor)
};

struct MeshAttributeLayout
{
	VkFormat format = VK_FORMAT_UNDEFINED;
	uint32_t offset = 0;
};

struct StaticMeshVertex
{
	mat4 Model;
	//mat4 Normal;
	enum
	{
		max_instances = 256
	};
};

struct StaticMeshFragment
{
	vec4 base_color;
	vec4 emissive;
	float roughness;
	float metallic;
	float normal_scale;
};

struct DebugMeshInstanceInfo
{
	vec3 *positions;
	vec4 *colors;
	uint32_t count = 0;
};

struct DebugMeshInfo
{
	Vulkan::Program *program;
	mat4 MVP;
};

struct StaticMeshInstanceInfo
{
	StaticMeshVertex vertex;
};

struct SkinnedMeshInstanceInfo
{
	mat4 *world_transforms = nullptr;
	//mat4 *normal_transforms = nullptr;
	uint32_t num_bones = 0;
};

struct StaticMeshInfo
{
	const Vulkan::Buffer *vbo_position;
	const Vulkan::Buffer *vbo_attributes;
	const Vulkan::Buffer *ibo;
	const Vulkan::ImageView *views[Util::ecast(Material::Textures::Count)];
	Vulkan::StockSampler sampler;
	Vulkan::Program *program;
	VkPrimitiveTopology topology;

	MeshAttributeLayout attributes[Util::ecast(MeshAttribute::Count)];

	StaticMeshFragment fragment;

	uint32_t ibo_offset = 0;
	int32_t vertex_offset = 0;
	uint32_t count = 0;

	uint32_t position_stride;
	uint32_t attribute_stride;
	VkIndexType index_type;
	bool two_sided;
	bool alpha_test;
	bool primitive_restart;
};

namespace RenderFunctions
{
void static_mesh_render(Vulkan::CommandBuffer &cmd, const RenderQueueData *render, unsigned instances);
void debug_mesh_render(Vulkan::CommandBuffer &cmd, const RenderQueueData *render, unsigned instances);
void line_strip_render(Vulkan::CommandBuffer &cmd, const RenderQueueData *render, unsigned instances);
void skinned_mesh_render(Vulkan::CommandBuffer &cmd, const RenderQueueData *render, unsigned instances);
void mesh_set_state(Vulkan::CommandBuffer &cmd, const StaticMeshInfo &info);
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
	VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	bool primitive_restart = false;

	MeshAttributeLayout attributes[Util::ecast(MeshAttribute::Count)];

	MaterialHandle material;

	Util::Hash get_instance_key() const;
	Util::Hash get_baked_instance_key() const;

	AABB static_aabb;

	void get_render_info(const RenderContext &context, const RenderInfoComponent *transform,
	                     RenderQueue &queue) const override;

	DrawPipeline get_mesh_draw_pipeline() const override
	{
		return material->pipeline;
	}

	void bake();

protected:
	void reset();
	void fill_render_info(StaticMeshInfo &info) const;
	Util::Hash cached_hash = 0;

private:
	bool has_static_aabb() const override
	{
		return true;
	}

	const AABB *get_static_aabb() const override
	{
		return &static_aabb;
	}
};

struct SkinnedMesh : public StaticMesh
{
	void get_render_info(const RenderContext &context, const RenderInfoComponent *transform,
	                     RenderQueue &queue) const override;
};
}
