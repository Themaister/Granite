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

#include "shader_suite.hpp"

using namespace std;
using namespace Util;
using namespace Vulkan;

namespace Granite
{

void ShaderSuite::init_graphics(ShaderManager *manager, const std::string &vertex, const std::string &fragment)
{
	this->manager = manager;
	program = manager->register_graphics(vertex, fragment);
	variants.clear();
	base_defines.clear();
}

void ShaderSuite::init_compute(Vulkan::ShaderManager *manager, const std::string &compute)
{
	this->manager = manager;
	program = manager->register_compute(compute);
	variants.clear();
	base_defines.clear();
}

void ShaderSuite::bake_base_defines()
{
	Hasher h;
	for (auto &define : base_defines)
	{
		h.string(define.first.c_str());
		h.s32(define.second);
	}
	base_define_hash = h.get();
}

Vulkan::ProgramHandle ShaderSuite::get_program(DrawPipeline pipeline, uint32_t attribute_mask,
                                               uint32_t texture_mask)
{
	Hasher h;
	assert(base_define_hash != 0);
	h.u64(base_define_hash);
	h.u32(pipeline == DrawPipeline::AlphaTest);
	h.u32(attribute_mask);
	h.u32(texture_mask);

	auto hash = h.get();
	auto itr = variants.find(hash);

	unsigned variant;
	if (itr == end(variants))
	{
		vector<pair<string, int>> defines = base_defines;
		switch (pipeline)
		{
		case DrawPipeline::AlphaTest:
			defines.emplace_back("ALPHA_TEST", 1);
			break;

		default:
			break;
		}

		defines.emplace_back("HAVE_EMISSIVE", !!(texture_mask & MATERIAL_EMISSIVE_BIT));
		defines.emplace_back("HAVE_EMISSIVE_REFRACTION", !!(texture_mask & MATERIAL_EMISSIVE_REFRACTION_BIT));
		defines.emplace_back("HAVE_EMISSIVE_REFLECTION", !!(texture_mask & MATERIAL_EMISSIVE_REFLECTION_BIT));
		defines.emplace_back("HAVE_POSITION", !!(attribute_mask & MESH_ATTRIBUTE_POSITION_BIT));
		defines.emplace_back("HAVE_UV", !!(attribute_mask & MESH_ATTRIBUTE_UV_BIT));
		defines.emplace_back("HAVE_NORMAL", !!(attribute_mask & MESH_ATTRIBUTE_NORMAL_BIT));
		defines.emplace_back("HAVE_TANGENT", !!(attribute_mask & MESH_ATTRIBUTE_TANGENT_BIT));
		defines.emplace_back("HAVE_BONE_INDEX", !!(attribute_mask & MESH_ATTRIBUTE_BONE_INDEX_BIT));
		defines.emplace_back("HAVE_BONE_WEIGHT", !!(attribute_mask & MESH_ATTRIBUTE_BONE_WEIGHTS_BIT));
		defines.emplace_back("HAVE_VERTEX_COLOR", !!(attribute_mask & MESH_ATTRIBUTE_VERTEX_COLOR_BIT));

		if (attribute_mask & MESH_ATTRIBUTE_UV_BIT)
		{
			defines.emplace_back("HAVE_BASECOLORMAP", !!(texture_mask & MATERIAL_TEXTURE_BASE_COLOR_BIT));
			if ((attribute_mask & MESH_ATTRIBUTE_NORMAL_BIT) && (attribute_mask & MESH_ATTRIBUTE_TANGENT_BIT))
				defines.emplace_back("HAVE_NORMALMAP", !!(texture_mask & MATERIAL_TEXTURE_NORMAL_BIT));
			defines.emplace_back("HAVE_METALLICROUGHNESSMAP", !!(texture_mask & MATERIAL_TEXTURE_METALLIC_ROUGHNESS_BIT));
		}
		variant = program->register_variant(defines);
		variants[hash] = variant;
	}
	else
		variant = itr->second;

	return program->get_program(variant);
}

}