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

#include <vector>
#include <unordered_set>
#include <stdint.h>
#include "mesh.hpp"
#include "enum_cast.hpp"
#include "transforms.hpp"
#include "array_view.hpp"

namespace Granite
{
namespace SceneFormats
{
struct NodeTransform
{
	vec3 scale = vec3(1.0f, 1.0f, 1.0f);
	quat rotation = quat(1.0f, 0.0f, 0.0f, 0.0f);
	vec3 translation = vec3(0.0f);
};

struct AnimationChannel
{
	uint32_t node_index = 0;
	enum class Type
	{
		Translation,
		Rotation,
		Scale,
		CubicTranslation,
		CubicScale
	};
	Type type;

	std::vector<float> timestamps;
	LinearSampler linear;
	SlerpSampler spherical;
	CubicSampler cubic;

	uint32_t joint_index;
	bool joint = false;

	float get_length() const
	{
		return timestamps.back();
	}

	void get_index_phase(float t, unsigned &index, float &phase, float &dt) const
	{
		if (t < timestamps.front() || timestamps.size() == 1)
		{
			index = 0;
			phase = 0.0f;
			dt = 0.0f;
		}
		else if (t >= timestamps.back())
		{
			assert(timestamps.size() >= 2);
			index = timestamps.size() - 2;
			phase = 1.0f;
			dt = timestamps[index + 1] - timestamps[index];
		}
		else
		{
			unsigned end_target = 0;
			while (t >= timestamps[end_target])
				end_target++;

			index = end_target - 1;
			phase = (t - timestamps[index]) / (timestamps[end_target] - timestamps[index]);
			dt = timestamps[index + 1] - timestamps[index];
		}
	}
};

struct Animation
{
	std::vector<AnimationChannel> channels;
	std::string name;
	float length = 0.0f;

	Util::Hash skin_compat = 0;
	bool skinning = false;

	void update_length()
	{
		length = 0.0f;
		for (auto &chan : channels)
			length = std::max(length, chan.get_length());
	}
};

struct Skin
{
	std::vector<mat4> inverse_bind_pose;
	std::vector<NodeTransform> joint_transforms;

	struct Bone
	{
		uint32_t index;
		std::vector<Bone> children;
	};
	std::vector<Bone> skeletons;
	Util::Hash skin_compat;
};

struct Node
{
	std::vector<uint32_t> meshes;
	std::vector<uint32_t> children;
	NodeTransform transform;

	Util::Hash skin = 0;
	bool has_skin = false;
	bool joint = false;
};

struct CameraInfo
{
	enum class Type
	{
		Orthographic,
		Perspective
	};

	std::string name;
	uint32_t node_index = 0;
	Type type = Type::Perspective;
	float aspect_ratio = 1.0f;
	float znear = 0.1f;
	float zfar = 1000.0f;
	float yfov = 0.66f;
	float xmag = 1.0f;
	float ymag = 1.0f;

	bool attached_to_node = false;
};

struct MaterialInfo
{
	struct Texture
	{
		Texture() = default;
		explicit Texture(std::string path_)
			: path(std::move(path_))
		{}

		std::string path;
	};
	Texture base_color;
	Texture normal;
	Texture metallic_roughness;
	Texture occlusion;
	Texture emissive;

	vec4 uniform_base_color = vec4(1.0f);
	vec3 uniform_emissive_color = vec3(0.0f);
	float uniform_metallic = 1.0f;
	float uniform_roughness = 1.0f;
	float normal_scale = 1.0f;
	DrawPipeline pipeline = DrawPipeline::Opaque;
	Vulkan::StockSampler sampler = Vulkan::StockSampler::TrilinearWrap;
	bool two_sided = false;
	bool bandlimited_pixel = false;
};

struct EnvironmentInfo
{
	MaterialInfo::Texture cube;
	MaterialInfo::Texture reflection;
	MaterialInfo::Texture irradiance;
	float intensity;

	struct Fog
	{
		vec3 color;
		float falloff;
	} fog;
};

struct LightInfo
{
	enum class Type
	{
		Directional,
		Spot,
		Point,
		Ambient
	};

	std::string name;
	uint32_t node_index = 0;
	Type type = Type::Spot;
	float inner_cone = 0.40f;
	float outer_cone = 0.45f;
	vec3 color = vec3(1.0f);
	float range = 0.0f;

	bool attached_to_node = false;
};

struct SceneNodes
{
	std::string name;
	std::vector<uint32_t> node_indices;
};

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
	bool has_material = false;
	bool primitive_restart = false;

	// AABB
	Granite::AABB static_aabb;

	uint32_t count = 0;
};

// A simplified mesh representation for CPU use.
struct CollisionMesh
{
	std::vector<vec4> positions;
	std::vector<uint32_t> indices;
};

struct SceneInformation
{
	Util::ArrayView<const MaterialInfo> materials;
	Util::ArrayView<const Mesh> meshes;
	Util::ArrayView<const LightInfo> lights;
	Util::ArrayView<const CameraInfo> cameras;
	Util::ArrayView<const Node> nodes;
	Util::ArrayView<const Skin> skins;
	Util::ArrayView<const Animation> animations;
	const SceneNodes *scene_nodes = nullptr;
};

bool mesh_recompute_normals(Mesh &mesh);
bool mesh_recompute_tangents(Mesh &mesh);
bool mesh_renormalize_normals(Mesh &mesh);
bool mesh_renormalize_tangents(Mesh &mesh);
bool mesh_flip_tangents_w(Mesh &mesh);
bool extract_collision_mesh(CollisionMesh &collision_mesh, const Mesh &mesh);

void mesh_deduplicate_vertices(Mesh &mesh);
Mesh mesh_optimize_index_buffer(const Mesh &mesh, bool stripify);
std::unordered_set<uint32_t> build_used_nodes_in_scene(const SceneNodes &scene, const std::vector<Node> &nodes);
}
}
