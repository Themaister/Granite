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

#include "math.hpp"
#include "bitmap_to_mesh.hpp"
#include "mesh_util.hpp"
#include "texture_files.hpp"
#include "cli_parser.hpp"
#include "gltf_export.hpp"
#include <string.h>

using namespace Granite;
using namespace Util;

static void print_help()
{
	LOGI("Usage: bitmap-to-mesh\n"
	     "\t[--input <path>]\n"
	     "\t[--output <path>]\n"
	     "\t[--quantize-attributes]\n"
	     "\t[--compute-center-of-mass]\n"
	     "\t[--fixed-center-of-mass x y z]\n"
	     "\t[--scale x y z]\n"
	     "\t[--no-depth]\n"
	     "\t[--flip-winding]\n"
	     "\t[--node-translate x y z]\n"
	     "\t[--node-rotate axisX axisY axisZ degrees]\n"
	     "\t[--node-scale x y z]\n"
	     "\t[--rect x y width hegiht]\n");
}

static vec3 center_of_mass_constant_density(const Vulkan::TextureFormatLayout &layout,
                                            unsigned rect_x, unsigned rect_y, unsigned rect_width, unsigned rect_height)
{
	float total_weight = 0.0f;
	vec3 mass_sum = vec3(0.0f);
	for (unsigned y = 0; y < rect_height; y++)
	{
		for (unsigned x = 0; x < rect_width; x++)
		{
			auto v = layout.data_2d<u8vec4>(x + rect_x, y + rect_y, 0, 0)->w;
			if (v >= 128)
			{
				mass_sum += vec3(float(x), 0.0f, float(y));
				total_weight += 1.0f;
			}
		}
	}

	if (total_weight == 0.0f)
		return vec3(float(rect_width) * 0.5f, 0.0f, float(rect_height) * 0.5f);
	else
		return mass_sum / total_weight;
}

