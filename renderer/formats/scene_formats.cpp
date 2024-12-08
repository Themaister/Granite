/* Copyright (c) 2017-2024 Hans-Kristian Arntzen
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

#define NOMINMAX
#include "scene_formats.hpp"
#include <string.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "mikktspace.h"
#include "meshoptimizer.h"

using namespace Util;

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
	std::vector<uint32_t> index_remap;
	std::vector<uint32_t> unique_attrib_to_source_index;
};

// Find duplicate indices.
static IndexRemapping build_attribute_remap_indices(const Mesh &mesh)
{
	auto attribute_count = unsigned(mesh.positions.size() / mesh.position_stride);
	struct RemappedAttribute
	{
		unsigned unique_index;
		unsigned source_index;
	};
	std::unordered_map<Hash, RemappedAttribute> attribute_remapper;
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
		bool is_unique;

		if (itr != end(attribute_remapper))
		{
			bool match = true;
			if (memcmp(mesh.positions.data() + i * mesh.position_stride,
			           mesh.positions.data() + itr->second.source_index * mesh.position_stride,
			           mesh.position_stride) != 0)
			{
				match = false;
			}

			if (match && !mesh.attributes.empty() &&
			    memcmp(mesh.attributes.data() + i * mesh.attribute_stride,
			           mesh.attributes.data() + itr->second.source_index * mesh.attribute_stride,
			           mesh.attribute_stride) != 0)
			{
				match = false;
			}

			if (match)
				remapped.index_remap.push_back(itr->second.unique_index);
			else
				LOGW("Hash collision in vertex dedup.\n");

			is_unique = !match;
		}
		else
		{
			attribute_remapper[hash] = { unique_count, i };
			is_unique = true;
		}

		if (is_unique)
		{
			remapped.index_remap.push_back(unique_count);
			remapped.unique_attrib_to_source_index.push_back(i);
			unique_count++;
		}
	}

	return remapped;
}

static std::vector<uint32_t> build_remapped_index_buffer(const Mesh &mesh, const std::vector<uint32_t> &index_remap)
{
	assert(mesh.topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST && mesh.index_type == VK_INDEX_TYPE_UINT32);

	std::vector<uint32_t> index_buffer;
	index_buffer.reserve(mesh.count);
	const auto *indices = reinterpret_cast<const uint32_t *>(mesh.indices.data());
	for (unsigned i = 0; i < mesh.count; i++)
		index_buffer.push_back(index_remap[indices[i]]);
	return index_buffer;
}

static void rebuild_new_attributes_remap_src(std::vector<uint8_t> &positions, unsigned position_stride,
                                             std::vector<uint8_t> &attributes, unsigned attribute_stride,
                                             const std::vector<uint8_t> &source_positions, const std::vector<uint8_t> &source_attributes,
                                             const std::vector<uint32_t> &unique_attrib_to_source_index)
{
	std::vector<uint8_t> new_positions;
	std::vector<uint8_t> new_attributes;

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

	positions = std::move(new_positions);
	attributes = std::move(new_attributes);
}

static void rebuild_new_attributes_remap_dst(std::vector<uint8_t> &positions, unsigned position_stride,
                                             std::vector<uint8_t> &attributes, unsigned attribute_stride,
                                             const std::vector<uint8_t> &source_positions, const std::vector<uint8_t> &source_attributes,
                                             const std::vector<uint32_t> &unique_attrib_to_dest_index,
                                             uint32_t vertex_count)
{
	std::vector<uint8_t> new_positions;
	std::vector<uint8_t> new_attributes;

	new_positions.resize(position_stride * vertex_count);
	if (attribute_stride)
		new_attributes.resize(attribute_stride * vertex_count);

	size_t count = unique_attrib_to_dest_index.size();
	for (size_t i = 0; i < count; i++)
	{
		if (unique_attrib_to_dest_index[i] == UINT32_MAX)
			continue;

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

	positions = std::move(new_positions);
	attributes = std::move(new_attributes);
}

static std::vector<uint32_t> remap_indices(const std::vector<uint32_t> &indices, const std::vector<uint32_t> &remap_table)
{
	std::vector<uint32_t> remapped;
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

	std::vector<uint8_t> positions(mesh.count * mesh.position_stride);
	std::vector<uint8_t> attributes(mesh.count * mesh.attribute_stride);

	if (mesh.index_type == VK_INDEX_TYPE_UINT32)
	{
		const auto *ibo = reinterpret_cast<const uint32_t *>(mesh.indices.data());
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
		const auto *ibo = reinterpret_cast<const uint16_t *>(mesh.indices.data());
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
	else if (mesh.index_type == VK_INDEX_TYPE_UINT8)
	{
		const auto *ibo = mesh.indices.data();
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

	mesh.positions = std::move(positions);
	mesh.attributes = std::move(attributes);
	mesh.indices.clear();
	return true;
}

bool mesh_canonicalize_indices(SceneFormats::Mesh &mesh)
{
	if (mesh.topology != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST &&
	    mesh.topology != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP)
	{
		LOGE("Topology must be trilist or tristrip.\n");
		return false;
	}

	std::vector<uint32_t> unrolled_indices;
	unrolled_indices.reserve(mesh.count);

	if (mesh.indices.empty())
	{
		for (unsigned i = 0; i < mesh.count; i++)
			unrolled_indices.push_back(i);
		mesh.index_type = VK_INDEX_TYPE_UINT32;
	}
	else if (mesh.index_type == VK_INDEX_TYPE_UINT32)
	{
		auto *indices = reinterpret_cast<const uint32_t *>(mesh.indices.data());
		for (unsigned i = 0; i < mesh.count; i++)
			unrolled_indices.push_back(indices[i]);
	}
	else if (mesh.index_type == VK_INDEX_TYPE_UINT16)
	{
		auto *indices = reinterpret_cast<const uint16_t *>(mesh.indices.data());
		for (unsigned i = 0; i < mesh.count; i++)
			unrolled_indices.push_back(mesh.primitive_restart && indices[i] == UINT16_MAX ? UINT32_MAX : indices[i]);
	}
	else if (mesh.index_type == VK_INDEX_TYPE_UINT8)
	{
		auto *indices = reinterpret_cast<const uint8_t *>(mesh.indices.data());
		for (unsigned i = 0; i < mesh.count; i++)
			unrolled_indices.push_back(mesh.primitive_restart && indices[i] == UINT8_MAX ? UINT32_MAX : indices[i]);
	}

	if (mesh.topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP)
	{
		std::vector<uint32_t> unstripped_indices;
		unstripped_indices.reserve(mesh.count * 3);
		unsigned primitive_count_since_restart = 0;

		for (unsigned i = 2; i < mesh.count; i++)
		{
			bool emit_primitive = true;
			if (mesh.primitive_restart &&
			    (unrolled_indices[i - 2] == UINT32_MAX ||
			     unrolled_indices[i - 1] == UINT32_MAX ||
			     unrolled_indices[i - 0] == UINT32_MAX))
			{
				emit_primitive = false;
				primitive_count_since_restart = 0;
			}

			if (emit_primitive)
			{
				unstripped_indices.push_back(unrolled_indices[i - 2]);
				unstripped_indices.push_back(unrolled_indices[i - (1 ^ (primitive_count_since_restart & 1))]);
				unstripped_indices.push_back(unrolled_indices[i - (primitive_count_since_restart & 1)]);
				primitive_count_since_restart++;
			}
		}

		unrolled_indices = std::move(unstripped_indices);
		mesh.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	}

	mesh.index_type = VK_INDEX_TYPE_UINT32;
	mesh.count = uint32_t(unrolled_indices.size());
	mesh.indices.resize(unrolled_indices.size() * sizeof(uint32_t));
	memcpy(mesh.indices.data(), unrolled_indices.data(), mesh.indices.size());
	return true;
}

void mesh_deduplicate_vertices(Mesh &mesh)
{
	mesh_canonicalize_indices(mesh);
	auto index_remap = build_attribute_remap_indices(mesh);
	auto index_buffer = build_remapped_index_buffer(mesh, index_remap.index_remap);
	rebuild_new_attributes_remap_src(mesh.positions, mesh.position_stride,
	                                 mesh.attributes, mesh.attribute_stride,
	                                 mesh.positions, mesh.attributes, index_remap.unique_attrib_to_source_index);

	mesh.indices.resize(index_buffer.size() * sizeof(uint32_t));
	memcpy(mesh.indices.data(), index_buffer.data(), index_buffer.size() * sizeof(uint32_t));
	mesh.count = unsigned(index_buffer.size());
}

bool mesh_optimize_index_buffer(Mesh &mesh, const IndexBufferOptimizeOptions &options)
{
	if (!mesh_canonicalize_indices(mesh) || mesh.topology != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
		return false;

	// Remove redundant indices and rewrite index and attribute buffers.
	auto index_remap = build_attribute_remap_indices(mesh);
	auto index_buffer = build_remapped_index_buffer(mesh, index_remap.index_remap);
	rebuild_new_attributes_remap_src(mesh.positions, mesh.position_stride,
	                                 mesh.attributes, mesh.attribute_stride,
	                                 mesh.positions, mesh.attributes, index_remap.unique_attrib_to_source_index);

	size_t vertex_count = mesh.positions.size() / mesh.position_stride;

	// Optimize for vertex cache.
	meshopt_optimizeVertexCache(index_buffer.data(), index_buffer.data(), index_buffer.size(),
	                            vertex_count);

	// Remap vertex fetch to get contiguous indices as much as possible.
	std::vector<uint32_t> remap_table(mesh.positions.size() / mesh.position_stride);
	vertex_count = meshopt_optimizeVertexFetchRemap(remap_table.data(), index_buffer.data(), index_buffer.size(), vertex_count);
	index_buffer = remap_indices(index_buffer, remap_table);
	rebuild_new_attributes_remap_dst(mesh.positions, mesh.position_stride,
	                                 mesh.attributes, mesh.attribute_stride,
	                                 mesh.positions, mesh.attributes, remap_table, vertex_count);

	if (options.stripify)
	{
		// Try to stripify the mesh. If we end up with fewer indices, use that.
		std::vector<uint32_t> stripped_index_buffer((index_buffer.size() / 3) * 4);
		size_t stripped_index_count = meshopt_stripify(stripped_index_buffer.data(),
		                                               index_buffer.data(), index_buffer.size(),
		                                               vertex_count, ~0u);

		stripped_index_buffer.resize(stripped_index_count);
		if (stripped_index_count < index_buffer.size())
		{
			mesh.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
			index_buffer = std::move(stripped_index_buffer);
			mesh.primitive_restart = true;
		}
	}

	bool emit_u32 = true;
	if (options.narrow_index_buffer)
	{
		uint32_t max_index = 0;
		for (auto &i: index_buffer)
			if (i != ~0u)
				max_index = muglm::max(max_index, i);

		if (max_index <= 0xffff) // 16-bit indices are enough.
		{
			mesh.index_type = VK_INDEX_TYPE_UINT16;
			mesh.indices.resize(index_buffer.size() * sizeof(uint16_t));
			size_t count = index_buffer.size();
			emit_u32 = false;

			auto *out_indices = reinterpret_cast<uint16_t *>(mesh.indices.data());
			for (size_t i = 0; i < count; i++)
				out_indices[i] = index_buffer[i] == ~0u ? uint16_t(0xffffu) : uint16_t(index_buffer[i]);
		}
	}

	if (emit_u32)
	{
		mesh.indices.resize(index_buffer.size() * sizeof(uint32_t));
		memcpy(mesh.indices.data(), index_buffer.data(), index_buffer.size() * sizeof(uint32_t));
	}

	mesh.count = unsigned(index_buffer.size());
	return true;
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

static void touch_node_children(std::unordered_set<uint32_t> &touched, const std::vector<Node> &nodes, uint32_t index)
{
	touched.insert(index);
	for (auto &child : nodes[index].children)
	{
		touched.insert(child);
		touch_node_children(touched, nodes, child);
	}
}

std::unordered_set<uint32_t> build_used_nodes_in_scene(const SceneNodes &scene, const std::vector<Node> &nodes)
{
	std::unordered_set<uint32_t> touched;
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

static std::vector<float> build_smooth_rail_animation_timestamps(const std::vector<float> &timestamps,
                                                                 float sharpness)
{
	if (sharpness < 0.001f)
		return timestamps;

	std::vector<float> new_linear_timestamps;
	float offset = 0.5f - sharpness * 0.5f;
	new_linear_timestamps.reserve((timestamps.size() - 2) * 3);

	for (size_t i = 0, n = timestamps.size(); i < n; i++)
	{
		if (i == 0 || i + 1 == n)
			new_linear_timestamps.push_back(timestamps[i]);
		else
		{
			new_linear_timestamps.push_back(mix(timestamps[i], timestamps[i - 1], offset));
			new_linear_timestamps.push_back(timestamps[i]);
			new_linear_timestamps.push_back(mix(timestamps[i], timestamps[i + 1], offset));
		}
	}

	return new_linear_timestamps;
}

static void copy_base_parameters(AnimationChannel &out_channel, const AnimationChannel &in_channel)
{
	out_channel.joint = in_channel.joint;
	out_channel.joint_index = in_channel.joint_index;
	out_channel.node_index = in_channel.node_index;

	switch (in_channel.type)
	{
	case SceneFormats::AnimationChannel::Type::Translation:
		out_channel.type = SceneFormats::AnimationChannel::Type::CubicTranslation;
		break;

	case SceneFormats::AnimationChannel::Type::Scale:
		out_channel.type = SceneFormats::AnimationChannel::Type::CubicScale;
		break;

	case SceneFormats::AnimationChannel::Type::Rotation:
		out_channel.type = SceneFormats::AnimationChannel::Type::Squad;
		break;

	default:
		LOGE("Invalid input type.\n");
		break;
	}
}

static SceneFormats::AnimationChannel build_smooth_rail_animation_positional(
		const SceneFormats::AnimationChannel &channel, float sharpness)
{
	// Do nothing for identity transforms.
	if (sharpness > 0.999f || channel.timestamps.size() < 2)
		return channel;

	AnimationChannel new_channel;
	copy_base_parameters(new_channel, channel);

	std::vector<float> new_linear_timestamps;
	std::vector<vec3> new_linear_values;
	std::vector<vec3> new_spline_values;

	new_linear_timestamps = build_smooth_rail_animation_timestamps(channel.timestamps, sharpness);
	new_linear_values.reserve(new_linear_timestamps.size());

	for (auto t : new_linear_timestamps)
	{
		unsigned index;
		float phase;
		float dt;
		channel.get_index_phase(t, index, phase, dt);
		new_linear_values.push_back(channel.positional.sample(index, phase));
	}

	new_spline_values.reserve(new_linear_values.size() * 3);

	// Compute desired tangents at the control points.
	for (size_t i = 0, n = new_linear_timestamps.size(); i < n; i++)
	{
		vec3 tangent;
		if (i == 0)
		{
			float t0 = new_linear_timestamps[i];
			float t1 = new_linear_timestamps[i + 1];
			vec3 v0 = new_linear_values[i];
			vec3 v1 = new_linear_values[i + 1];
			tangent = (v1 - v0) / (t1 - t0);
		}
		else if (i + 1 == n)
		{
			float t0 = new_linear_timestamps[i - 1];
			float t1 = new_linear_timestamps[i];
			vec3 v0 = new_linear_values[i - 1];
			vec3 v1 = new_linear_values[i];
			tangent = (v1 - v0) / (t1 - t0);
		}
		else
		{
			float t0 = new_linear_timestamps[i - 1];
			float t1 = new_linear_timestamps[i + 1];
			vec3 v0 = new_linear_values[i - 1];
			vec3 v1 = new_linear_values[i + 1];
			tangent = (v1 - v0) / (t1 - t0);
		}

		new_spline_values.push_back(tangent);
		new_spline_values.push_back(new_linear_values[i]);
		new_spline_values.push_back(tangent);
	}

	new_channel.timestamps = std::move(new_linear_timestamps);
	new_channel.positional.values = std::move(new_spline_values);
	return new_channel;
}

static AnimationChannel build_smooth_rail_animation_spherical(const AnimationChannel &channel, float sharpness)
{
	// Do nothing for identity transforms.
	if (sharpness > 0.999f || channel.timestamps.size() < 2)
		return channel;

	AnimationChannel new_channel;
	copy_base_parameters(new_channel, channel);

	std::vector<float> new_linear_timestamps;
	std::vector<quat> new_linear_values;
	std::vector<vec4> new_spline_values;
	std::vector<vec3> tmp_spline_deltas;

	new_linear_timestamps = build_smooth_rail_animation_timestamps(channel.timestamps, sharpness);
	new_linear_values.reserve(new_linear_timestamps.size());

	for (auto t : new_linear_timestamps)
	{
		unsigned index;
		float phase;
		float dt;
		channel.get_index_phase(t, index, phase, dt);
		new_linear_values.push_back(channel.spherical.sample(index, phase));
	}

	new_spline_values.reserve(new_linear_values.size() * 3);
	tmp_spline_deltas.reserve(new_linear_timestamps.size());

	for (size_t i = 1, n = new_linear_timestamps.size(); i < n; i++)
	{
		// Ensure that neighboring quaternions have minimum difference, otherwise, we might end up with
		// broken animations when we try to lerp.
		auto &q0 = new_linear_values[i - 1];
		auto &q1 = new_linear_values[i];
		if (dot(q0.as_vec4(), q1.as_vec4()) < 0)
			q1 = quat(-q1.as_vec4());
	}

	// Compute desired tangents at the control points.
	for (size_t i = 0, n = new_linear_timestamps.size(); i < n; i++)
	{
		vec3 delta;
		if (i == 0)
		{
			quat q0 = new_linear_values[i];
			quat q1 = new_linear_values[i + 1];
			float dt = new_linear_timestamps[i + 1] - new_linear_timestamps[i];
			delta = compute_inner_control_point_delta(q0, q0, q1, dt, dt);
		}
		else if (i + 1 == n)
		{
			quat q0 = new_linear_values[i - 1];
			quat q1 = new_linear_values[i];
			float dt = new_linear_timestamps[i] - new_linear_timestamps[i - 1];
			delta = compute_inner_control_point_delta(q0, q1, q1, dt, dt);
		}
		else
		{
			quat q0 = new_linear_values[i - 1];
			quat q1 = new_linear_values[i];
			quat q2 = new_linear_values[i + 1];
			float dt0 = new_linear_timestamps[i] - new_linear_timestamps[i - 1];
			float dt1 = new_linear_timestamps[i + 1] - new_linear_timestamps[i];
			delta = compute_inner_control_point_delta(q0, q1, q2, dt0, dt1);
		}

		tmp_spline_deltas.push_back(delta);
	}

	for (size_t i = 0, n = new_linear_timestamps.size(); i < n; i++)
	{
		if (i > 0)
		{
			// Adjust the inner control points such that velocities remain continuous,
			// even with non-uniform spacing of timestamps.
			// Adjust the incoming inner control point based on the outgoing control point.
			vec3 outgoing = tmp_spline_deltas[i];

			float dt0 = new_linear_timestamps[i] - new_linear_timestamps[i - 1];
			float dt1 = i + 1 < n ? (new_linear_timestamps[i + 1] - new_linear_timestamps[i]) : dt0;
			float t_ratio = dt0 / dt1;

			const quat &q0 = new_linear_values[i - 1];
			const quat &q1 = new_linear_values[i];
			const quat &q2 = i + 1 < n ? new_linear_values[i + 1] : q1;

			quat q12 = conjugate(q1) * q2;
			quat q10 = conjugate(q1) * q0;
			vec3 delta_q12 = quat_log(q12);
			vec3 delta_q10 = quat_log(q10);

			vec3 incoming = 0.5f * (t_ratio * delta_q12 + delta_q10) - t_ratio * outgoing;

			new_spline_values.push_back(compute_inner_control_point(new_linear_values[i], incoming).as_vec4());
			new_spline_values.push_back(new_linear_values[i].as_vec4());
			new_spline_values.push_back(compute_inner_control_point(new_linear_values[i], outgoing).as_vec4());
		}
		else
		{
			quat completed = compute_inner_control_point(new_linear_values[i], tmp_spline_deltas[i]);
			new_spline_values.push_back(completed.as_vec4());
			new_spline_values.push_back(new_linear_values[i].as_vec4());
			new_spline_values.push_back(completed.as_vec4());
		}
	}

	new_channel.timestamps = std::move(new_linear_timestamps);
	new_channel.spherical.values = std::move(new_spline_values);
	return new_channel;
}

AnimationChannel AnimationChannel::build_smooth_rail_animation(float sharpness) const
{
	switch (type)
	{
	case SceneFormats::AnimationChannel::Type::Scale:
	case SceneFormats::AnimationChannel::Type::Translation:
		return build_smooth_rail_animation_positional(*this, sharpness);

	case SceneFormats::AnimationChannel::Type::Rotation:
		return build_smooth_rail_animation_spherical(*this, sharpness);

	default:
		LOGE("Invalid input channel type.\n");
		return {};
	}
}
}
}
