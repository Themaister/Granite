/* Copyright (c) 2017 Hans-Kristian Arntzen
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

#include "gltf_export.hpp"
#include "texture_compression.hpp"
#include "texture_files.hpp"

#define RAPIDJSON_ASSERT(x) do { if (!(x)) throw "JSON error"; } while(0)
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/istreamwrapper.h"
#include "hashmap.hpp"
#include "thread_group.hpp"
#include <unordered_set>

using namespace std;
using namespace rapidjson;
using namespace Util;

namespace Granite
{
namespace SceneFormats
{
template<typename T>
struct Remap
{
	vector<unsigned> to_index;
	HashMap<unsigned> hashmap;
	vector<const T *> info;
};

struct BufferView
{
	size_t offset;
	size_t length;
	size_t stride;
};

struct EmittedMesh
{
	int index_accessor = -1;
	int material = -1;
	uint32_t attribute_mask = 0;
	int attribute_accessor[ecast(MeshAttribute::Count)] = {};
};

struct EmittedAccessor
{
	unsigned view = 0;
	unsigned count = 0;
	const char *type = nullptr;
	unsigned component = 0;
	unsigned offset = 0;

	AABB aabb;
	bool normalized = false;
	bool use_aabb = false;
};

struct EmittedMaterial
{
	int base_color = -1;
	int normal = -1;
	int metallic_roughness = -1;
	int occlusion = -1;
	int emissive = -1;

	vec4 uniform_base_color = vec4(1.0f);
	vec3 uniform_emissive_color = vec4(0.0f);
	float uniform_metallic = 1.0f;
	float uniform_roughness = 1.0f;
	float lod_bias = 0.0f;
	float normal_scale = 1.0f;
	DrawPipeline pipeline = DrawPipeline::Opaque;
	bool two_sided = false;
};

struct EmittedTexture
{
	unsigned image;
	unsigned sampler;
};

struct EmittedImage
{
	string source_path;
	string target_relpath;
	string target_mime;
	Material::Textures type;
};

struct EmittedSampler
{
	unsigned mag_filter;
	unsigned min_filter;
	unsigned wrap_s;
	unsigned wrap_t;
};

struct RemapState
{
	Hash hash(const Mesh &mesh);
	Hash hash(const MaterialInfo &mesh);

	template<typename StateType, typename SceneType>
	void filter_input(StateType &output, const SceneType &input);

	unsigned emit_buffer(ArrayView<const uint8_t> view, uint32_t stride);

	unsigned emit_accessor(unsigned view_index,
	                       VkFormat format, unsigned offset, unsigned stride, unsigned count);

	unsigned emit_texture(const string &texture, Vulkan::StockSampler sampler, Material::Textures type);
	unsigned emit_sampler(Vulkan::StockSampler sampler);
	unsigned emit_image(const string &texture, Material::Textures type);
	void emit_material(unsigned remapped_material);
	void emit_mesh(unsigned remapped_index);
	unsigned emit_meshes(ArrayView<const unsigned> meshes);

	Remap<Mesh> mesh;
	Remap<MaterialInfo> material;

	vector<uint8_t> glb_buffer_data;
	HashMap<unsigned> buffer_hash;
	vector<BufferView> buffer_views;

	HashMap<unsigned> accessor_hash;
	vector<EmittedAccessor> accessor_cache;

	unordered_set<unsigned> mesh_hash;
	vector<EmittedMesh> mesh_cache;

	unordered_set<unsigned> material_hash;
	vector<EmittedMaterial> material_cache;

	HashMap<unsigned> texture_hash;
	vector<EmittedTexture> texture_cache;

	HashMap<unsigned> image_hash;
	vector<EmittedImage> image_cache;

	HashMap<unsigned> sampler_hash;
	vector<EmittedSampler> sampler_cache;

	HashMap<unsigned> mesh_group_hash;
	vector<vector<unsigned>> mesh_group_cache;
};

Hash RemapState::hash(const Mesh &mesh)
{
	Hasher h;

	h.u32(mesh.topology);
	h.u32(mesh.index_type);
	h.u32(mesh.attribute_stride);
	h.u32(mesh.position_stride);
	h.u32(mesh.has_material);
	if (mesh.has_material)
		h.u32(material.to_index[mesh.material_index]);
	h.data(reinterpret_cast<const uint8_t *>(mesh.attribute_layout), sizeof(mesh.attribute_layout));

	auto lo = mesh.static_aabb.get_minimum();
	auto hi = mesh.static_aabb.get_maximum();
	h.f32(lo.x);
	h.f32(lo.y);
	h.f32(lo.z);
	h.f32(hi.x);
	h.f32(hi.y);
	h.f32(hi.z);

	h.u32(0xff);
	if (!mesh.positions.empty())
		h.data(mesh.positions.data(), mesh.positions.size() * sizeof(mesh.positions[0]));
	h.u32(0xff);
	if (!mesh.indices.empty())
		h.data(mesh.indices.data(), mesh.indices.size() * sizeof(mesh.indices[0]));
	h.u32(0xff);
	if (!mesh.attributes.empty())
		h.data(mesh.attributes.data(), mesh.attributes.size() * sizeof(mesh.attributes[0]));

	h.u32(mesh.count);
	return h.get();
}

Hash RemapState::hash(const MaterialInfo &mat)
{
	Hasher h;
	h.string(mat.base_color);
	h.string(mat.normal);
	h.string(mat.occlusion);
	h.string(mat.metallic_roughness);
	h.string(mat.emissive);

	h.f32(mat.normal_scale);
	h.f32(mat.uniform_metallic);
	h.f32(mat.uniform_roughness);
	for (unsigned i = 0; i < 4; i++)
		h.f32(mat.uniform_base_color[i]);
	h.f32(mat.lod_bias);
	for (unsigned i = 0; i < 3; i++)
		h.f32(mat.uniform_emissive_color[i]);
	h.u32(mat.two_sided);
	h.u32(ecast(mat.pipeline));

	return h.get();
}

template<typename StateType, typename SceneType>
void RemapState::filter_input(StateType &output, const SceneType &input)
{
	for (auto &i : input)
	{
		auto h = hash(i);
		auto itr = output.hashmap.find(h);
		if (itr != end(output.hashmap))
			output.to_index.push_back(itr->second);
		else
		{
			unsigned index = output.info.size();
			output.to_index.push_back(output.info.size());
			output.info.push_back(&i);
			output.hashmap[h] = index;
		}
	}
}

unsigned RemapState::emit_buffer(ArrayView<const uint8_t> view, uint32_t stride)
{
	Hasher h;
	h.data(view.data(), view.size());
	h.u32(stride);
	auto itr = buffer_hash.find(h.get());

	if (itr == end(buffer_hash))
	{
		unsigned index = buffer_views.size();
		size_t offset = glb_buffer_data.size();
		offset = (offset + 15) & ~15;
		glb_buffer_data.resize(offset + view.size());
		memcpy(glb_buffer_data.data() + offset, view.data(), view.size());
		buffer_views.push_back({offset, view.size(), stride});
		buffer_hash[h.get()] = index;
		return index;
	}
	else
		return itr->second;
}

#define GL_BYTE                           0x1400
#define GL_UNSIGNED_BYTE                  0x1401
#define GL_SHORT                          0x1402
#define GL_UNSIGNED_SHORT                 0x1403
#define GL_INT                            0x1404
#define GL_UNSIGNED_INT                   0x1405
#define GL_FLOAT                          0x1406

#define GL_REPEAT                         0x2901
#define GL_CLAMP_TO_EDGE                  0x812F
#define GL_NEAREST                        0x2600
#define GL_LINEAR                         0x2601
#define GL_NEAREST_MIPMAP_NEAREST         0x2700
#define GL_LINEAR_MIPMAP_NEAREST          0x2701
#define GL_NEAREST_MIPMAP_LINEAR          0x2702
#define GL_LINEAR_MIPMAP_LINEAR           0x2703

static const char *get_accessor_type(VkFormat format)
{
	switch (format)
	{
	case VK_FORMAT_R32_SFLOAT:
	case VK_FORMAT_R8_UNORM:
	case VK_FORMAT_R8_UINT:
	case VK_FORMAT_R8_SNORM:
	case VK_FORMAT_R8_SINT:
	case VK_FORMAT_R16_UNORM:
	case VK_FORMAT_R16_UINT:
	case VK_FORMAT_R16_SNORM:
	case VK_FORMAT_R16_SINT:
	case VK_FORMAT_R32_UINT:
	case VK_FORMAT_R32_SINT:
		return "SCALAR";

	case VK_FORMAT_R32G32_SFLOAT:
	case VK_FORMAT_R8G8_UNORM:
	case VK_FORMAT_R8G8_UINT:
	case VK_FORMAT_R8G8_SNORM:
	case VK_FORMAT_R8G8_SINT:
	case VK_FORMAT_R16G16_UINT:
	case VK_FORMAT_R16G16_SINT:
	case VK_FORMAT_R16G16_SNORM:
	case VK_FORMAT_R16G16_UNORM:
	case VK_FORMAT_R32G32_UINT:
	case VK_FORMAT_R32G32_SINT:
		return "VEC2";

	case VK_FORMAT_R32G32B32_SFLOAT:
	case VK_FORMAT_R8G8B8_UNORM:
	case VK_FORMAT_R8G8B8_UINT:
	case VK_FORMAT_R8G8B8_SNORM:
	case VK_FORMAT_R8G8B8_SINT:
	case VK_FORMAT_R16G16B16_UNORM:
	case VK_FORMAT_R16G16B16_UINT:
	case VK_FORMAT_R16G16B16_SNORM:
	case VK_FORMAT_R16G16B16_SINT:
		return "VEC3";

	case VK_FORMAT_R32G32B32A32_SFLOAT:
	case VK_FORMAT_R8G8B8A8_UNORM:
	case VK_FORMAT_R8G8B8A8_UINT:
	case VK_FORMAT_R8G8B8A8_SNORM:
	case VK_FORMAT_R8G8B8A8_SINT:
	case VK_FORMAT_R16G16B16A16_UNORM:
	case VK_FORMAT_R16G16B16A16_UINT:
	case VK_FORMAT_R16G16B16A16_SNORM:
	case VK_FORMAT_R16G16B16A16_SINT:
	case VK_FORMAT_R32G32B32_UINT:
	case VK_FORMAT_R32G32B32A32_UINT:
	case VK_FORMAT_R32G32B32_SINT:
	case VK_FORMAT_R32G32B32A32_SINT:
		return "VEC4";

	default:
		throw invalid_argument("Unsupported format.");
	}
}

static bool get_accessor_normalized(VkFormat format)
{
	switch (format)
	{
	case VK_FORMAT_R8_UNORM:
	case VK_FORMAT_R8G8_UNORM:
	case VK_FORMAT_R8G8B8_UNORM:
	case VK_FORMAT_R8G8B8A8_UNORM:
	case VK_FORMAT_R8_SNORM:
	case VK_FORMAT_R8G8_SNORM:
	case VK_FORMAT_R8G8B8_SNORM:
	case VK_FORMAT_R8G8B8A8_SNORM:
	case VK_FORMAT_R16_UNORM:
	case VK_FORMAT_R16G16_UNORM:
	case VK_FORMAT_R16G16B16_UNORM:
	case VK_FORMAT_R16G16B16A16_UNORM:
	case VK_FORMAT_R16_SNORM:
	case VK_FORMAT_R16G16_SNORM:
	case VK_FORMAT_R16G16B16_SNORM:
	case VK_FORMAT_R16G16B16A16_SNORM:
		return true;

	default:
		return false;
	}
}

static unsigned get_accessor_component(VkFormat format)
{
	switch (format)
	{
	case VK_FORMAT_R32_SFLOAT:
	case VK_FORMAT_R32G32_SFLOAT:
	case VK_FORMAT_R32G32B32_SFLOAT:
	case VK_FORMAT_R32G32B32A32_SFLOAT:
		return GL_FLOAT;

	case VK_FORMAT_R8_UNORM:
	case VK_FORMAT_R8G8_UNORM:
	case VK_FORMAT_R8G8B8_UNORM:
	case VK_FORMAT_R8G8B8A8_UNORM:
	case VK_FORMAT_R8_UINT:
	case VK_FORMAT_R8G8_UINT:
	case VK_FORMAT_R8G8B8_UINT:
	case VK_FORMAT_R8G8B8A8_UINT:
		return GL_UNSIGNED_BYTE;

	case VK_FORMAT_R8_SNORM:
	case VK_FORMAT_R8G8_SNORM:
	case VK_FORMAT_R8G8B8_SNORM:
	case VK_FORMAT_R8G8B8A8_SNORM:
	case VK_FORMAT_R8_SINT:
	case VK_FORMAT_R8G8_SINT:
	case VK_FORMAT_R8G8B8_SINT:
	case VK_FORMAT_R8G8B8A8_SINT:
		return GL_UNSIGNED_BYTE;
		return GL_BYTE;

	case VK_FORMAT_R16_UNORM:
	case VK_FORMAT_R16G16_UNORM:
	case VK_FORMAT_R16G16B16_UNORM:
	case VK_FORMAT_R16G16B16A16_UNORM:
	case VK_FORMAT_R16_UINT:
	case VK_FORMAT_R16G16_UINT:
	case VK_FORMAT_R16G16B16_UINT:
	case VK_FORMAT_R16G16B16A16_UINT:
		return GL_UNSIGNED_SHORT;

	case VK_FORMAT_R16_SNORM:
	case VK_FORMAT_R16G16_SNORM:
	case VK_FORMAT_R16G16B16_SNORM:
	case VK_FORMAT_R16G16B16A16_SNORM:
	case VK_FORMAT_R16_SINT:
	case VK_FORMAT_R16G16_SINT:
	case VK_FORMAT_R16G16B16_SINT:
	case VK_FORMAT_R16G16B16A16_SINT:
		return GL_SHORT;

	case VK_FORMAT_R32_UINT:
	case VK_FORMAT_R32G32_UINT:
	case VK_FORMAT_R32G32B32_UINT:
	case VK_FORMAT_R32G32B32A32_UINT:
		return GL_UNSIGNED_INT;

	case VK_FORMAT_R32_SINT:
	case VK_FORMAT_R32G32_SINT:
	case VK_FORMAT_R32G32B32_SINT:
	case VK_FORMAT_R32G32B32A32_SINT:
		return GL_INT;

	default:
		throw invalid_argument("Unsupported format.");
	}
}

static void set_accessor_type(EmittedAccessor &accessor, VkFormat format)
{
	accessor.component = get_accessor_component(format);
	accessor.type = get_accessor_type(format);
	accessor.normalized = get_accessor_normalized(format);
}

unsigned RemapState::emit_accessor(unsigned view_index, VkFormat format, unsigned offset, unsigned stride, unsigned count)
{
	Hasher h;
	h.u32(view_index);
	h.u32(format);
	h.u32(offset);
	h.u32(stride);
	h.u32(count);

	auto itr = accessor_hash.find(h.get());
	if (itr == end(accessor_hash))
	{
		unsigned index = accessor_cache.size();
		EmittedAccessor acc = {};
		acc.count = count;
		acc.view = view_index;
		acc.offset = offset;
		set_accessor_type(acc, format);

		accessor_cache.push_back(acc);
		accessor_hash[h.get()] = index;
		return index;
	}
	else
		return itr->second;
}

unsigned RemapState::emit_sampler(Vulkan::StockSampler sampler)
{
	Hasher h;
	h.u32(ecast(sampler));
	auto itr = sampler_hash.find(h.get());

	if (itr == end(sampler_hash))
	{
		unsigned index = sampler_cache.size();
		sampler_hash[h.get()] = index;

		unsigned mag_filter = 0, min_filter = 0, wrap_s = 0, wrap_t = 0;

		switch (sampler)
		{
		case Vulkan::StockSampler::TrilinearWrap:
			mag_filter = GL_LINEAR;
			min_filter = GL_LINEAR_MIPMAP_LINEAR;
			wrap_s = GL_REPEAT;
			wrap_t = GL_REPEAT;
			break;

		case Vulkan::StockSampler::TrilinearClamp:
			mag_filter = GL_LINEAR;
			min_filter = GL_LINEAR_MIPMAP_LINEAR;
			wrap_s = GL_CLAMP_TO_EDGE;
			wrap_t = GL_CLAMP_TO_EDGE;
			break;

		case Vulkan::StockSampler::LinearWrap:
			mag_filter = GL_LINEAR;
			min_filter = GL_LINEAR_MIPMAP_NEAREST;
			wrap_s = GL_REPEAT;
			wrap_t = GL_REPEAT;
			break;

		case Vulkan::StockSampler::LinearClamp:
			mag_filter = GL_LINEAR;
			min_filter = GL_LINEAR_MIPMAP_NEAREST;
			wrap_s = GL_CLAMP_TO_EDGE;
			wrap_t = GL_CLAMP_TO_EDGE;
			break;

		case Vulkan::StockSampler::NearestClamp:
			mag_filter = GL_NEAREST;
			min_filter = GL_NEAREST_MIPMAP_NEAREST;
			wrap_s = GL_CLAMP_TO_EDGE;
			wrap_t = GL_CLAMP_TO_EDGE;
			break;

		case Vulkan::StockSampler::NearestWrap:
			mag_filter = GL_NEAREST;
			min_filter = GL_NEAREST_MIPMAP_NEAREST;
			wrap_s = GL_REPEAT;
			wrap_t = GL_REPEAT;
			break;

		default:
			break;
		}

		sampler_cache.push_back({ mag_filter, min_filter, wrap_s, wrap_t });
		return index;
	}
	else
		return itr->second;
}

unsigned RemapState::emit_image(const string &texture, Material::Textures type)
{
	Hasher h;
	h.string(texture);
	h.u32(ecast(type));
	auto itr = image_hash.find(h.get());

	if (itr == end(image_hash))
	{
		unsigned index = image_cache.size();
		image_hash[h.get()] = index;
		image_cache.push_back({ texture, to_string(h.get()) + ".ktx", "image/ktx", type });
		return index;
	}
	else
		return itr->second;
}

unsigned RemapState::emit_texture(const string &texture, Vulkan::StockSampler sampler, Material::Textures type)
{
	unsigned image_index = emit_image(texture, type);
	unsigned sampler_index = emit_sampler(sampler);
	Hasher h;
	h.u32(image_index);
	h.u32(sampler_index);
	auto itr = texture_hash.find(h.get());

	if (itr == end(texture_hash))
	{
		unsigned index = texture_cache.size();
		texture_hash[h.get()] = index;
		texture_cache.push_back({ image_index, sampler_index });
		return index;
	}
	else
		return itr->second;
}

void RemapState::emit_material(unsigned remapped_material)
{
	auto &material = *this->material.info[remapped_material];
	material_cache.resize(std::max<size_t>(material_cache.size(), remapped_material + 1));
	auto &output = material_cache[remapped_material];

	if (!material.normal.empty())
		output.normal = emit_texture(material.normal, material.sampler, Material::Textures::Normal);
	if (!material.occlusion.empty())
		output.occlusion = emit_texture(material.occlusion, material.sampler, Material::Textures::Occlusion);
	if (!material.base_color.empty())
		output.base_color = emit_texture(material.base_color, material.sampler, Material::Textures::BaseColor);
	if (!material.metallic_roughness.empty())
		output.metallic_roughness = emit_texture(material.metallic_roughness, material.sampler, Material::Textures::MetallicRoughness);
	if (!material.emissive.empty())
		output.emissive = emit_texture(material.emissive, material.sampler, Material::Textures::Emissive);

	output.uniform_base_color = material.uniform_base_color;
	output.uniform_emissive_color = material.uniform_emissive_color;
	output.uniform_metallic = material.uniform_metallic;
	output.uniform_roughness = material.uniform_roughness;
	output.lod_bias = material.lod_bias;
	output.normal_scale = material.normal_scale;
	output.pipeline = material.pipeline;
	output.two_sided = material.two_sided;
}

void RemapState::emit_mesh(unsigned remapped_index)
{
	auto &mesh = *this->mesh.info[remapped_index];
	mesh_cache.resize(std::max<size_t>(mesh_cache.size(), remapped_index + 1));

	auto &emit = mesh_cache[remapped_index];
	emit.material = mesh.has_material ? int(mesh.material_index) : -1;

	if (!mesh.indices.empty())
	{
		unsigned index = emit_buffer(mesh.indices, mesh.index_type == VK_INDEX_TYPE_UINT16 ? 2 : 4);
		emit.index_accessor = emit_accessor(index,
		                                    mesh.index_type == VK_INDEX_TYPE_UINT16 ? VK_FORMAT_R16_UINT
		                                                                            : VK_FORMAT_R32_UINT,
		                                    0, mesh.index_type == VK_INDEX_TYPE_UINT16 ? 2 : 4, mesh.count);
	}
	else
		emit.index_accessor = -1;

	if (mesh.has_material)
	{
		unsigned remapped_material = material.to_index[mesh.material_index];
		if (!material_hash.count(remapped_material))
		{
			emit_material(remapped_material);
			material_hash.insert(remapped_material);
		}
	}

	unsigned position_buffer = 0;
	unsigned attribute_buffer = 0;
	if (!mesh.positions.empty())
		position_buffer = emit_buffer(mesh.positions, mesh.position_stride);
	if (!mesh.attributes.empty())
		attribute_buffer = emit_buffer(mesh.attributes, mesh.attribute_stride);

	emit.attribute_mask = 0;
	for (unsigned i = 0; i < ecast(MeshAttribute::Count); i++)
	{
		if (mesh.attribute_layout[i].format == VK_FORMAT_UNDEFINED)
			continue;

		emit.attribute_mask |= 1u << i;

		if (i == ecast(MeshAttribute::Position))
		{
			emit.attribute_accessor[i] = emit_accessor(position_buffer,
			                                           mesh.attribute_layout[i].format,
			                                           mesh.attribute_layout[i].offset, mesh.position_stride,
			                                           mesh.positions.size() / mesh.position_stride);

			accessor_cache[emit.attribute_accessor[i]].aabb = mesh.static_aabb;
			accessor_cache[emit.attribute_accessor[i]].use_aabb = true;
		}
		else
		{
			emit.attribute_accessor[i] = emit_accessor(attribute_buffer,
			                                           mesh.attribute_layout[i].format,
			                                           mesh.attribute_layout[i].offset, mesh.attribute_stride,
			                                           mesh.attributes.size() / mesh.attribute_stride);
		}
	}
}

unsigned RemapState::emit_meshes(ArrayView<const unsigned> meshes)
{
	Hasher emit_hash;
	vector<unsigned> mesh_group;
	mesh_group.reserve(meshes.size());

	for (auto &mesh : meshes)
	{
		unsigned remapped_index = this->mesh.to_index[mesh];
		emit_hash.u32(remapped_index);
		mesh_group.push_back(remapped_index);

		if (!mesh_hash.count(remapped_index))
		{
			emit_mesh(remapped_index);
			mesh_hash.insert(remapped_index);
		}
	}

	unsigned index;
	auto itr = mesh_group_hash.find(emit_hash.get());
	if (itr == end(mesh_group_hash))
	{
		index = mesh_group_cache.size();
		mesh_group_cache.push_back(move(mesh_group));
		mesh_group_hash[emit_hash.get()] = index;
	}
	else
		index = itr->second;

	return index;
}

static void compress_textures(ThreadGroup &workers,

bool export_scene_to_glb(const SceneInformation &scene, const string &path, const ExportOptions &options)
{
	Document doc;
	doc.SetObject();
	auto &allocator = doc.GetAllocator();

	ThreadGroup workers;
	workers.start(8); // TODO: Dynamically figure this out.

	Value asset(kObjectType);
	asset.AddMember("generator", "Granite glTF 2.0 exporter", allocator);
	asset.AddMember("version", "2.0", allocator);
	doc.AddMember("asset", asset, allocator);

	if (!scene.lights.empty())
	{
		Value req(kArrayType);
		req.PushBack("KHR_lights_cmn", allocator);
		doc.AddMember("extensionsRequired", req, allocator);

		Value used(kArrayType);
		used.PushBack("KHR_lights_cmn", allocator);
		doc.AddMember("extensionsUsed", used, allocator);
	}

	RemapState state;
	state.filter_input(state.material, scene.materials);
	state.filter_input(state.mesh, scene.meshes);

	Value nodes(kArrayType);
	for (auto &node : scene.nodes)
	{
		Value n(kObjectType);
		if (!node.children.empty())
		{
			Value children(kArrayType);
			for (auto &c : node.children)
				children.PushBack(c, allocator);
			n.AddMember("children", children, allocator);
		}

		if (!node.meshes.empty())
			n.AddMember("mesh", state.emit_meshes(node.meshes), allocator);

		// TODO: Reverse mapping to avoid searching every time.
		for (auto &camera : scene.cameras)
		{
			if (camera.attached_to_node && camera.node_index == uint32_t(&node - scene.nodes.data()))
			{
				n.AddMember("camera", uint32_t(&camera - scene.cameras.data()), allocator);
				break;
			}
		}

		// TODO: Reverse mapping to avoid searching every time.
		for (auto &light : scene.lights)
		{
			if (light.attached_to_node && light.node_index == uint32_t(&node - scene.nodes.data()))
			{
				Value ext(kObjectType);
				Value cmn(kObjectType);
				cmn.AddMember("light", uint32_t(&light - scene.lights.data()), allocator);
				ext.AddMember("KHR_lights_cmn", cmn, allocator);
				n.AddMember("extensions", ext, allocator);
				break;
			}
		}

		if (node.transform.rotation.w != 1.0f ||
		    node.transform.rotation.x != 0.0f ||
		    node.transform.rotation.y != 0.0f ||
		    node.transform.rotation.z != 0.0f)
		{
			Value rot(kArrayType);
			for (unsigned i = 0; i < 4; i++)
				rot.PushBack(node.transform.rotation[i], allocator);
			n.AddMember("rotation", rot, allocator);
		}

		if (any(notEqual(node.transform.scale, vec3(1.0f))))
		{
			Value s(kArrayType);
			for (unsigned i = 0; i < 3; i++)
				s.PushBack(node.transform.scale[i], allocator);
			n.AddMember("scale", s, allocator);
		}

		if (any(notEqual(node.transform.translation, vec3(0.0f))))
		{
			Value t(kArrayType);
			for (unsigned i = 0; i < 3; i++)
				t.PushBack(node.transform.translation[i], allocator);
			n.AddMember("translation", t, allocator);
		}
		nodes.PushBack(n, allocator);
	}
	doc.AddMember("nodes", nodes, allocator);

	// The baked GLB buffer.
	{
		Value buffers(kArrayType);
		Value buffer(kObjectType);
		buffer.AddMember("byteLength", state.glb_buffer_data.size(), allocator);
		buffers.PushBack(buffer, allocator);
		doc.AddMember("buffers", buffers, allocator);
	}

	// Buffer Views
	{
		Value views(kArrayType);
		for (auto &view : state.buffer_views)
		{
			Value v(kObjectType);
			v.AddMember("buffer", 0, allocator);
			v.AddMember("byteLength", view.length, allocator);
			v.AddMember("byteOffset", view.offset, allocator);
			v.AddMember("byteStride", view.stride, allocator);
			views.PushBack(v, allocator);
		}
		doc.AddMember("bufferViews", views, allocator);
	}

	// Accessors
	{
		Value accessors(kArrayType);
		for (auto &accessor : state.accessor_cache)
		{
			Value acc(kObjectType);
			acc.AddMember("bufferView", accessor.view, allocator);
			acc.AddMember("componentType", accessor.component, allocator);
			acc.AddMember("type", StringRef(accessor.type), allocator);
			acc.AddMember("count", accessor.count, allocator);
			acc.AddMember("byteOffset", accessor.offset, allocator);
			if (accessor.use_aabb)
			{
				vec4 lo = vec4(accessor.aabb.get_minimum(), 1.0f);
				vec4 hi = vec4(accessor.aabb.get_maximum(), 1.0f);
				unsigned components = 0;
				if (strcmp(accessor.type, "SCALAR") == 0)
					components = 1;
				else if (strcmp(accessor.type, "VEC2") == 0)
					components = 2;
				else if (strcmp(accessor.type, "VEC3") == 0)
					components = 3;
				else if (strcmp(accessor.type, "VEC4") == 0)
					components = 4;

				if (components)
				{
					Value minimum(kArrayType);
					Value maximum(kArrayType);
					for (unsigned i = 0; i < components; i++)
						minimum.PushBack(lo[i], allocator);
					for (unsigned i = 0; i < components; i++)
						maximum.PushBack(hi[i], allocator);
					acc.AddMember("min", minimum, allocator);
					acc.AddMember("max", maximum, allocator);
				}
			}
			accessors.PushBack(acc, allocator);
		}
		doc.AddMember("accessors", accessors, allocator);
	}

	// Samplers
	{
		Value samplers(kArrayType);
		for (auto &sampler : state.sampler_cache)
		{
			Value s(kObjectType);
			if (sampler.mag_filter)
				s.AddMember("magFilter", sampler.mag_filter, allocator);
			if (sampler.min_filter)
				s.AddMember("minFilter", sampler.min_filter, allocator);
			if (sampler.wrap_s)
				s.AddMember("wrapS", sampler.wrap_s, allocator);
			if (sampler.wrap_t)
				s.AddMember("wrapT", sampler.wrap_t, allocator);
			samplers.PushBack(s, allocator);
		}
		doc.AddMember("samplers", samplers, allocator);
	}

	// Images
	{
		// TODO: Multi-thread this.

		const auto get_compression_format = [&](Material::Textures type) -> gli::format {
			bool srgb = type == Material::Textures::BaseColor || type == Material::Textures::Emissive;
			switch (options.compression)
			{
			case TextureCompression::Uncompressed:
				return srgb ? gli::FORMAT_RGBA8_SRGB_PACK8 : gli::FORMAT_RGBA8_UNORM_PACK8;

			case TextureCompression::BC3:
				return srgb ? gli::FORMAT_RGBA_DXT5_SRGB_BLOCK16 : gli::FORMAT_RGBA_DXT5_UNORM_BLOCK16;

			case TextureCompression::BC7:
				return srgb ? gli::FORMAT_RGBA_BP_SRGB_BLOCK16 : gli::FORMAT_RGBA_BP_UNORM_BLOCK16;

			case TextureCompression::ASTC4x4:
				return srgb ? gli::FORMAT_RGBA_ASTC_4X4_SRGB_BLOCK16 : gli::FORMAT_RGBA_ASTC_4X4_UNORM_BLOCK16;

			case TextureCompression::ASTC5x5:
				return srgb ? gli::FORMAT_RGBA_ASTC_5X5_SRGB_BLOCK16 : gli::FORMAT_RGBA_ASTC_5X5_UNORM_BLOCK16;

			case TextureCompression::ASTC6x6:
				return srgb ? gli::FORMAT_RGBA_ASTC_6X6_SRGB_BLOCK16 : gli::FORMAT_RGBA_ASTC_6X6_UNORM_BLOCK16;

			case TextureCompression::ASTC8x8:
				return srgb ? gli::FORMAT_RGBA_ASTC_8X8_SRGB_BLOCK16 : gli::FORMAT_RGBA_ASTC_8X8_UNORM_BLOCK16;
			}

			return gli::FORMAT_UNDEFINED;
		};

		Value images(kArrayType);
		for (auto &image : state.image_cache)
		{
			compress_image(image, options.compression, options.texcomp_quality);

			Value i(kObjectType);
			i.AddMember("uri", image.target_relpath, allocator);
			i.AddMember("mimeType", image.target_mime, allocator);
			images.PushBack(i, allocator);

			auto target_path = Path::relpath(path, image.target_relpath);
			auto group = ThreadGroup::create_wait_group();

			CompressorArguments args;
			args.output = target_path;
			args.format = get_compression_format(image.type);
			args.quality = options.texcomp_quality;

			const auto load_task = [target_path = move(target_path), get_compression_format, &image, &options]() -> Variant {
				auto input = load_texture_from_file(image.source_path,
				                                    image.type == Material::Textures::BaseColor ? ColorSpace::sRGB : ColorSpace::Linear);

				return Variant(move(input));
			};

			const auto mipgen_task = [](Variant tex) -> Variant {
				return Variant(generate_offline_mipmaps(tex.get<gli::texture>()));
			};

			const auto mipgen_complete = [target_path = move(target_path), args = move(args), &options](Variant tex) -> Variant {
				if (options.compression != TextureCompression::Uncompressed)
				{
					if (!compress_texture(args, tex.get<gli::texture>()))
					{
						LOGE("Failed to compress!\n");
						return Variant(false);
					}
				}
				else
				{
					if (!save_texture_to_file(target_path, tex.get<gli::texture>()))
					{
						LOGE("Failed to save uncompressed file!\n");
						return Variant(false);
					}
				}
				return Variant(true);
			};

			const auto load_complete = [&](Variant result) {
				auto mipmap = ThreadGroup::create_wait_group();
				workers.enqueue_task(mipmap, [tex = move(result), mipgen_task] -> Variant {
					return mipgen_task(tex);
				});

				mipmap->on_complete([=](Variant result) -> Variant {
					mipgen_complete(move(result));
				});
			};

			workers.enqueue_task(group, load_task);
			group->on_complete(load_complete);
		}
		doc.AddMember("images", images, allocator);
	}

	// Sources
	{
		Value sources(kArrayType);
		for (auto &texture : state.texture_cache)
		{
			Value t(kObjectType);
			t.AddMember("sampler", texture.sampler, allocator);
			t.AddMember("source", texture.image, allocator);
			sources.PushBack(t, allocator);
		}
		doc.AddMember("textures", sources, allocator);
	}

	// Materials
	{
		Value materials(kArrayType);
		for (auto &material : state.material_cache)
		{
			Value m(kObjectType);

			if (material.pipeline == DrawPipeline::AlphaBlend)
				m.AddMember("alphaMode", "BLEND", allocator);
			else if (material.pipeline == DrawPipeline::AlphaTest)
				m.AddMember("alphaMode", "MASK", allocator);

			if (material.two_sided)
				m.AddMember("doubleSided", true, allocator);

			if (any(notEqual(material.uniform_emissive_color, vec3(0.0f))))
			{
				Value emissive(kArrayType);
				for (unsigned i = 0; i < 3; i++)
					emissive.PushBack(material.uniform_emissive_color[i], allocator);
				m.AddMember("emissiveFactor", emissive, allocator);
			}

			Value pbr(kObjectType);
			if (material.uniform_roughness != 1.0f)
				pbr.AddMember("roughnessFactor", material.uniform_roughness, allocator);
			if (material.uniform_metallic != 1.0f)
				pbr.AddMember("metallicFactor", material.uniform_metallic, allocator);

			if (any(notEqual(material.uniform_base_color, vec4(1.0f))))
			{
				Value base(kArrayType);
				for (unsigned i = 0; i < 4; i++)
					base.PushBack(material.uniform_base_color[i], allocator);
				pbr.AddMember("baseColorFactor", base, allocator);
			}

			if (material.base_color >= 0)
			{
				Value base(kObjectType);
				base.AddMember("index", material.base_color, allocator);
				pbr.AddMember("baseColorTexture", base, allocator);
			}

			if (material.metallic_roughness >= 0)
			{
				Value mr(kObjectType);
				mr.AddMember("index", material.metallic_roughness, allocator);
				pbr.AddMember("metallicRoughnessTexture", mr, allocator);
			}

			m.AddMember("pbrMetallicRoughness", pbr, allocator);

			if (material.normal >= 0)
			{
				Value n(kObjectType);
				n.AddMember("index", material.normal, allocator);
				n.AddMember("scale", material.normal_scale, allocator);
				m.AddMember("normalTexture", n, allocator);
			}

			if (material.emissive >= 0)
			{
				Value e(kObjectType);
				e.AddMember("index", material.emissive, allocator);
				m.AddMember("emissiveTexture", e, allocator);
			}

			if (material.occlusion >= 0)
			{
				Value e(kObjectType);
				e.AddMember("index", material.occlusion, allocator);
				m.AddMember("occlusionTexture", e, allocator);
			}

			materials.PushBack(m, allocator);
		}
		doc.AddMember("materials", materials, allocator);
	}

	// Meshes
	{
		Value meshes(kArrayType);
		for (auto &mesh : state.mesh_group_cache)
		{
			Value m(kObjectType);
			Value primitives(kArrayType);

			for (auto &submesh : mesh)
			{
				Value prim(kObjectType);
				Value attribs(kObjectType);

				auto &m = state.mesh_cache[submesh];

				for_each_bit(m.attribute_mask, [&](unsigned bit) {
					auto attr = static_cast<MeshAttribute>(bit);
					const char *semantic = nullptr;
					switch (attr)
					{
					case MeshAttribute::Position:
						semantic = "POSITION";
						break;
					case MeshAttribute::Normal:
						semantic = "NORMAL";
						break;
					case MeshAttribute::BoneWeights:
						semantic = "WEIGHTS_0";
						break;
					case MeshAttribute::BoneIndex:
						semantic = "JOINTS_0";
						break;
					case MeshAttribute::VertexColor:
						semantic = "COLOR_0";
						break;
					case MeshAttribute::Tangent:
						semantic = "TANGENT";
						break;
					case MeshAttribute::UV:
						semantic = "TEXCOORD_0";
						break;
					default:
						return;
					}
					attribs.AddMember(StringRef(semantic), m.attribute_accessor[bit], allocator);
				});

				if (m.index_accessor >= 0)
					prim.AddMember("indices", m.index_accessor, allocator);

				if (m.material >= 0)
					prim.AddMember("material", state.material.to_index[m.material], allocator);

				prim.AddMember("attributes", attribs, allocator);
				primitives.PushBack(prim, allocator);
			}
			m.AddMember("primitives", primitives, allocator);
			meshes.PushBack(m, allocator);
		}
		doc.AddMember("meshes", meshes, allocator);
	}

	// Cameras
	{
		Value cameras(kArrayType);
		for (auto &camera : scene.cameras)
		{
			Value cam(kObjectType);
			if (camera.type == CameraInfo::Type::Perspective)
			{
				cam.AddMember("type", "perspective", allocator);
				Value perspective(kObjectType);
				perspective.AddMember("aspectRatio", camera.aspect_ratio, allocator);
				perspective.AddMember("yfov", camera.yfov, allocator);
				perspective.AddMember("znear", camera.znear, allocator);
				perspective.AddMember("zfar", camera.zfar, allocator);
				cam.AddMember("perspective", perspective, allocator);
			}
			else if (camera.type == CameraInfo::Type::Orthographic)
			{
				cam.AddMember("type", "orthographic", allocator);
				Value ortho(kObjectType);
				ortho.AddMember("xmag", camera.xmag, allocator);
				ortho.AddMember("ymag", camera.ymag, allocator);
				ortho.AddMember("znear", camera.znear, allocator);
				ortho.AddMember("zfar", camera.zfar, allocator);
				cam.AddMember("orthographic", ortho, allocator);
			}
			cameras.PushBack(cam, allocator);
		}
		doc.AddMember("cameras", cameras, allocator);
	}

	// Lights
	if (!scene.lights.empty())
	{
		Value ext(kObjectType);
		Value lights_cmn(kObjectType);
		Value lights(kArrayType);

		for (auto &light : scene.lights)
		{
			Value l(kObjectType);
			Value positional(kObjectType);
			Value color(kArrayType);
			Value spot(kObjectType);

			color.PushBack(light.color.x, allocator);
			color.PushBack(light.color.y, allocator);
			color.PushBack(light.color.z, allocator);
			l.AddMember("color", color, allocator);

			switch (light.type)
			{
			case LightInfo::Type::Spot:
				l.AddMember("type", "spot", allocator);
				l.AddMember("profile", "CMN", allocator);
				if (light.constant_falloff != 0.0f)
					positional.AddMember("constantAttenuation", light.constant_falloff, allocator);
				if (light.linear_falloff != 0.0f)
					positional.AddMember("linearAttenuation", light.linear_falloff, allocator);
				if (light.quadratic_falloff != 0.0f)
					positional.AddMember("quadraticAttenuation", light.quadratic_falloff, allocator);

				spot.AddMember("innerAngle", glm::sqrt(std::max(1.0f - light.inner_cone * light.inner_cone, 0.0f)), allocator);
				spot.AddMember("outerAngle", glm::sqrt(std::max(1.0f - light.outer_cone * light.outer_cone, 0.0f)), allocator);
				positional.AddMember("spot", spot, allocator);

				l.AddMember("positional", positional, allocator);
				break;

			case LightInfo::Type::Point:
				l.AddMember("type", "point", allocator);
				l.AddMember("profile", "CMN", allocator);
				if (light.constant_falloff != 0.0f)
					positional.AddMember("constantAttenuation", light.constant_falloff, allocator);
				if (light.linear_falloff != 0.0f)
					positional.AddMember("linearAttenuation", light.linear_falloff, allocator);
				if (light.quadratic_falloff != 0.0f)
					positional.AddMember("quadraticAttenuation", light.quadratic_falloff, allocator);
				l.AddMember("positional", positional, allocator);
				break;

			case LightInfo::Type::Directional:
				l.AddMember("type", "directional", allocator);
				l.AddMember("profile", "CMN", allocator);
				break;

			case LightInfo::Type::Ambient:
				l.AddMember("type", "ambient", allocator);
			}

			lights.PushBack(l, allocator);
		}

		lights_cmn.AddMember("lights", lights, allocator);
		ext.AddMember("KHR_lights_cmn", lights_cmn, allocator);
		doc.AddMember("extensions", ext, allocator);
	}

	StringBuffer buffer;
	//PrettyWriter<StringBuffer> writer(buffer);
	Writer<StringBuffer> writer(buffer);
	doc.Accept(writer);

	const auto aligned_size = [](size_t size) {
		return (size + 3) & ~size_t(3);
	};

	const auto write_u32 = [](uint8_t *data, uint32_t v) {
		memcpy(data, &v, sizeof(uint32_t));
	};

	size_t glb_size = 12 + 8 + aligned_size(buffer.GetLength()) + 8 + aligned_size(state.glb_buffer_data.size());

	auto file = Filesystem::get().open(path, FileMode::WriteOnly);
	if (!file)
	{
		LOGE("Failed to open file: %s\n", path.c_str());
		return false;
	}

	uint8_t *mapped = static_cast<uint8_t *>(file->map_write(glb_size));
	if (!mapped)
	{
		LOGE("Failed to map file: %s\n", path.c_str());
		return false;
	}

	memcpy(mapped, "glTF", 4);
	mapped += 4;
	write_u32(mapped, 2);
	mapped += 4;
	write_u32(mapped, glb_size);
	mapped += 4;

	write_u32(mapped, aligned_size(buffer.GetLength()));
	mapped += 4;
	memcpy(mapped, "JSON", 4);
	mapped += 4;

	memcpy(mapped, buffer.GetString(), buffer.GetLength());
	size_t pad_length = aligned_size(buffer.GetLength()) - buffer.GetLength();
	memset(mapped + buffer.GetLength(), ' ', pad_length);
	mapped += aligned_size(buffer.GetLength());

	write_u32(mapped, aligned_size(state.glb_buffer_data.size()));
	mapped += 4;
	memcpy(mapped, "BIN\0", 4);
	mapped += 4;
	memcpy(mapped, state.glb_buffer_data.data(), state.glb_buffer_data.size());
	pad_length = aligned_size(state.glb_buffer_data.size()) - state.glb_buffer_data.size();
	memset(mapped + state.glb_buffer_data.size(), 0, pad_length);

	file->unmap();
	return true;
}
}
}