int main(int argc, char **argv)
{
	Global::init();
	if (argc < 1)
		return 1;

	VoxelizeBitmapOptions options;
	std::string input;
	std::string output;
	unsigned rect_x = 0;
	unsigned rect_y = 0;
	unsigned rect_width = 0;
	unsigned rect_height = 0;
	vec3 scale = vec3(1.0f);
	bool flip_winding = false;
	bool compute_center_of_mass = false;
	bool quantize_attributes = false;
	vec3 center_of_mass = vec3(0.0f);
	SceneFormats::NodeTransform static_transform;

	CLICallbacks cbs;
	cbs.add("--input", [&](CLIParser &parser) { input = parser.next_string(); });
	cbs.add("--output", [&](CLIParser &parser) { output = parser.next_string(); });
	cbs.add("--flip-winding", [&](CLIParser &) { flip_winding = true; });
	cbs.add("--no-depth", [&](CLIParser &) { options.depth = false; });
	cbs.add("--compute-center-of-mass", [&](CLIParser &) { compute_center_of_mass = true; });
	cbs.add("--quantize-attributes", [&](CLIParser &) { quantize_attributes = true; });
	cbs.add("--fixed-center-of-mass", [&](CLIParser &parser) {
		for (unsigned i = 0; i < 3; i++)
			center_of_mass[i] = float(parser.next_double());
	});
	cbs.add("--scale", [&](CLIParser &parser) {
		for (unsigned i = 0; i < 3; i++)
			scale[i] = float(parser.next_double());
	});
	cbs.add("--node-scale", [&](CLIParser &parser) {
		for (unsigned i = 0; i < 3; i++)
			static_transform.scale[i] = float(parser.next_double());
	});
	cbs.add("--node-translate", [&](CLIParser &parser) {
		for (unsigned i = 0; i < 3; i++)
			static_transform.translation[i] = float(parser.next_double());
	});
	cbs.add("--node-rotate", [&](CLIParser &parser) {
		vec4 rotation;
		for (unsigned i = 0; i < 4; i++)
			rotation[i] = float(parser.next_double());
		static_transform.rotation =
				normalize(angleAxis(radians(rotation.w), rotation.xyz()) * static_transform.rotation);
	});
	cbs.add("--rect", [&](CLIParser &parser) {
		rect_x = parser.next_uint();
		rect_y = parser.next_uint();
		rect_width = parser.next_uint();
		rect_height = parser.next_uint();
	});
	cbs.add("--help", [&](CLIParser &parser) { print_help(); parser.end(); });

	CLIParser parser(std::move(cbs), argc - 1, argv + 1);
	if (!parser.parse())
	{
		print_help();
		return 1;
	}
	else if (parser.is_ended_state())
		return 0;

	if (input.empty())
	{
		LOGE("Input must be used.\n");
		return 1;
	}

	if (output.empty())
	{
		LOGE("Output must be empty.\n");
		return 1;
	}

	auto image = load_texture_from_file(input);
	if (image.empty())
		return 1;

	unsigned width = image.get_layout().get_width();
	unsigned height = image.get_layout().get_height();
	float inv_width = 1.0f / width;
	float inv_height = 1.0f / height;

	if (rect_width == 0)
		rect_width = width;
	if (rect_height == 0)
		rect_height = height;

	if (rect_x >= width)
	{
		LOGE("X is out of range (%u > %u).\n", rect_x, width);
		return 1;
	}

	if (rect_y >= height)
	{
		LOGE("Y is out of range (%u > %u).\n", rect_y, height);
		return 1;
	}

	if (rect_x + rect_width > width)
	{
		LOGE("Rect is out of range.\n");
		return 1;
	}

	if (rect_y + rect_height > height)
	{
		LOGE("Rect is out of range.\n");
		return 1;
	}

	VoxelizedBitmap bitmap;
	if (!voxelize_bitmap(bitmap,
	                     image.get_layout().data_2d<u8vec4>(rect_x, rect_y, 0, 0)->data, 3, 4,
	                     rect_width, rect_height, width * 4, options))
	{
		return 1;
	}

	if (flip_winding)
	{
		for (size_t i = 0; i < bitmap.indices.size(); i += 3)
			std::swap(bitmap.indices[i + 1], bitmap.indices[i + 2]);
		for (auto &n : bitmap.normals)
			n = -n;
	}

	struct Attr
	{
		vec3 normal;
		vec2 uv;
	};
	std::vector<Attr> attrs;
	attrs.reserve(bitmap.normals.size());

	for (size_t i = 0; i < bitmap.normals.size(); i++)
	{
		attrs.push_back({bitmap.normals[i],
		                 vec2((rect_x + bitmap.positions[i].x) * inv_width,
		                      (rect_y + bitmap.positions[i].z) * inv_height)
		                });
	}

	if (compute_center_of_mass)
	{
		center_of_mass = center_of_mass_constant_density(image.get_layout(), rect_x, rect_y,
		                                                 rect_width, rect_height);
	}

	for (auto &pos : bitmap.positions)
		pos = scale * (pos - center_of_mass);

	SceneFormats::Mesh m;
	m.indices.resize(bitmap.indices.size() * sizeof(uint32_t));
	memcpy(m.indices.data(), bitmap.indices.data(), m.indices.size());
	m.positions.resize(bitmap.positions.size() * sizeof(vec3));
	memcpy(m.positions.data(), bitmap.positions.data(), m.positions.size());
	m.attributes.resize(attrs.size() * sizeof(Attr));
	memcpy(m.attributes.data(), attrs.data(), m.attributes.size());
	m.position_stride = sizeof(vec3);
	m.attribute_stride = sizeof(Attr);
	m.attribute_layout[Util::ecast(MeshAttribute::Position)].format = VK_FORMAT_R32G32B32_SFLOAT;
	m.attribute_layout[Util::ecast(MeshAttribute::Normal)].format = VK_FORMAT_R32G32B32_SFLOAT;
	m.attribute_layout[Util::ecast(MeshAttribute::Normal)].offset = offsetof(Attr, normal);
	m.attribute_layout[Util::ecast(MeshAttribute::UV)].format = VK_FORMAT_R32G32_SFLOAT;
	m.attribute_layout[Util::ecast(MeshAttribute::UV)].offset = offsetof(Attr, uv);

	m.index_type = VK_INDEX_TYPE_UINT32;
	m.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	m.count = bitmap.indices.size();
	m.has_material = true;
	m.material_index = 0;
	m.static_aabb = AABB(scale * (vec3(0.0f, -0.5f, 0.0f) - center_of_mass),
	                     scale * (vec3(rect_width, 0.5f, rect_height) - center_of_mass));

	SceneFormats::MaterialInfo mat;
	mat.bandlimited_pixel = true;
	mat.base_color.path = input;
	mat.uniform_metallic = 0.0f;
	mat.uniform_roughness = 1.0f;
	mat.sampler = Vulkan::StockSampler::TrilinearClamp;
	mat.pipeline = DrawPipeline::Opaque;

	SceneFormats::SceneInformation scene;
	SceneFormats::ExportOptions export_options;
	SceneFormats::Node n;
	n.transform = static_transform;
	n.meshes.push_back(0);

	scene.materials = { &mat, 1 };
	scene.meshes = { &m, 1 };
	scene.nodes = { &n, 1 };
	export_options.quantize_attributes = quantize_attributes;
	export_options.optimize_meshes = true;
	if (!SceneFormats::export_scene_to_glb(scene, output, export_options))
	{
		LOGE("Failed to export scene to %s.\n", output.c_str());
		return 1;
	}
	Global::deinit();
}
