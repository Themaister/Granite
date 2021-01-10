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

#include <string>
#include <vector>
#include "math.hpp"
#include "scene_formats.hpp"

namespace GLTF
{
using namespace Granite;
using namespace Granite::SceneFormats;

enum class ScalarType
{
	Float32,
	Float16,
	Int32,
	Uint32,
	Int16,
	Uint16,
	Int8,
	Uint8,
	Int16Snorm,
	Uint16Unorm,
	Int8Snorm,
	Uint8Unorm,
	A2Bgr10Unorm,
	A2Bgr10Uint,
	A2Bgr10Snorm,
	A2Bgr10Int
};

class Parser
{
public:
	explicit Parser(const std::string &path);

	const std::vector<SceneNodes> &get_scenes() const
	{
		return json_scenes;
	}

	uint32_t get_default_scene() const
	{
		return default_scene_index;
	}

	const std::vector<Mesh> &get_meshes() const
	{
		return meshes;
	}

	const std::vector<MaterialInfo> &get_materials() const
	{
		return materials;
	}

	const std::vector<Node> &get_nodes() const
	{
		return nodes;
	}

	const std::vector<Animation> &get_animations() const
	{
		return animations;
	}

	const std::vector<Skin> &get_skins() const
	{
		return json_skins;
	}

	const std::vector<CameraInfo> &get_cameras() const
	{
		return json_cameras;
	}

	const std::vector<LightInfo> &get_lights() const
	{
		return json_lights;
	}

	const std::vector<EnvironmentInfo> &get_environments() const
	{
		return json_environments;
	}

private:
	using Buffer = std::vector<uint8_t>;

	struct BufferView
	{
		uint32_t buffer_index;
		uint32_t offset;
		uint32_t length;
		uint32_t stride;
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
			bool primitive_restart;
		};
		std::vector<AttributeData> primitives;
	};

	struct Texture
	{
		uint32_t image_index;
		Vulkan::StockSampler sampler;
		VkComponentMapping swizzle;
	};

	void parse(const std::string &path, const std::string &json);
	std::vector<Mesh> meshes;
	std::vector<MaterialInfo> materials;
	static VkFormat components_to_padded_format(ScalarType type, uint32_t components);
	static Buffer read_buffer(const std::string &path, uint64_t length);
	static Buffer read_base64(const char *data, uint64_t length);
	static uint32_t type_stride(ScalarType type);
	static void resolve_component_type(uint32_t component_type, const char *type, bool normalized,
	                                   ScalarType &scalar_type, uint32_t &components, uint32_t &stride);

	std::vector<Buffer> json_buffers;
	std::vector<BufferView> json_views;
	std::vector<Accessor> json_accessors;
	std::vector<MeshData> json_meshes;
	std::vector<MaterialInfo::Texture> json_images;
	std::vector<Texture> json_textures;
	std::vector<Vulkan::StockSampler> json_stock_samplers;
	std::vector<Skin> json_skins;
	std::vector<CameraInfo> json_cameras;
	std::vector<LightInfo> json_lights;
	std::vector<EnvironmentInfo> json_environments;
	std::vector<Node> nodes;
	std::vector<Animation> animations;
	std::vector<Util::Hash> skin_compat;
	std::vector<std::string> json_animation_names;
	std::unordered_map<uint32_t, uint32_t> json_node_index_to_skin;
	std::unordered_map<uint32_t, uint32_t> json_node_index_to_joint_index;
	std::vector<std::vector<uint32_t>> mesh_index_to_primitives;
	std::vector<SceneNodes> json_scenes;
	uint32_t default_scene_index = 0;

	void build_meshes();
	void build_primitive(const MeshData::AttributeData &prim);

	void extract_attribute(std::vector<float> &attributes, const Accessor &accessor);
	void extract_attribute(std::vector<vec3> &attributes, const Accessor &accessor);
	void extract_attribute(std::vector<quat> &attributes, const Accessor &accessor);
	void extract_attribute(std::vector<mat4> &attributes, const Accessor &accessor);
};
}
