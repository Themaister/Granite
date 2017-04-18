#pragma once

#include <string>
#include <vector>
#include "math.hpp"
#include "importers.hpp"

namespace GLTF
{
using namespace glm;

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

struct Scene
{
	std::vector<Node> nodes;
};

enum class ScalarType
{
	Float32,
	Int32,
	Uint32,
	Int16,
	Uint16,
	Int8,
	Uint8,
	Int16Snorm,
	Uint16Unorm,
	Int8Snorm,
	Uint8Unorm
};

class Parser
{
public:
	Parser(const std::string &path);

	const std::vector<Granite::Mesh> &get_meshes() const
	{
		return meshes;
	}

	const std::vector<Granite::MaterialInfo> &get_materials() const
	{
		return materials;
	}

private:
	using Buffer = std::vector<uint8_t>;

	struct BufferView
	{
		uint32_t buffer_index;
		uint32_t offset;
		uint32_t length;
		uint32_t target;
	};

	struct Accessor
	{
		uint32_t view;
		uint32_t offset;
		uint32_t count;
		uint32_t stride;

		ScalarType type;
		uint32_t components;

		union
		{
			float f32;
			uint32_t u32;
			int32_t i32;
		} min[16], max[16];
	};

	struct MeshData
	{
		struct AttributeData
		{
			struct Buffer
			{
				uint32_t accessor_index;
				bool active;
			};
			Buffer attributes[Util::ecast(Granite::MeshAttribute::Count)] = {};
			Buffer index_buffer;
			uint32_t material_index;
			VkPrimitiveTopology topology;
			bool has_material;
		};
		std::vector<AttributeData> primitives;
	};

	struct Texture
	{
		uint32_t image_index;
	};

	void parse(const std::string &path, const std::string &json);
	std::vector<Node> nodes;
	std::vector<Granite::Mesh> meshes;
	std::vector<Granite::MaterialInfo> materials;
	static VkFormat components_to_padded_format(ScalarType type, uint32_t components);
	static Buffer read_buffer(const std::string &path, uint64_t length);
	static uint32_t type_stride(ScalarType type);
	static void resolve_component_type(uint32_t component_type, const char *type, bool normalized,
	                                   ScalarType &scalar_type, uint32_t &components, uint32_t &stride);

	std::vector<Buffer> json_buffers;
	std::vector<BufferView> json_views;
	std::vector<Accessor> json_accessors;
	std::vector<MeshData> json_meshes;
	std::vector<std::string> json_images;
	std::vector<Texture> json_textures;
	std::unordered_map<std::string, uint32_t> json_buffer_map;
	std::unordered_map<std::string, uint32_t> json_view_map;
	std::unordered_map<std::string, uint32_t> json_accessor_map;
	std::unordered_map<std::string, uint32_t> json_mesh_map;
	std::unordered_map<std::string, uint32_t> json_images_map;
	std::unordered_map<std::string, uint32_t> json_textures_map;
	std::unordered_map<std::string, uint32_t> json_material_map;
	std::vector<std::vector<uint32_t>> mesh_index_to_primitives;

	void build_meshes();
	void build_primitive(const MeshData::AttributeData &prim);
};
}
