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
}

Vulkan::ProgramHandle ShaderSuite::get_program(MeshDrawPipeline pipeline, uint32_t attribute_mask,
                                               uint32_t texture_mask)
{
	Hasher h;
	h.u32(pipeline == MeshDrawPipeline::AlphaTest);
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
		case MeshDrawPipeline::AlphaTest:
			defines.emplace_back("ALPHA_TEST", 1);
			break;

		default:
			break;
		}

		defines.emplace_back("HAVE_POSITION", !!(attribute_mask & (1u << static_cast<uint32_t>(MeshAttribute::Position))));
		defines.emplace_back("HAVE_UV", !!(attribute_mask & (1u << static_cast<uint32_t>(MeshAttribute::UV))));
		defines.emplace_back("HAVE_NORMAL", !!(attribute_mask & (1u << static_cast<uint32_t>(MeshAttribute::Normal))));
		defines.emplace_back("HAVE_TANGENT", !!(attribute_mask & (1u << static_cast<uint32_t>(MeshAttribute::Tangent))));
		defines.emplace_back("HAVE_BONE_INDEX", !!(attribute_mask & (1u << static_cast<uint32_t>(MeshAttribute::BoneIndex))));
		defines.emplace_back("HAVE_BONE_WEIGHT", !!(attribute_mask & (1u << static_cast<uint32_t>(MeshAttribute::BoneWeights))));

		defines.emplace_back("HAVE_ALBEDOMAP", !!(texture_mask & (1u << static_cast<uint32_t>(Material::Textures::Albedo))));
		defines.emplace_back("HAVE_NORMALMAP", !!(texture_mask & (1u << static_cast<uint32_t>(Material::Textures::Normal))));
		defines.emplace_back("HAVE_ROUGHNESSMAP", !!(texture_mask & (1u << static_cast<uint32_t>(Material::Textures::Roughness))));
		defines.emplace_back("HAVE_METALLICMAP", !!(texture_mask & (1u << static_cast<uint32_t>(Material::Textures::Metallic))));
		variant = program->register_variant(defines);
		variants[hash] = variant;
	}
	else
		variant = itr->second;

	return program->get_program(variant);
}

}