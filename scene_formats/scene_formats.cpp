/* Copyright (c) 2017-2018 Hans-Kristian Arntzen
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

static void rebuild_new_attributes(vector<uint8_t> &positions, unsigned position_stride,
                                   vector<uint8_t> &attributes, unsigned attribute_stride,
                                   const vector<uint8_t> &source_positions, const vector<uint8_t> &source_attributes,
                                   const vector<uint32_t> &unique_attrib_to_source_index)
{
	positions.resize(position_stride * unique_attrib_to_source_index.size());
	if (attribute_stride)
		attributes.resize(attribute_stride * unique_attrib_to_source_index.size());

	size_t count = unique_attrib_to_source_index.size();
	for (size_t i = 0; i < count; i++)
	{
		memcpy(positions.data() + i * position_stride,
		       source_positions.data() + unique_attrib_to_source_index[i] * position_stride,
		       position_stride);

		if (attribute_stride)
		{
			memcpy(attributes.data() + i * attribute_stride,
			       source_attributes.data() + unique_attrib_to_source_index[i] * attribute_stride,
			       attribute_stride);
		}
	}
}


Mesh mesh_optimize_index_buffer(const Mesh &mesh)
{
	if (mesh.topology != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
		return mesh;

	Mesh optimized;
	optimized.position_stride = mesh.position_stride;
	optimized.attribute_stride = mesh.attribute_stride;

	auto index_remap = build_index_remap_list(mesh);
	auto index_buffer = build_canonical_index_buffer(mesh, index_remap.index_remap);
	rebuild_new_attributes(optimized.positions, optimized.position_stride,
	                       optimized.attributes, optimized.attribute_stride,
	                       mesh.positions, mesh.attributes, index_remap.unique_attrib_to_source_index);

	uint32_t max_index = 0;
	for (auto &i : index_buffer)
		if (i != ~0u)
			max_index = muglm::max(max_index, i);

	if (max_index < 0xffff) // 16-bit indices are enough.
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
	optimized.topology = mesh.topology;

	memcpy(optimized.attribute_layout, mesh.attribute_layout, sizeof(mesh.attribute_layout));
	optimized.material_index = mesh.material_index;
	optimized.has_material = mesh.has_material;
	optimized.static_aabb = mesh.static_aabb;

	return optimized;
}

bool recompute_normals(Mesh &mesh)
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

	if (mesh.topology != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
	{
		LOGE("Unsupported primitive topology for normal computation.\n");
		return false;
	}

	unsigned attr_count = mesh.attributes.size() / mesh.attribute_stride;
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
}
}