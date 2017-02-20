#include <cstring>
#include "path.hpp"
#include "shader_manager.hpp"

using namespace std;

namespace Granite
{
ShaderTemplate::ShaderTemplate(const std::string &shader_path)
	: path(shader_path)
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

}

void ShaderTemplate::recompile()
{
	try
	{
		unique_ptr<GLSLCompiler> newcompiler(new GLSLCompiler);
		newcompiler->set_source_from_file(path);
		if (!newcompiler->preprocess())
		{
			LOGE("Failed to preprocess updated shader: %s\n", path.c_str());
			return;
		}
		auto newspirv = newcompiler->compile();
		if (newspirv.empty())
		{
			LOGE("Failed to compile shader: %s\n%s\n", path.c_str(), newcompiler->get_error_message().c_str());
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

void ShaderTemplate::register_dependencies(ShaderManager &manager)
{
	for (auto &dep : compiler->get_dependencies())
		manager.register_dependency(this, dep);
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
	ShaderTemplate *tmpl = get_template(compute);
	register_dependency(tmpl, compute);
	tmpl->register_dependencies(*this);

	Util::Hasher h;
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

ShaderTemplate *ShaderManager::get_template(const std::string &path)
{
	auto itr = shaders.find(path);
	if (itr == end(shaders))
	{
		auto &shader = shaders[path];
		shader = unique_ptr<ShaderTemplate>(new ShaderTemplate(path));
		return shader.get();
	}
	else
		return itr->second.get();
}

ShaderProgram *ShaderManager::register_graphics(const std::string &vertex, const std::string &fragment)
{
	ShaderTemplate *vert_tmpl = get_template(vertex);
	ShaderTemplate *frag_tmpl = get_template(fragment);
	register_dependency(vert_tmpl, vertex);
	register_dependency(frag_tmpl, fragment);
	vert_tmpl->register_dependencies(*this);
	frag_tmpl->register_dependencies(*this);

	Util::Hasher h;
	h.pointer(vert_tmpl);
	h.pointer(frag_tmpl);
	auto hash = h.get();

	auto pitr = programs.find(hash);
	if (pitr == end(programs))
	{
		auto prog = unique_ptr<ShaderProgram>(new ShaderProgram);
		prog->set_stage(Vulkan::ShaderStage::Vertex, vert_tmpl);
		prog->set_stage(Vulkan::ShaderStage::Fragment, frag_tmpl);
		auto *ret = prog.get();
		programs[hash] = move(prog);
		return ret;
	}
	else
		return pitr->second.get();
}

ShaderManager::~ShaderManager()
{
	for (auto &dir : directory_watches)
		if (dir.second.backend)
			dir.second.backend->uninstall_notification(dir.second.handle);
}

void ShaderManager::register_dependency(ShaderTemplate *shader, const std::string &dependency)
{
	dependees[dependency].insert(shader);
	add_directory_watch(dependency);
}

void ShaderManager::recompile(const FileNotifyInfo &info)
{
	if (info.type == FileNotifyType::FileDeleted)
		return;

	for (auto &dep : dependees[info.path])
	{
		dep->recompile();
		dep->register_dependencies(*this);
	}
}

void ShaderManager::add_directory_watch(const std::string &source)
{
	auto basedir = Path::basedir(source);
	if (directory_watches.find(basedir) != end(directory_watches))
		return;

	auto paths = Path::protocol_split(basedir);
	auto *backend = Filesystem::get().get_backend(paths.first);
	if (!backend)
		return;

	FileNotifyHandle handle = -1;
	if (backend)
	{
		handle = backend->install_notification(paths.second, [this](const FileNotifyInfo &info) {
			recompile(info);
		});
	}

	directory_watches[basedir] = { backend, handle };
}
}