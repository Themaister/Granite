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

#include "scene_formats.hpp"
#include <string.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <mikktspace/mikktspace.h>
#include "meshoptimizer.h"
#include "mikktspace.h"

using namespace Util;
using namespace std;

namespace Granite
{
namespace SceneFormats
{
static vec3 compute_normal(const vec3 &a, const vec3 &b, const vec3 &c)
{
	vec3 n = cross(b - a, c - a);
	n = normalize(n);
	return n;
}

struct IndexRemapping
{
	std::vector<unsigned> index_remap;
	std::vector<unsigned> unique_attrib_to_source_index;
};

// Find duplicate indices.
static IndexRemapping build_index_remap_list(const Mesh &mesh)
{
	unsigned attribute_count = unsigned(mesh.positions.size() / mesh.position_stride);
	unordered_map<Hash, unsigned> attribute_remapper;
	IndexRemapping remapped;
	remapped.index_remap.reserve(attribute_count);

	unsigned unique_count = 0;
	for (unsigned i = 0; i < attribute_count; i++)
	{
		Hasher h;
		h.data(mesh.positions.data() + i * mesh.position_stride, mesh.position_stride);
		if (!mesh.attributes.empty())
			h.data(mesh.attributes.data() + i * mesh.attribute_stride, mesh.attribute_stride);

		auto hash = h.get();
		auto itr = attribute_remapper.find(hash);
		if (itr != end(attribute_remapper))
		{
			remapped.index_remap.push_back(itr->second);
		}
		else
		{
			attribute_remapper[hash] = unique_count;
			remapped.index_remap.push_back(unique_count);
			remapped.unique_attrib_to_source_index.push_back(i);
			unique_count++;
		}
	}

	return remapped;
}

static vector<uint32_t> build_canonical_index_buffer(const Mesh &mesh, const vector<unsigned> &index_remap)
{
	vector<uint32_t> index_buffer;
	if (mesh.indices.empty())
	{
		index_buffer.reserve(mesh.count);
		for (unsigned i = 0; i < mesh.count; i++)
			index_buffer.push_back(index_remap[i]);
	}
	else if (mesh.index_type == VK_INDEX_TYPE_UINT32)
	{
		index_buffer.reserve(mesh.count);
		for (unsigned i = 0; i < mesh.count; i++)
			index_buffer.push_back(index_remap[reinterpret_cast<const uint32_t *>(mesh.indices.data())[i]]);
	}
	else if (mesh.index_type == VK_INDEX_TYPE_UINT16)
	{
		index_buffer.reserve(mesh.count);
		for (unsigned i = 0; i < mesh.count; i++)
			index_buffer.push_back(index_remap[reinterpret_cast<const uint16_t *>(mesh.indices.data())[i]]);
	}

	return index_buffer;
}

static void rebuild_new_attributes_remap_src(vector<uint8_t> &positions, unsigned position_stride,
                                             vector<uint8_t> &attributes, unsigned attribute_stride,
                                             const vector<uint8_t> &source_positions, const vector<uint8_t> &source_attributes,
                                             const vector<uint32_t> &unique_attrib_to_source_index)
{
	vector<uint8_t> new_positions;
	vector<uint8_t> new_attributes;

	new_positions.resize(position_stride * unique_attrib_to_source_index.size());
	if (attribute_stride)
		new_attributes.resize(attribute_stride * unique_attrib_to_source_index.size());

	size_t count = unique_attrib_to_source_index.size();
	for (size_t i = 0; i < count; i++)
	{
		memcpy(new_positions.data() + i * position_stride,
		       source_positions.data() + unique_attrib_to_source_index[i] * position_stride,
		       position_stride);

		if (attribute_stride)
		{
			memcpy(new_attributes.data() + i * attribute_stride,
			       source_attributes.data() + unique_attrib_to_source_index[i] * attribute_stride,
			       attribute_stride);
		}
	}

	positions = move(new_positions);
	attributes = move(new_attributes);
}

static void rebuild_new_attributes_remap_dst(vector<uint8_t> &positions, unsigned position_stride,
                                             vector<uint8_t> &attributes, unsigned attribute_stride,
                                             const vector<uint8_t> &source_positions, const vector<uint8_t> &source_attributes,
                                             const vector<uint32_t> &unique_attrib_to_dest_index)
{
	vector<uint8_t> new_positions;
	vector<uint8_t> new_attributes;

	new_positions.resize(position_stride * unique_attrib_to_dest_index.size());
	if (attribute_stride)
		new_attributes.resize(attribute_stride * unique_attrib_to_dest_index.size());

	size_t count = unique_attrib_to_dest_index.size();
	for (size_t i = 0; i < count; i++)
	{
		memcpy(new_positions.data() + unique_attrib_to_dest_index[i] * position_stride,
		       source_positions.data() + i * position_stride,
		       position_stride);

		if (attribute_stride)
		{
			memcpy(new_attributes.data() + unique_attrib_to_dest_index[i] * attribute_stride,
			       source_attributes.data() + i * attribute_stride,
			       attribute_stride);
		}
	}

	positions = move(new_positions);
	attributes = move(new_attributes);
}

static vector<uint32_t> remap_indices(const vector<uint32_t> &indices, const vector<uint32_t> &remap_table)
{
	vector<uint32_t> remapped;
	remapped.reserve(indices.size());
	for (auto &i : indices)
		remapped.push_back(remap_table[i]);
	return remapped;
}

static bool mesh_unroll_vertices(Mesh &mesh)
{
	if (mesh.topology != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
		return false;
	if (mesh.indices.empty())
		return true;

	vector<uint8_t> positions(mesh.count * mesh.position_stride);
	vector<uint8_t> attributes(mesh.count * mesh.attribute_stride);

	if (mesh.index_type == VK_INDEX_TYPE_UINT32)
	{
		const uint32_t *ibo = reinterpret_cast<const uint32_t *>(mesh.indices.data());
		for (unsigned i = 0; i < mesh.count; i++)
		{
			uint32_t index = ibo[i];
			memcpy(positions.data() + i * mesh.position_stride,
			       mesh.positions.data() + index * mesh.position_stride,
			       mesh.position_stride);
			memcpy(attributes.data() + i * mesh.attribute_stride,
			       mesh.attributes.data() + index * mesh.attribute_stride,
			       mesh.attribute_stride);
		}
	}
	else if (mesh.index_type == VK_INDEX_TYPE_UINT16)
	{
		const uint16_t *ibo = reinterpret_cast<const uint16_t *>(mesh.indices.data());
		for (unsigned i = 0; i < mesh.count; i++)
		{
			uint16_t index = ibo[i];
			memcpy(positions.data() + i * mesh.position_stride,
			       mesh.positions.data() + index * mesh.position_stride,
			       mesh.position_stride);
			memcpy(attributes.data() + i * mesh.attribute_stride,
			       mesh.attributes.data() + index * mesh.attribute_stride,
			       mesh.attribute_stride);
		}
	}

	mesh.positions = move(positions);
	mesh.attributes = move(attributes);
	mesh.indices.clear();
	return true;
}

void mesh_deduplicate_vertices(Mesh &mesh)
{
	auto index_remap = build_index_remap_list(mesh);
	auto index_buffer = build_canonical_index_buffer(mesh, index_remap.index_remap);
	rebuild_new_attributes_remap_src(mesh.positions, mesh.position_stride,
	                                 mesh.attributes, mesh.attribute_stride,
	                                 mesh.positions, mesh.attributes, index_remap.unique_attrib_to_source_index);

	mesh.index_type = VK_INDEX_TYPE_UINT32;
	mesh.indices.resize(index_buffer.size() * sizeof(uint32_t));
	size_t count = index_buffer.size();
	for (size_t i = 0; i < count; i++)
		reinterpret_cast<uint32_t *>(mesh.indices.data())[i] = index_buffer[i];
	mesh.count = unsigned(index_buffer.size());
}

Mesh mesh_optimize_index_buffer(const Mesh &mesh, bool stripify)
{
	if (mesh.topology != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
		return mesh;

	Mesh optimized;
	optimized.position_stride = mesh.position_stride;
	optimized.attribute_stride = mesh.attribute_stride;

	// Remove redundant indices and rewrite index and attribute buffers.
	auto index_remap = build_index_remap_list(mesh);
	auto index_buffer = build_canonical_index_buffer(mesh, index_remap.index_remap);
	rebuild_new_attributes_remap_src(optimized.positions, optimized.position_stride,
	                                 optimized.attributes, optimized.attribute_stride,
	                                 mesh.positions, mesh.attributes, index_remap.unique_attrib_to_source_index);

	size_t vertex_count = optimized.positions.size() / optimized.position_stride;

	// Optimize for vertex cache.
	meshopt_optimizeVertexCache(index_buffer.data(), index_buffer.data(), index_buffer.size(),
	                            vertex_count);

	// Remap vertex fetch to get contiguous indices as much as possible.
	vector<uint32_t> remap_table(optimized.positions.size() / optimized.position_stride);
	meshopt_optimizeVertexFetchRemap(remap_table.data(), index_buffer.data(), index_buffer.size(), vertex_count);
	index_buffer = remap_indices(index_buffer, remap_table);
	rebuild_new_attributes_remap_dst(optimized.positions, optimized.position_stride,
	                                 optimized.attributes, optimized.attribute_stride,
	                                 optimized.positions, optimized.attributes, remap_table);

	optimized.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	optimized.primitive_restart = false;

	if (stripify)
	{
		// Try to stripify the mesh. If we end up with fewer indices, use that.
		vector<uint32_t> stripped_index_buffer((index_buffer.size() / 3) * 4);
		size_t stripped_index_count = meshopt_stripify(stripped_index_buffer.data(),
		                                               index_buffer.data(), index_buffer.size(),
		                                               vertex_count, ~0u);

		stripped_index_buffer.resize(stripped_index_count);
		if (stripped_index_count < index_buffer.size())
		{
			optimized.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
			index_buffer = move(stripped_index_buffer);
			optimized.primitive_restart = true;
		}
	}

	uint32_t max_index = 0;
	for (auto &i : index_buffer)
		if (i != ~0u)
			max_index = muglm::max(max_index, i);

	if (max_index <= 0xffff) // 16-bit indices are enough.
	{
		optimized.index_type = VK_INDEX_TYPE_UINT16;
		optimized.indices.resize(index_buffer.size() * sizeof(uint16_t));
		size_t count = index_buffer.size();
		for (size_t i = 0; i < count; i++)
		{
			reinterpret_cast<uint16_t *>(optimized.indices.data())[i] =
					index_buffer[i] == ~0u ? uint16_t(0xffffu) : uint16_t(index_buffer[i]);
		}
	}
	else
	{
		optimized.index_type = VK_INDEX_TYPE_UINT32;
		optimized.indices.resize(index_buffer.size() * sizeof(uint32_t));
		size_t count = index_buffer.size();
		for (size_t i = 0; i < count; i++)
			reinterpret_cast<uint32_t *>(optimized.indices.data())[i] = index_buffer[i];
	}

	optimized.count = unsigned(index_buffer.size());

	memcpy(optimized.attribute_layout, mesh.attribute_layout, sizeof(mesh.attribute_layout));
	optimized.material_index = mesh.material_index;
	optimized.has_material = mesh.has_material;
	optimized.static_aabb = mesh.static_aabb;

	return optimized;
}

bool mesh_recompute_tangents(Mesh &mesh)
{
	if (mesh.attribute_layout[ecast(MeshAttribute::Tangent)].format != VK_FORMAT_R32G32B32A32_SFLOAT)
	{
		LOGE("Unsupported format for tangents.\n");
		return false;
	}

	if (mesh.attribute_layout[ecast(MeshAttribute::Normal)].format != VK_FORMAT_R32G32B32_SFLOAT)
	{
		LOGE("Unsupported format for normals.\n");
		return false;
	}

	if (mesh.attribute_layout[ecast(MeshAttribute::UV)].format != VK_FORMAT_R32G32_SFLOAT)
	{
		LOGE("Unsupported format for UVs.\n");
		return false;
	}

	if (!mesh_unroll_vertices(mesh))
		return false;

	SMikkTSpaceInterface iface = {};

	iface.m_getNumFaces = [](const SMikkTSpaceContext *ctx) -> int {
		const Mesh *m = static_cast<const Mesh *>(ctx->m_pUserData);
		return m->count / 3;
	};

	iface.m_getNumVerticesOfFace = [](const SMikkTSpaceContext *, const int) -> int {
		return 3;
	};

	iface.m_getNormal = [](const SMikkTSpaceContext *ctx, float normals[],
	                       const int face_index, const int vert_index) {
		int i = face_index * 3 + vert_index;
		const Mesh *m = static_cast<const Mesh *>(ctx->m_pUserData);
		memcpy(normals, m->attributes.data() + i * m->attribute_stride +
		                m->attribute_layout[ecast(MeshAttribute::Normal)].offset,
		       sizeof(vec3));
	};

	iface.m_getTexCoord = [](const SMikkTSpaceContext *ctx, float normals[],
	                         const int face_index, const int vert_index) {
		int i = face_index * 3 + vert_index;
		const Mesh *m = static_cast<const Mesh *>(ctx->m_pUserData);
		memcpy(normals, m->attributes.data() + i * m->attribute_stride +
		                m->attribute_layout[ecast(MeshAttribute::UV)].offset,
		       sizeof(vec2));
	};

	iface.m_getPosition = [](const SMikkTSpaceContext *ctx, float positions[],
	                         const int face_index, const int vert_index) {
		int i = face_index * 3 + vert_index;
		const Mesh *m = static_cast<const Mesh *>(ctx->m_pUserData);
		memcpy(positions, m->positions.data() + i * m->position_stride,
		       sizeof(vec3));
	};

	iface.m_setTSpaceBasic = [](const SMikkTSpaceContext *ctx, const float tangent[], const float sign,
	                            const int face_index, const int vert_index) {
		int i = face_index * 3 + vert_index;
		Mesh *m = static_cast<Mesh *>(ctx->m_pUserData);
		// Invert the sign because of glTF convention.
		vec4 t(tangent[0], tangent[1], tangent[2], -sign);
		memcpy(m->attributes.data() + i * m->attribute_stride +
		       m->attribute_layout[ecast(MeshAttribute::Tangent)].offset,
		       &t,
		       sizeof(vec4));
	};

	SMikkTSpaceContext ctx;
	ctx.m_pUserData = &mesh;
	ctx.m_pInterface = &iface;
	genTangSpaceDefault(&ctx);

	mesh_deduplicate_vertices(mesh);
	return true;
}

template <typename T, typename Op>
static void mesh_transform_attribute(Mesh &mesh, const Op &op, uint32_t offset)
{
	size_t count = mesh.attributes.size() / mesh.attribute_stride;
	for (size_t i = 0; i < count; i++)
	{
		auto &attr = *reinterpret_cast<T *>(mesh.attributes.data() + i * mesh.attribute_stride + offset);
		attr = op(attr);
	}
}

bool mesh_renormalize_normals(Mesh &mesh)
{
	auto &n = mesh.attribute_layout[ecast(MeshAttribute::Normal)];
	if (n.format == VK_FORMAT_UNDEFINED)
		return false;
	if (n.format != VK_FORMAT_R32G32B32_SFLOAT)
	{
		LOGI("Found normal, but got format: %u\n", unsigned(n.format));
		return false;
	}

	mesh_transform_attribute<vec3>(mesh, [](const vec3 &v) -> vec3 {
		float sqr = dot(v, v);
		if (sqr < 0.000001f)
		{
			LOGI("Found degenerate normal.\n");
			return vec3(1.0f, 0.0f, 0.0f);
		}
		else
			return normalize(v);
	}, n.offset);
	return true;
}

bool mesh_renormalize_tangents(Mesh &mesh)
{
	auto &t = mesh.attribute_layout[ecast(MeshAttribute::Tangent)];
	if (t.format == VK_FORMAT_UNDEFINED)
		return false;
	if (t.format != VK_FORMAT_R32G32B32A32_SFLOAT)
	{
		LOGI("Found tangent, but got format: %u\n", unsigned(t.format));
		return false;
	}

	mesh_transform_attribute<vec3>(mesh, [](const vec3 &v) -> vec3 {
		float sqr = dot(v, v);
		if (sqr < 0.000001f)
		{
			LOGI("Found degenerate tangent.\n");
			return vec3(1.0f, 0.0f, 0.0f);
		}
		else
			return normalize(v);
	}, t.offset);
	return true;
}

bool mesh_flip_tangents_w(Mesh &mesh)
{
	auto &t = mesh.attribute_layout[ecast(MeshAttribute::Tangent)];
	if (t.format == VK_FORMAT_UNDEFINED)
		return false;
	if (t.format != VK_FORMAT_R32G32B32A32_SFLOAT)
	{
		LOGI("Found tangent, but got format: %u\n", unsigned(t.format));
		return false;
	}

	size_t count = mesh.attributes.size() / mesh.attribute_stride;
	for (size_t i = 0; i < count; i++)
		reinterpret_cast<vec4 *>(mesh.attributes.data() + i * mesh.attribute_stride + t.offset)->w *= -1.0f;
	return true;
}

bool mesh_recompute_normals(Mesh &mesh)
{
	if (mesh.attribute_layout[ecast(MeshAttribute::Position)].format != VK_FORMAT_R32G32B32_SFLOAT &&
	    mesh.attribute_layout[ecast(MeshAttribute::Position)].format != VK_FORMAT_R32G32B32A32_SFLOAT)
	{
		LOGE("Unsupported format for position.\n");
		return false;
	}

	if (mesh.attribute_layout[ecast(MeshAttribute::Normal)].format != VK_FORMAT_R32G32B32_SFLOAT)
	{
		LOGE("Unsupported format for normals.\n");
		return false;
	}

	mesh_deduplicate_vertices(mesh);

	if (mesh.topology != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
	{
		LOGE("Unsupported primitive topology for normal computation.\n");
		return false;
	}

	auto attr_count = unsigned(mesh.attributes.size() / mesh.attribute_stride);
	unsigned normal_offset = mesh.attribute_layout[ecast(MeshAttribute::Normal)].offset;

	const auto get_normal = [&](unsigned i) -> vec3 & {
		return *reinterpret_cast<vec3 *>(mesh.attributes.data() + normal_offset + i * mesh.attribute_stride);
	};

	const auto get_position = [&](unsigned i) -> const vec3 & {
		return *reinterpret_cast<const vec3 *>(mesh.positions.data() + i * mesh.position_stride);
	};

	for (unsigned i = 0; i < attr_count; i++)
		get_normal(i) = vec3(0.0f);

	uint32_t count = mesh.count;
	uint32_t primitives = count / 3;

	const auto accumulate_normals = [&](const auto &op) {
		for (unsigned i = 0; i < primitives; i++)
		{
			vec3 pos[3];
			for (unsigned j = 0; j < 3; j++)
			{
				unsigned index = op(3 * i + j);
				pos[j] = get_position(index);
			}

			vec3 n = compute_normal(pos[0], pos[1], pos[2]);

			for (unsigned j = 0; j < 3; j++)
			{
				unsigned index = op(3 * i + j);
				get_normal(index) += n;
			}
		}
	};

	if (mesh.indices.empty())
	{
		accumulate_normals([&](unsigned i) {
			return i;
		});
	}
	else if (mesh.index_type == VK_INDEX_TYPE_UINT16)
	{
		auto *ibo = reinterpret_cast<uint16_t *>(mesh.indices.data());
		accumulate_normals([&](unsigned i) {
			return ibo[i];
		});
	}
	else if (mesh.index_type == VK_INDEX_TYPE_UINT32)
	{
		auto *ibo = reinterpret_cast<uint32_t *>(mesh.indices.data());
		accumulate_normals([&](unsigned i) {
			return ibo[i];
		});
	}

	for (unsigned i = 0; i < attr_count; i++)
	{
		auto &n = get_normal(i);
		n = normalize(n);
	}

	return true;
}

static void touch_node_children(unordered_set<uint32_t> &touched, const vector<Node> &nodes, uint32_t index)
{
	touched.insert(index);
	for (auto &child : nodes[index].children)
	{
		touched.insert(child);
		touch_node_children(touched, nodes, child);
	}
}

unordered_set<uint32_t> build_used_nodes_in_scene(const SceneNodes &scene, const vector<Node> &nodes)
{
	unordered_set<uint32_t> touched;
	for (auto &node : scene.node_indices)
		touch_node_children(touched, nodes, node);
	return touched;
}

bool extract_collision_mesh(CollisionMesh &col, const Mesh &mesh)
{
	if (mesh.topology != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
		return false;

	col.indices.clear();
	col.positions.clear();

	size_t vertex_count = mesh.positions.size() / mesh.position_stride;
	col.positions.reserve(vertex_count);

	switch (mesh.attribute_layout[ecast(MeshAttribute::Position)].format)
	{
	case VK_FORMAT_R32G32B32_SFLOAT:
	case VK_FORMAT_R32G32B32A32_SFLOAT:
		for (size_t i = 0; i < vertex_count; i++)
		{
			const auto *v = reinterpret_cast<const vec3 *>(mesh.positions.data() + i * mesh.position_stride);
			col.positions.emplace_back(*v, 1.0f);
		}
		break;

	default:
		return false;
	}

	if (mesh.indices.empty())
	{
		col.indices.reserve(vertex_count);
		for (size_t i = 0; i < vertex_count; i++)
			col.indices.push_back(uint32_t(i));
	}
	else if (mesh.index_type == VK_INDEX_TYPE_UINT16)
	{
		col.indices.reserve(mesh.count);
		for (unsigned i = 0; i < mesh.count; i++)
			col.indices.push_back(reinterpret_cast<const uint16_t *>(mesh.indices.data())[i]);
	}
	else if (mesh.index_type == VK_INDEX_TYPE_UINT32)
	{
		col.indices.reserve(mesh.count);
		for (unsigned i = 0; i < mesh.count; i++)
			col.indices.push_back(reinterpret_cast<const uint32_t *>(mesh.indices.data())[i]);
	}
	else
		return false;

	return true;
}
}
}
