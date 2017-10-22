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

#define RAPIDJSON_ASSERT(x) do { if (!(x)) throw "JSON error"; } while(0)
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/istreamwrapper.h"
#include "hashmap.hpp"
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

	AABB aabb;
	bool normalized = false;
	bool use_aabb = false;
};

struct EmittedMaterial
{

};

struct RemapState
{
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

	HashMap<unsigned> mesh_group_hash;
	vector<vector<unsigned>> mesh_group_cache;
};

static Hash hash(RemapState &state, const Mesh &mesh)
{
	Hasher h;

	h.u32(mesh.topology);
	h.u32(mesh.index_type);
	h.u32(mesh.attribute_stride);
	h.u32(mesh.position_stride);
	h.u32(mesh.has_material);
	if (mesh.has_material)
		h.u32(state.material.to_index[mesh.material_index]);
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

static Hash hash(RemapState &state, const MaterialInfo &mat)
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
static void filter_input(RemapState &state, StateType &output, const SceneType &input)
{
	for (auto &i : input)
	{
		auto h = hash(state, i);
		auto itr = output.hashmap.find(h);
		if (itr != end(output.hashmap))
			output.to_index.push_back(itr->second);
		else
		{
			unsigned index = output.to_index.size();
			output.to_index.push_back(output.info.size());
			output.info.push_back(&i);
			output.hashmap[h] = index;
		}
	}
}

static unsigned emit_buffer(RemapState &state, ArrayView<const uint8_t> view)
{
	Hasher h;
	h.data(view.data(), view.size());
	auto itr = state.buffer_hash.find(h.get());

	if (itr == end(state.buffer_hash))
	{
		unsigned index = state.buffer_views.size();
		size_t offset = state.glb_buffer_data.size();
		offset = (offset + 15) & ~15;
		state.glb_buffer_data.resize(offset + view.size());
		memcpy(state.glb_buffer_data.data() + offset, view.data(), view.size());
		state.buffer_views.push_back({offset, view.size()});
		state.buffer_hash[h.get()] = index;
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

static unsigned emit_accessor(RemapState &state, unsigned view_index,
                              VkFormat format, unsigned offset, unsigned stride, unsigned count)
{
	Hasher h;
	h.u32(view_index);
	h.u32(format);
	h.u32(offset);
	h.u32(stride);
	h.u32(count);

	auto itr = state.accessor_hash.find(h.get());
	if (itr == end(state.accessor_hash))
	{
		unsigned index = state.accessor_cache.size();
		EmittedAccessor acc = {};
		acc.count = count;
		acc.view = view_index;
		set_accessor_type(acc, format);

		state.accessor_cache.push_back(acc);
		state.accessor_hash[h.get()] = index;
		return index;
	}
	else
		return itr->second;
}

static void emit_material(RemapState &state, unsigned remapped_material)
{
	auto &material = *state.material.info[remapped_material];
	state.material_cache.resize(std::max<size_t>(state.material_cache.size(), remapped_material + 1));
}

static void emit_mesh(RemapState &state, unsigned remapped_index)
{
	auto &mesh = *state.mesh.info[remapped_index];
	state.mesh_cache.resize(std::max<size_t>(state.mesh_cache.size(), remapped_index + 1));

	auto &emit = state.mesh_cache[remapped_index];
	emit.material = mesh.has_material ? int(mesh.material_index) : -1;

	if (!mesh.indices.empty())
	{
		unsigned index = emit_buffer(state, mesh.indices);
		emit.index_accessor = emit_accessor(state, index,
		                                    mesh.index_type == VK_INDEX_TYPE_UINT16 ? VK_FORMAT_R16_UINT
		                                                                            : VK_FORMAT_R32_UINT,
		                                    0, mesh.index_type == VK_INDEX_TYPE_UINT16 ? 2 : 4, mesh.count);
	}
	else
		emit.index_accessor = -1;

	if (mesh.has_material)
	{
		unsigned remapped_material = state.material.to_index[mesh.material_index];
		if (!state.material_hash.count(remapped_material))
		{
			emit_material(state, remapped_material);
			state.material_hash.insert(remapped_material);
		}
	}

	unsigned position_buffer = 0;
	unsigned attribute_buffer = 0;
	if (!mesh.positions.empty())
		position_buffer = emit_buffer(state, mesh.positions);
	if (!mesh.attributes.empty())
		attribute_buffer = emit_buffer(state, mesh.attributes);

	emit.attribute_mask = 0;
	for (unsigned i = 0; i < ecast(MeshAttribute::Count); i++)
	{
		if (mesh.attribute_layout[i].format == VK_FORMAT_UNDEFINED)
			continue;

		emit.attribute_mask |= 1u << i;

		if (i == ecast(MeshAttribute::Position))
		{
			emit.attribute_accessor[i] = emit_accessor(state, position_buffer,
			                                           mesh.attribute_layout[i].format,
			                                           mesh.attribute_layout[i].offset, mesh.position_stride,
			                                           mesh.positions.size() / mesh.position_stride);

			state.accessor_cache[emit.attribute_accessor[i]].aabb = mesh.static_aabb;
			state.accessor_cache[emit.attribute_accessor[i]].use_aabb = true;
		}
		else
		{
			emit.attribute_accessor[i] = emit_accessor(state, attribute_buffer,
			                                           mesh.attribute_layout[i].format,
			                                           mesh.attribute_layout[i].offset, mesh.attribute_stride,
			                                           mesh.attributes.size() / mesh.attribute_stride);
		}
	}
}

static unsigned emit_meshes(RemapState &state, ArrayView<const unsigned> meshes)
{
	Hasher emit_hash;
	vector<unsigned> mesh_group;
	mesh_group.reserve(meshes.size());

	for (auto &mesh : meshes)
	{
		unsigned remapped_index = state.mesh.to_index[mesh];
		emit_hash.u32(remapped_index);
		mesh_group.push_back(remapped_index);

		if (!state.mesh_hash.count(remapped_index))
		{
			emit_mesh(state, remapped_index);
			state.mesh_hash.insert(remapped_index);
		}
	}

	unsigned index;
	auto itr = state.mesh_group_hash.find(emit_hash.get());
	if (itr == end(state.mesh_group_hash))
	{
		index = state.mesh_group_cache.size();
		state.mesh_group_cache.push_back(move(mesh_group));
		state.mesh_group_hash[emit_hash.get()] = index;
	}
	else
		index = itr->second;

	return index;
}

bool export_scene_to_glb(const SceneInformation &scene, const string &path)
{
	Document doc;
	doc.SetObject();
	auto &allocator = doc.GetAllocator();

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
	filter_input(state, state.material, scene.materials);
	filter_input(state, state.mesh, scene.meshes);

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
			n.AddMember("mesh", emit_meshes(state, node.meshes), allocator);

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
			acc.AddMember("type", string(accessor.type), allocator);
			acc.AddMember("count", accessor.count, allocator);
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

	StringBuffer buffer;
	PrettyWriter<StringBuffer> writer(buffer);
	doc.Accept(writer);

	auto file = Filesystem::get().open(path, FileMode::WriteOnly);
	if (!file)
	{
		LOGE("Failed to open file: %s\n", path.c_str());
		return false;
	}

	void *mapped = file->map_write(buffer.GetLength());
	if (!mapped)
	{
		LOGE("Failed to map file: %s\n", path.c_str());
		return false;
	}

	memcpy(mapped, buffer.GetString(), buffer.GetLength());
	file->unmap();
	return true;
}
}
}