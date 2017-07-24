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

#include <cstring>
#include <algorithm>
#include "path.hpp"
#include "shader_manager.hpp"
#include "device.hpp"

using namespace std;
using namespace Granite;
using namespace Util;

namespace Vulkan
{
ShaderTemplate::ShaderTemplate(const std::string &shader_path)
	: path(shader_path)
{
	compiler = unique_ptr<GLSLCompiler>(new GLSLCompiler);
	compiler->set_source_from_file(shader_path);
	if (!compiler->preprocess())
		throw runtime_error(Util::join("Failed to pre-process shader: ", shader_path));
}

const ShaderTemplate::Variant *ShaderTemplate::register_variant(const std::vector<std::pair<std::string, int>> *defines)
{
	Hasher h;
	if (defines)
	{
		for (auto &define : *defines)
		{
			h.u64(hash<string>()(define.first));
			h.u32(uint32_t(define.second));
		}
	}

	auto hash = h.get();
	auto itr = variants.find(hash);
	if (itr == end(variants))
	{
		auto variant = unique_ptr<Variant>(new Variant);

		variant->spirv = compiler->compile(defines);
		if (variant->spirv.empty())
		{
			LOGE("Shader error:\n%s\n", compiler->get_error_message().c_str());
			throw runtime_error("Shader compile failed.");
		}

		variant->instance++;
		if (defines)
			variant->defines = *defines;

		auto *ret = variant.get();
		variants[hash] = move(variant);
		return ret;
	}
	else
		return itr->second.get();
}

void ShaderTemplate::recompile()
{
	// Recompile all variants.
	try
	{
		unique_ptr<GLSLCompiler> newcompiler(new GLSLCompiler);
		newcompiler->set_source_from_file(path);
		if (!newcompiler->preprocess())
		{
			LOGE("Failed to preprocess updated shader: %s\n", path.c_str());
			return;
		}
		compiler = move(newcompiler);

		for (auto &variant : variants)
		{
			auto newspirv = compiler->compile(&variant.second->defines);
			if (newspirv.empty())
			{
				LOGE("Failed to compile shader: %s\n%s\n", path.c_str(), compiler->get_error_message().c_str());
				for (auto &define : variant.second->defines)
					LOGE("  Define: %s = %d\n", define.first.c_str(), define.second);
				continue;
			}

			variant.second->spirv = move(newspirv);
			variant.second->instance++;
		}
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

void ShaderProgram::set_stage(Vulkan::ShaderStage stage, ShaderTemplate *shader)
{
	stages[static_cast<unsigned>(stage)] = shader;
	VK_ASSERT(variants.empty());
}

Vulkan::ProgramHandle ShaderProgram::get_program(unsigned variant)
{
	auto &var = variants[variant];
	auto *vert = var.stages[static_cast<unsigned>(Vulkan::ShaderStage::Vertex)];
	auto *frag = var.stages[static_cast<unsigned>(Vulkan::ShaderStage::Fragment)];
	auto *comp = var.stages[static_cast<unsigned>(Vulkan::ShaderStage::Compute)];
	if (comp)
	{
		auto &comp_instance = var.shader_instance[static_cast<unsigned>(Vulkan::ShaderStage::Compute)];
		if (comp_instance != comp->instance)
		{
			comp_instance = comp->instance;
			var.program = device->create_program(comp->spirv.data(), comp->spirv.size() * sizeof(uint32_t));
		}
	}
	else if (vert && frag)
	{
		auto &vert_instance = var.shader_instance[static_cast<unsigned>(Vulkan::ShaderStage::Vertex)];
		auto &frag_instance = var.shader_instance[static_cast<unsigned>(Vulkan::ShaderStage::Fragment)];
		if (vert_instance != vert->instance || frag_instance != frag->instance)
		{
			vert_instance = vert->instance;
			frag_instance = frag->instance;
			var.program = device->create_program(vert->spirv.data(), vert->spirv.size() * sizeof(uint32_t),
			                                     frag->spirv.data(), frag->spirv.size() * sizeof(uint32_t));
		}
	}
	return var.program;
}

unsigned ShaderProgram::register_variant(const std::vector<std::pair<std::string, int>> &defines)
{
	Hasher h;
	for (auto &define : defines)
	{
		h.u64(hash<string>()(define.first));
		h.u32(uint32_t(define.second));
	}

	auto hash = h.get();
	auto itr = find(begin(variant_hashes), end(variant_hashes), hash);
	if (itr != end(variant_hashes))
		return unsigned(itr - begin(variant_hashes));

	unsigned index = unsigned(variants.size());
	variants.emplace_back();
	auto &var = variants.back();
	variant_hashes.push_back(hash);

	for (unsigned i = 0; i < static_cast<unsigned>(Vulkan::ShaderStage::Count); i++)
		if (stages[i])
			var.stages[i] = stages[i]->register_variant(&defines);

	// Make sure it's compiled correctly.
	get_program(index);
	return index;
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
		auto prog = unique_ptr<ShaderProgram>(new ShaderProgram(device));
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
		auto prog = unique_ptr<ShaderProgram>(new ShaderProgram(device));
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