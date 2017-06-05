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

Vulkan::ProgramHandle ShaderSuite::get_program(DrawPipeline pipeline, uint32_t attribute_mask,
                                               uint32_t texture_mask)
{
	Hasher h;
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

		defines.emplace_back("HAVE_POSITION", !!(attribute_mask & MESH_ATTRIBUTE_POSITION_BIT));
		defines.emplace_back("HAVE_UV", !!(attribute_mask & MESH_ATTRIBUTE_UV_BIT));
		defines.emplace_back("HAVE_NORMAL", !!(attribute_mask & MESH_ATTRIBUTE_NORMAL_BIT));
		defines.emplace_back("HAVE_TANGENT", !!(attribute_mask & MESH_ATTRIBUTE_TANGENT_BIT));
		defines.emplace_back("HAVE_BONE_INDEX", !!(attribute_mask & MESH_ATTRIBUTE_BONE_INDEX_BIT));
		defines.emplace_back("HAVE_BONE_WEIGHT", !!(attribute_mask & MESH_ATTRIBUTE_BONE_WEIGHTS_BIT));
		defines.emplace_back("HAVE_VERTEX_COLOR", !!(attribute_mask & MESH_ATTRIBUTE_VERTEX_COLOR_BIT));

		if (attribute_mask & (1u << static_cast<uint32_t>(MeshAttribute::UV)))
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