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

#include "obj.hpp"
#include "filesystem.hpp"
#include "memory_mapped_texture.hpp"
#include "texture_files.hpp"
#include "string_helpers.hpp"

using namespace std;
using namespace Util;

namespace OBJ
{
void Parser::flush_mesh()
{
	if (current_positions.empty())
		return;

	Mesh mesh = {};

	if (current_material >= 0)
	{
		mesh.has_material = true;
		mesh.material_index = unsigned(current_material);
	}

	mesh.positions.resize(current_positions.size() * sizeof(vec3));
	memcpy(mesh.positions.data(), current_positions.data(), mesh.positions.size());
	mesh.position_stride = sizeof(vec3);
	mesh.attribute_layout[ecast(MeshAttribute::Position)].format = VK_FORMAT_R32G32B32_SFLOAT;
	mesh.count = unsigned(current_positions.size());
	mesh.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	vec3 lo = vec3(numeric_limits<float>::max());
	vec3 hi = vec3(-numeric_limits<float>::max());
	for (auto &p : current_positions)
	{
		lo = min(lo, p);
		hi = max(hi, p);
	}
	mesh.static_aabb = AABB(lo, hi);

	if (!current_normals.empty() && !current_uvs.empty())
	{
		if (current_normals.size() != current_positions.size())
			throw runtime_error("Normal size != position size.");
		if (current_uvs.size() != current_positions.size())
			throw runtime_error("UV size != position size.");

		mesh.attribute_layout[ecast(MeshAttribute::Normal)].format = VK_FORMAT_R32G32B32_SFLOAT;
		mesh.attribute_layout[ecast(MeshAttribute::UV)].format = VK_FORMAT_R32G32_SFLOAT;
		mesh.attribute_layout[ecast(MeshAttribute::UV)].offset = sizeof(vec3);
		static const size_t stride = sizeof(vec3) + sizeof(vec2);
		mesh.attribute_stride = stride;
		mesh.attributes.resize(stride * current_positions.size());

		for (size_t i = 0; i < current_positions.size(); i++)
		{
			memcpy(mesh.attributes.data() + stride * i, &current_normals[i], sizeof(vec3));
			memcpy(mesh.attributes.data() + stride * i + sizeof(vec3), &current_uvs[i], sizeof(vec2));
		}
	}
	else if (!current_normals.empty())
	{
		if (current_normals.size() != current_positions.size())
			throw runtime_error("Normal size != position size.");
		mesh.attribute_layout[ecast(MeshAttribute::Normal)].format = VK_FORMAT_R32G32B32_SFLOAT;
		static const size_t stride = sizeof(vec3);
		mesh.attribute_stride = stride;
		mesh.attributes.resize(stride * current_positions.size());
		for (size_t i = 0; i < current_positions.size(); i++)
			memcpy(mesh.attributes.data() + stride * i, &current_normals[i], sizeof(vec3));
	}
	else if (!current_uvs.empty())
	{
		if (current_uvs.size() != current_positions.size())
			throw runtime_error("UV size != position size.");
		mesh.attribute_layout[ecast(MeshAttribute::UV)].format = VK_FORMAT_R32G32_SFLOAT;
		static const size_t stride = sizeof(vec2);
		mesh.attribute_stride = stride;
		mesh.attributes.resize(stride * current_positions.size());
		for (size_t i = 0; i < current_positions.size(); i++)
			memcpy(mesh.attributes.data() + stride * i, &current_uvs[i], sizeof(vec2));
	}

	current_positions.clear();
	current_normals.clear();
	current_uvs.clear();

	mesh_deduplicate_vertices(mesh);

	root_node.meshes.push_back(meshes.size());
	meshes.push_back(move(mesh));
}

void Parser::emit_gltf_base_color(const std::string &base_color_path, const std::string &alpha_mask_path)
{
	MemoryMappedTexture alpha_mask;
	MemoryMappedTexture output;

	if (!alpha_mask_path.empty())
	{
		alpha_mask = load_texture_from_file(alpha_mask_path, ColorSpace::Linear);
		if (alpha_mask.empty())
		{
			LOGE("Failed to open alpha mask texture %s, falling back to default material.\n",
			     alpha_mask_path.c_str());
			return;
		}
	}
	else
	{
		materials.back().base_color.path = base_color_path;
		return;
	}

	auto base_color = load_texture_from_file(base_color_path, ColorSpace::sRGB);
	if (base_color.empty())
	{
		LOGE("Failed to open base color texture %s, falling back to default material.\n",
		     base_color_path.c_str());
		return;
	}

	unsigned width = 0;
	unsigned height = 0;
	Hasher hasher;

	if (base_color.get_layout().get_width() != alpha_mask.get_layout().get_width())
	{
		LOGE("Widths of textures do not match ... Falling back to default material.\n");
		return;
	}

	if (base_color.get_layout().get_height() != alpha_mask.get_layout().get_height())
	{
		LOGE("Widths of textures do not match ... Falling back to default material.\n");
		return;
	}

	if (base_color.get_layout().get_format() != VK_FORMAT_R8G8B8A8_SRGB)
		LOGE("Unexpected format.");
	if (alpha_mask.get_layout().get_format() != VK_FORMAT_R8G8B8A8_UNORM)
		LOGE("Unexpected format.");

	width = base_color.get_layout().get_width();
	height = base_color.get_layout().get_height();
	hasher.string(base_color_path);
	hasher.string(alpha_mask_path);
	string packed_path = string("memory://") + to_string(hasher.get()) + ".gtx";
	materials.back().base_color.path = packed_path;
	materials.back().pipeline = DrawPipeline::AlphaTest;
	materials.back().two_sided = true;

	output.set_2d(VK_FORMAT_R8G8B8A8_SRGB, width, height);
	if (!output.map_write(packed_path))
	{
		LOGE("Failed to map texture for writing.\n");
		return;
	}

	for (unsigned y = 0; y < height; y++)
	{
		for (unsigned x = 0; x < width; x++)
		{
			auto *dst = output.get_layout().data_2d<u8vec4>(x, y);
			auto *b = base_color.get_layout().data_2d<u8vec4>(x, y);
			auto *a = alpha_mask.get_layout().data_2d<u8vec4>(x, y);
			*dst = u8vec4(b->x, b->y, b->z, a->x);
		}
	}
}

void Parser::emit_gltf_pbr_metallic_roughness(const std::string &metallic_path, const std::string &roughness_path)
{
	MemoryMappedTexture metallic;
	MemoryMappedTexture roughness;
	MemoryMappedTexture pbr;

	if (!metallic_path.empty())
	{
		metallic = load_texture_from_file(metallic_path, ColorSpace::Linear);
		if (metallic.empty())
		{
			LOGE("Failed to open metallic texture %s, falling back to default material.\n",
			     metallic_path.c_str());
			return;
		}
	}

	if (!roughness_path.empty())
	{
		roughness = load_texture_from_file(roughness_path, ColorSpace::Linear);
		if (roughness.empty())
		{
			LOGE("Failed to open roughness texture %s, falling back to default material.\n",
			     roughness_path.c_str());
			return;
		}
	}

	unsigned width = 0;
	unsigned height = 0;
	Hasher hasher;

	if (!metallic.empty() && !roughness.empty())
	{
		if (metallic.get_layout().get_width() != roughness.get_layout().get_width())
		{
			LOGE("Widths of textures do not match ... Falling back to default material.\n");
			return;
		}

		if (metallic.get_layout().get_height() != roughness.get_layout().get_height())
		{
			LOGE("Widths of textures do not match ... Falling back to default material.\n");
			return;
		}

		if (metallic.get_layout().get_format() != VK_FORMAT_R8G8B8A8_UNORM)
			LOGE("Unexpected format.");
		if (roughness.get_layout().get_format() != VK_FORMAT_R8G8B8A8_UNORM)
			LOGE("Unexpected format.");

		width = metallic.get_layout().get_width();
		height = metallic.get_layout().get_height();
		hasher.string(metallic_path);
		hasher.string(roughness_path);
	}
	else if (!metallic.empty())
	{
		if (metallic.get_layout().get_format() != VK_FORMAT_R8G8B8A8_UNORM)
			LOGE("Unexpected format.");
		width = metallic.get_layout().get_width();
		height = metallic.get_layout().get_height();
		hasher.string(metallic_path);
	}
	else if (!roughness.empty())
	{
		if (roughness.get_layout().get_format() != VK_FORMAT_R8G8B8A8_UNORM)
			LOGE("Unexpected format.");
		width = roughness.get_layout().get_width();
		height = roughness.get_layout().get_height();
		hasher.string(roughness_path);
	}

	hasher.string(metallic_path);
	hasher.string(roughness_path);
	string packed_path = string("memory://") + to_string(hasher.get()) + ".gtx";
	materials.back().metallic_roughness.path = packed_path;

	pbr.set_2d(VK_FORMAT_R8G8B8A8_UNORM, width, height);
	if (!pbr.map_write(packed_path))
	{
		LOGE("Failed to map texture for writing.\n");
		return;
	}

	if (!metallic.empty() && !roughness.empty())
	{
		for (unsigned y = 0; y < height; y++)
		{
			for (unsigned x = 0; x < width; x++)
			{
				auto *output = pbr.get_layout().data_2d<u8vec4>(x, y);
				auto *m = metallic.get_layout().data_2d<u8vec4>(x, y);
				auto *r = roughness.get_layout().data_2d<u8vec4>(x, y);
				*output = u8vec4(0u, r->x, m->x, 0u);
			}
		}
	}
	else if (!metallic.empty())
	{
		for (unsigned y = 0; y < height; y++)
		{
			for (unsigned x = 0; x < width; x++)
			{
				auto *output = pbr.get_layout().data_2d<u8vec4>(x, y);
				auto *m = metallic.get_layout().data_2d<u8vec4>(x, y);
				*output = u8vec4(0u, 255u, m->x, 0u); // Probably need to estimate roughness based on specular.
			}
		}
	}
	else if (!roughness.empty())
	{
		for (unsigned y = 0; y < height; y++)
		{
			for (unsigned x = 0; x < width; x++)
			{
				auto *output = pbr.get_layout().data_2d<u8vec4>(x, y);
				auto *r = roughness.get_layout().data_2d<u8vec4>(x, y);
				*output = u8vec4(0u, r->x, 0u, 0u);
			}
		}
	}
}

void Parser::emit_vertex(const OBJVertex * const *face)
{
	// Positions
	for (unsigned i = 0; i < 3; i++)
	{
		auto &p = face[i]->operator[](0);

		if (!p.empty())
		{
			int index = stoi(p);
			if (index < 0)
				index = int(positions.size()) + index;
			else
				index--;

			if (index < 0 || index >= int(positions.size()))
				throw logic_error("Index out of bounds.");
			current_positions.push_back(positions[index]);
		}

		if (face[i]->size() == 1)
			continue;

		auto &u = face[i]->operator[](1);
		auto &n = face[i]->operator[](2);

		if (!u.empty())
		{
			int index = stoi(u);
			if (index < 0)
				index = int(uvs.size()) + index;
			else
				index--;

			if (index < 0 || index >= int(uvs.size()))
				throw logic_error("Index out of bounds.");
			current_uvs.push_back(uvs[index]);
		}

		if (!n.empty())
		{
			int index = stoi(n);
			if (index < 0)
				index = int(normals.size()) + index;
			else
				index--;

			if (index < 0 || index >= int(normals.size()))
				throw logic_error("Index out of bounds.");
			current_normals.push_back(normals[index]);
		}
	}
}

void Parser::load_material_library(const std::string &path)
{
	string mtl;
	if (!Granite::Global::filesystem()->read_file_to_string(path, mtl))
		throw runtime_error("Failed to load material library.");

	vector<string> lines = split_no_empty(mtl, "\n");
	string metallic;
	string roughness;
	string base_color;
	string alpha_mask;

	for (auto &line : lines)
	{
		line = strip_whitespace(line);
		auto comment_index = line.find_first_of('#');
		if (comment_index != string::npos)
			line = line.substr(comment_index, string::npos);

		auto elements = split_no_empty(line, " ");
		if (elements.empty())
			continue;

		auto &ident = elements.front();
		if (ident == "newmtl")
		{
			if (!metallic.empty() || !roughness.empty())
				emit_gltf_pbr_metallic_roughness(metallic, roughness);
			if (!base_color.empty())
				emit_gltf_base_color(base_color, alpha_mask);

			material_library[elements.at(1)] = unsigned(materials.size());
			materials.push_back({});
			metallic.clear();
			roughness.clear();
			base_color.clear();
			alpha_mask.clear();
		}
		else if (ident == "Kd")
		{
			if (materials.empty())
				throw logic_error("No material");
			for (unsigned i = 0; i < 3; i++)
				materials.back().uniform_base_color[i] = stof(elements.at(i + 1));
		}
		else if (ident == "map_Kd")
		{
			if (materials.empty())
				throw logic_error("No material");
			base_color = Path::relpath(path, elements.at(1));
		}
		else if (ident == "map_d")
		{
			if (materials.empty())
				throw logic_error("No material");
			alpha_mask = Path::relpath(path, elements.at(1));
		}
		else if (ident == "bump")
		{
			if (materials.empty())
				throw logic_error("No material");
			materials.back().normal.path = Path::relpath(path, elements.at(1));
		}
		else if (ident == "map_Ka")
		{
			// Custom magic stuff for Sponza PBR.
			if (materials.empty())
				throw logic_error("No material");
			metallic = Path::relpath(path, elements.at(1));
		}
		else if (ident == "map_Ns")
		{
			// Custom magic stuff for Sponza PBR.
			if (materials.empty())
				throw logic_error("No material");
			roughness = Path::relpath(path, elements.at(1));
		}
	}

	if (!metallic.empty() || !roughness.empty())
		emit_gltf_pbr_metallic_roughness(metallic, roughness);
	if (!base_color.empty())
		emit_gltf_base_color(base_color, alpha_mask);
}

Parser::Parser(const std::string &path)
{
	string obj;
	if (!Global::filesystem()->read_file_to_string(path, obj))
		throw runtime_error("Failed to load OBJ.");

	vector<string> lines = split_no_empty(obj, "\n");
	for (auto &line : lines)
	{
		line = strip_whitespace(line);
		auto comment_index = line.find_first_of('#');
		if (comment_index != string::npos)
			line = line.substr(comment_index, string::npos);

		auto elements = split_no_empty(line, " ");
		if (elements.empty())
			continue;

		auto &ident = elements.front();
		if (ident == "mtllib")
			load_material_library(Path::relpath(path, elements.at(1)));
		else if (ident == "v")
			positions.push_back(vec3(stof(elements.at(1)), stof(elements.at(2)), stof(elements.at(3))));
		else if (ident == "vn")
			normals.push_back(vec3(stof(elements.at(1)), stof(elements.at(2)), stof(elements.at(3))));
		else if (ident == "vt")
			uvs.push_back(vec2(stof(elements.at(1)), 1.0f - stof(elements.at(2))));
		else if (ident == "usemtl")
		{
			auto itr = material_library.find(elements.at(1));
			if (itr == end(material_library))
			{
				LOGE("Material %s does not exist!\n",
				     elements.at(1).c_str());
				throw runtime_error("Material does not exist.");
			}
			int index = int(itr->second);
			if (index != current_material)
				flush_mesh();
			current_material = index;
		}
		else if (ident == "f")
		{
			if (elements.size() == 5)
			{
				auto v0 = split(elements.at(1), "/");
				auto v1 = split(elements.at(2), "/");
				auto v2 = split(elements.at(3), "/");
				auto v3 = split(elements.at(4), "/");

				const OBJVertex *f0[3] = { &v0, &v1, &v2 };
				const OBJVertex *f1[3] = { &v0, &v2, &v3 };
				emit_vertex(f0);
				emit_vertex(f1);
			}
			else if (elements.size() == 4)
			{
				auto v0 = split(elements.at(1), "/");
				auto v1 = split(elements.at(2), "/");
				auto v2 = split(elements.at(3), "/");
				const OBJVertex *f0[3] = { &v0, &v1, &v2 };
				emit_vertex(f0);
			}
		}
	}

	flush_mesh();
	nodes.push_back(move(root_node));
}
}

