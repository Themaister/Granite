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

namespace OBJ
{
using namespace Granite;
using namespace Granite::SceneFormats;

class Parser
{
public:
	explicit Parser(const std::string &path);

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

private:
	std::vector<MaterialInfo> materials;
	std::vector<Node> nodes;
	std::vector<Mesh> meshes;
	std::unordered_map<std::string, unsigned> material_library;

	std::vector<vec3> positions;
	std::vector<vec3> normals;
	std::vector<vec2> uvs;
	std::vector<vec3> current_positions;
	std::vector<vec3> current_normals;
	std::vector<vec2> current_uvs;
	int current_material = -1;

	void load_material_library(const std::string &path);
	void flush_mesh();

	using OBJVertex = std::vector<std::string>;
	void emit_vertex(const OBJVertex * const *face);
	void emit_gltf_pbr_metallic_roughness(const std::string &metallic, const std::string &roughness);
	void emit_gltf_base_color(const std::string &metallic, const std::string &roughness);
	Node root_node;
};
}