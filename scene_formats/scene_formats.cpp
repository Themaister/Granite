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

#include "scene_formats.hpp"

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