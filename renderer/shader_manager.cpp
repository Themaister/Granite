#include <cstring>
#include "path.hpp"
#include "shader_manager.hpp"

using namespace std;

namespace Granite
{
ShaderTemplate::ShaderTemplate(const std::string &shader_path)
{
	compiler = unique_ptr<GLSLCompiler>(new GLSLCompiler);
	compiler->set_source_from_file(shader_path);
	if (!compiler->preprocess())
		throw runtime_error(Util::join("Failed to pre-process shader: ", shader_path));

	spirv = compiler->compile();
	if (spirv.empty())
	{
		LOGE("Shader error:\n%s\n", compiler->get_error_message().c_str());
		throw runtime_error("Shader compile failed.");
	}

	instance++;

	notify_backend = Filesystem::get().get_backend(Path::protocol_split(shader_path).first);
	if (notify_backend)
	{
		notify_handle = notify_backend->install_notification(shader_path, [this](const FileNotifyInfo &info) {
			recompile(info);
		});
	}
}

void ShaderTemplate::recompile(const FileNotifyInfo &info)
{
	try
	{
		unique_ptr<GLSLCompiler> newcompiler(new GLSLCompiler);
		newcompiler->set_source_from_file(info.path);
		if (!newcompiler->preprocess())
		{
			LOGE("Failed to preprocess updated shader: %s\n", info.path.c_str());
			return;
		}
		auto newspirv = newcompiler->compile();
		if (newspirv.empty())
		{
			LOGE("Failed to compile shader: %s\n%s\n", info.path.c_str(), newcompiler->get_error_message().c_str());
			return;
		}

		spirv = move(newspirv);
		compiler = move(newcompiler);
		instance++;
	}
	catch (const std::exception &e)
	{
		LOGE("Exception caught: %s\n", e.what());
	}
}

ShaderTemplate::~ShaderTemplate()
{
	if (notify_backend)
		notify_backend->uninstall_notification(notify_handle);
}

void ShaderProgram::set_stage(Vulkan::ShaderStage stage, const ShaderTemplate *shader)
{
	stages[static_cast<unsigned>(stage)] = shader;
	program.reset();
	memset(shader_instance, 0, sizeof(shader_instance));
}

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

ShaderManager &ShaderManager::get()
{
	static ShaderManager manager;
	return manager;
}

ShaderProgram *ShaderManager::register_compute(const std::string &compute)
{
	ShaderTemplate *tmpl = nullptr;

	Util::Hasher h;
	auto itr = shaders.find(compute);
	if (itr == end(shaders))
	{
		auto &shader = shaders[compute];
		shader = unique_ptr<ShaderTemplate>(new ShaderTemplate(compute));
		tmpl = shader.get();
	}
	else
		tmpl = itr->second.get();

	h.pointer(tmpl);
	auto hash = h.get();

	auto pitr = programs.find(hash);
	if (pitr == end(programs))
	{
		auto prog = unique_ptr<ShaderProgram>(new ShaderProgram);
		prog->set_stage(Vulkan::ShaderStage::Compute, tmpl);
		auto *ret = prog.get();
		programs[hash] = move(prog);
		return ret;
	}
	else
		return pitr->second.get();
}
}