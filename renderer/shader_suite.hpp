#pragma once

#include "shader_manager.hpp"
#include "mesh.hpp"

namespace Granite
{
class ShaderSuite
{
public:
	void init_graphics(Vulkan::ShaderManager *manager, const std::string &vertex, const std::string &fragment);
	void init_compute(Vulkan::ShaderManager *manager, const std::string &compute);
	Vulkan::ProgramHandle get_program(DrawPipeline pipeline, uint32_t attribute_mask, uint32_t texture_mask);

	std::vector<std::pair<std::string, int>> &get_base_defines()
	{
		return base_defines;
	}

private:
	Vulkan::ShaderManager *manager = nullptr;
	Vulkan::ShaderProgram *program = nullptr;
	Util::HashMap<unsigned> variants;
	std::vector<std::pair<std::string, int>> base_defines;
};
}