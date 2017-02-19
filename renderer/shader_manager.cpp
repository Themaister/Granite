#include "shader_manager.hpp"

namespace Granite
{
Vulkan::ProgramHandle ShaderProgram::get_program(Vulkan::Device &device)
{
	auto *vert = stages[static_cast<unsigned>(Vulkan::ShaderStage::Vertex)];
	auto *frag = stages[static_cast<unsigned>(Vulkan::ShaderStage::Fragment)];
	auto *comp = stages[static_cast<unsigned>(Vulkan::ShaderStage::Compute)];
	if (comp)
	{
		auto &comp_instance = shader_instance[static_cast<unsigned>(Vulkan::ShaderStage::Compute)];
		if (comp_instance != comp->get_instance())
		{
			comp_instance = comp->get_instance();
			program = device.create_program(comp->get_spirv().data(), comp->get_spirv().size() * sizeof(uint32_t));
			instance++;
		}
	}
	else if (vert && frag)
	{
		auto &vert_instance = shader_instance[static_cast<unsigned>(Vulkan::ShaderStage::Vertex)];
		auto &frag_instance = shader_instance[static_cast<unsigned>(Vulkan::ShaderStage::Fragment)];
		if (vert_instance != vert->get_instance() || frag_instance != frag->get_instance())
		{
			vert_instance = vert->get_instance();
			frag_instance = frag->get_instance();
			program = device.create_program(vert->get_spirv().data(), vert->get_spirv().size() * sizeof(uint32_t),
			                                frag->get_spirv().data(), frag->get_spirv().size() * sizeof(uint32_t));
			instance++;
		}
	}
	return program;
}
}