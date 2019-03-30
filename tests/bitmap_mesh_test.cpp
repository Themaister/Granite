/* Copyright (c) 2017-2019 Hans-Kristian Arntzen
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

#include "math.hpp"
#include "bitmap_to_mesh.hpp"
#include "mesh_util.hpp"
#include "gltf_export.hpp"
#include <string.h>

using namespace Granite;

int main()
{
	Global::init();
	VoxelizedBitmap bitmap;
#define O 0xff
#define x 0x00
	static const uint8_t comps[] = {
			x, x, x, O, O, x, x, x,
			x, x, O, O, O, O, x, x,
			x, O, O, O, x, O, O, x,
			O, O, O, x, x, O, O, O,
			O, O, O, x, x, O, O, O,
			x, O, O, O, O, O, O, x,
			x, x, O, O, O, O, x, x,
			x, x, x, O, O, x, x, x,
	};
#undef O
#undef x
	voxelize_bitmap(bitmap, comps, 0, 1, 8, 8, 8);

	SceneFormats::Mesh m;
	m.indices.resize(bitmap.indices.size() * sizeof(uint32_t));
	memcpy(m.indices.data(), bitmap.indices.data(), m.indices.size());
	m.positions.resize(bitmap.positions.size() * sizeof(vec3));
	memcpy(m.positions.data(), bitmap.positions.data(), m.positions.size());
	m.attributes.resize(bitmap.normals.size() * sizeof(vec3));
	memcpy(m.attributes.data(), bitmap.normals.data(), m.attributes.size());
	m.position_stride = sizeof(vec3);
	m.attribute_stride = sizeof(vec3);
	m.attribute_layout[Util::ecast(MeshAttribute::Position)].format = VK_FORMAT_R32G32B32_SFLOAT;
	m.attribute_layout[Util::ecast(MeshAttribute::Normal)].format = VK_FORMAT_R32G32B32_SFLOAT;
	m.index_type = VK_INDEX_TYPE_UINT32;
	m.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	m.count = bitmap.indices.size();
	m.has_material = true;
	m.material_index = 0;
	m.static_aabb = AABB(vec3(0.0f, -0.5f, 0.0f), vec3(8.0f, 0.5f, 8.0f));

	SceneFormats::MaterialInfo mat;
	mat.uniform_base_color = vec4(1.0f, 0.8f, 0.6f, 1.0f);
	mat.uniform_metallic = 0.0f;
	mat.uniform_roughness = 1.0f;
	mat.pipeline = DrawPipeline::Opaque;

	SceneFormats::SceneInformation scene;
	SceneFormats::ExportOptions options;

	SceneFormats::Node n;
	n.meshes.push_back(0);

	scene.materials = { &mat, 1 };
	scene.meshes = { &m, 1 };
	scene.nodes = { &n, 1 };
	SceneFormats::export_scene_to_glb(scene, "/tmp/test.glb", options);
	Global::deinit();
}
