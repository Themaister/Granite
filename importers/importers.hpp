#pragma once

#include <vector>
#include <stdint.h>
#include "mesh.hpp"
#include "enum_cast.hpp"
#include "transforms.hpp"

namespace Granite
{
namespace Importer
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
		Scale
	};
	Type type;
	LinearSampler linear;
	SlerpSampler spherical;

	uint32_t joint_index;
	bool joint = false;
};

struct Animation
{
	std::vector<float> timestamps;
	std::vector<AnimationChannel> channels;
	std::string name;

	Util::Hash skin_compat = 0;
	bool skinning = false;

	float get_length() const
	{
		return timestamps.back();
	}

	void get_index_phase(float t, unsigned &index, float &phase) const
	{
		if (t <= timestamps.front() || timestamps.size() == 1)
		{
			index = 0;
			phase = 0.0f;
		}
		else if (t >= timestamps.back())
		{
			index = timestamps.size() - 2;
			phase = 1.0f;
		}
		else
		{
			unsigned end_target = 0;
			while (t > timestamps[end_target])
				end_target++;

			index = end_target - 1;
			phase = (t - timestamps[index]) / (timestamps[end_target] - timestamps[index]);
		}
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

	std::vector<uint32_t> skeletons;
	Util::Hash skin = 0;
	bool has_skin = false;

	std::string joint_name;
	bool joint = false;
};

struct Scene
{
	std::vector<Node> nodes;
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

	// AABB
	Granite::AABB static_aabb;

	uint32_t count = 0;
};

struct MaterialInfo
{
	std::string base_color;
	std::string normal;
	std::string metallic_roughness;
	vec4 uniform_base_color = vec4(1.0f);
	vec3 uniform_emissive_color = vec4(0.0f);
	float uniform_metallic = 1.0f;
	float uniform_roughness = 1.0f;
	float lod_bias = 0.0f;
	DrawPipeline pipeline = DrawPipeline::Opaque;
	Vulkan::StockSampler sampler = Vulkan::StockSampler::TrilinearWrap;
	bool two_sided = false;
};
}
}