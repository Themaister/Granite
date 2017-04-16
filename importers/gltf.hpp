#pragma once

#include <string>
#include <vector>
#include "math.hpp"
#include "mesh.hpp"

namespace GLTF
{
using namespace glm;
struct Mesh
{
	// Attributes
	std::vector<vec3> positions;
	std::vector<float> attributes;
	uint32_t attribute_stride = 0;
	Granite::MeshAttributeLayout attribute_layout[Util::ecast(Granite::MeshAttribute::Count)] = {};

	// Index buffer
	std::vector<uint8_t> indices;
	uint32_t index_stride = 0;

	// Material
	uint32_t material_index = 0;

	// AABB
	Granite::AABB static_aabb;
};

struct NodeTransform
{
	vec3 scale = vec3(1.0f, 1.0f, 1.0f);
	quat rotation = quat(1.0f, 0.0f, 0.0f, 0.0f);
	vec3 translation = vec3(0.0f);
};

struct Node
{
	std::vector<uint32_t> meshes;
	std::vector<uint32_t> children;
	NodeTransform transform;
};

struct Material
{
	std::string base_color;
	std::string metallic_roughness;
};

struct Scene
{
	std::vector<Node> nodes;
};

class Parser
{
public:
	Parser(const std::string &path);

private:
	void parse(const std::string &path, const std::string &json);
	std::vector<Node> nodes;
	std::vector<Mesh> meshes;
	std::vector<Material> materials;
};
}
