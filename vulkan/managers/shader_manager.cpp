/* Copyright (c) 2017-2024 Hans-Kristian Arntzen
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

#define NOMINMAX
#ifdef GRANITE_VULKAN_SHADER_MANAGER_RUNTIME_COMPILER
#include "compiler.hpp"
#endif
#include "shader_manager.hpp"
#include "path_utils.hpp"
#include "device.hpp"
#include "rapidjson_wrapper.hpp"
#include "timeline_trace_file.hpp"
#include <algorithm>
#include <cstring>

using namespace Util;

#define DEPENDENCY_LOCK() std::lock_guard<std::mutex> holder{dependency_lock}

namespace Vulkan
{
ShaderTemplate::ShaderTemplate(Device *device_,
                               const std::string &shader_path,
                               ShaderStage force_stage_,
                               MetaCache &cache_,
                               Util::Hash path_hash_,
                               const std::vector<std::string> &include_directories_)
	: device(device_), path(shader_path), force_stage(force_stage_), cache(cache_), path_hash(path_hash_)
#ifdef GRANITE_VULKAN_SHADER_MANAGER_RUNTIME_COMPILER
	, include_directories(include_directories_)
#endif
{
	(void)include_directories_;
}

ShaderTemplate::~ShaderTemplate()
{
}

bool ShaderTemplate::init()
{
	if (Granite::Path::ext(path) == "spv")
	{
		if (!device->get_system_handles().filesystem)
			return false;

		auto precompiled_file = device->get_system_handles().filesystem->open_readonly_mapping(path);
		const uint32_t *ptr;

		if (!precompiled_file || !(ptr = precompiled_file->data<uint32_t>()))
		{
			LOGE("Failed to load shader: %s.\n", path.c_str());
			return false;
		}

		static_shader = { ptr, ptr + precompiled_file->get_size() / sizeof(uint32_t) };
#ifdef GRANITE_VULKAN_SHADER_MANAGER_RUNTIME_COMPILER
		source_hash = 0;
#endif
		return true;
	}

#ifdef GRANITE_VULKAN_SHADER_MANAGER_RUNTIME_COMPILER
	if (!device->get_system_handles().filesystem)
		return false;

	GRANITE_SCOPED_TIMELINE_EVENT_FILE(device->get_system_handles().timeline_trace_file, "glsl-preprocess");

	compiler = std::make_unique<Granite::GLSLCompiler>(*device->get_system_handles().filesystem);
	compiler->set_target(device->get_device_features().device_api_core_version >= VK_API_VERSION_1_3 ?
	                     Granite::Target::Vulkan13 : Granite::Target::Vulkan11);
	if (!compiler->set_source_from_file(path, Granite::Stage(force_stage)))
		return false;
	compiler->set_include_directories(&include_directories);
	if (!compiler->preprocess())
	{
		LOGE("Failed to pre-process shader: %s\n", path.c_str());
		compiler.reset();
		return false;
	}
	source_hash = compiler->get_source_hash();
#endif

	return true;
}

const ShaderTemplateVariant *ShaderTemplate::register_variant(
		const std::vector<std::pair<std::string, int>> *defines, Shader *precompiled_shader)
{
	Hasher h;
	if (defines)
	{
		// If we have a static shader, we cannot use defines since we won't be compiling anything.
		VK_ASSERT(static_shader.empty() || defines->empty());

		for (auto &define : *defines)
		{
			h.string(define.first);
			h.s32(define.second);
		}
	}

	auto hash = h.get();
	h.u64(path_hash);
	auto complete_hash = h.get();

	auto *ret = variants.find(hash);
	if (!ret)
	{
		auto *variant = variants.allocate();
		variant->hash = complete_hash;

		PrecomputedMeta *precompiled_spirv = nullptr;
		if (!precompiled_shader)
		{
			precompiled_spirv = cache.variant_to_shader.find(complete_hash);

			if (precompiled_spirv)
			{
				if (!device->request_shader_by_hash(precompiled_spirv->shader_hash))
				{
					LOGW("Got precompiled SPIR-V hash for variant, but it does not exist, is Fossilize archive incomplete?\n");
					precompiled_spirv = nullptr;
				}
#ifdef GRANITE_VULKAN_SHADER_MANAGER_RUNTIME_COMPILER
				else if (source_hash != precompiled_spirv->source_hash)
				{
					LOGW("Source hash is invalidated for %s, recompiling.\n", path.c_str());
					precompiled_spirv = nullptr;
				}
#endif
			}
		}

		if (precompiled_shader)
		{
			variant->precompiled_shader = precompiled_shader;
		}
		else if (!precompiled_spirv)
		{
			if (!static_shader.empty())
			{
				variant->spirv = static_shader;
#ifdef GRANITE_VULKAN_SHADER_MANAGER_RUNTIME_COMPILER
				update_variant_cache(*variant);
#endif
			}
#ifdef GRANITE_VULKAN_SHADER_MANAGER_RUNTIME_COMPILER
			else if (compiler)
			{
#ifdef VULKAN_DEBUG
				std::string hash_debug_str;
				if (defines)
				{
					for (auto &def : *defines)
					{
						hash_debug_str += "\n";
						hash_debug_str += "   ";
						hash_debug_str += def.first;
						hash_debug_str += " = ";
						hash_debug_str += std::to_string(def.second);
					}
					hash_debug_str += ".";
				}
				LOGI("Compiling shader: %s%s\n", path.c_str(), hash_debug_str.c_str());
#endif

				std::string error_message;

				{
					GRANITE_SCOPED_TIMELINE_EVENT_FILE(device->get_system_handles().timeline_trace_file,
					                                   "glsl-compile");
					variant->spirv = compiler->compile(error_message, defines);
				}

				if (variant->spirv.empty())
				{
					LOGE("Shader error:\n%s\n", error_message.c_str());
					variants.free(variant);
					return nullptr;
				}
				update_variant_cache(*variant);
			}
			else
				return nullptr;
#else
			LOGE("Could not find shader variant for %s in cache.\n", path.c_str());
			{
				std::string str;
				str += "[";
				for (size_t i = 0, n = defines ? defines->size() : 0; i < n; i++)
				{
					str += "\n\t{ \"define\" : \"" + defines->operator[](i).first + "\", \"value\" : " + std::to_string(defines->operator[](i).second) + "}";
					if (i + 1 < n)
						str += ",";
				}
				str += "\n]";
				LOGE("Slangmosh variant:\n%s\n", str.c_str());
			}
			variants.free(variant);
			return nullptr;
#endif
		}
		else
			variant->spirv_hash = precompiled_spirv->shader_hash;

		variant->instance++;
		if (defines)
			variant->defines = *defines;

		ret = variants.insert_yield(hash, variant);
	}
	return ret;
}

#ifdef GRANITE_VULKAN_SHADER_MANAGER_RUNTIME_COMPILER
#ifndef GRANITE_SHIPPING
void ShaderTemplate::recompile_variant(ShaderTemplateVariant &variant)
{
	std::string error_message;
	auto newspirv = compiler->compile(error_message, &variant.defines);
	if (newspirv.empty())
	{
		LOGE("Failed to compile shader: %s\n%s\n", path.c_str(), error_message.c_str());
		for (auto &define : variant.defines)
			LOGE("  Define: %s = %d\n", define.first.c_str(), define.second);
		return;
	}

	variant.spirv = std::move(newspirv);
	variant.instance++;
	update_variant_cache(variant);
}
#endif

void ShaderTemplate::update_variant_cache(const ShaderTemplateVariant &variant)
{
	if (variant.spirv.empty())
		return;

	auto shader_hash = Shader::hash(variant.spirv.data(),
	                                variant.spirv.size() * sizeof(uint32_t));

	ResourceLayout layout;
	Shader::reflect_resource_layout(layout, variant.spirv.data(), variant.spirv.size() * sizeof(uint32_t));

#ifndef GRANITE_SHIPPING
	auto *var_to_shader = cache.variant_to_shader.find(variant.hash);
	if (var_to_shader)
	{
		// This is only updated from inotify callbacks, so threading shouldn't really be a concern.
		var_to_shader->source_hash = source_hash;
		var_to_shader->shader_hash = shader_hash;
	}
	else
#endif
	{
		cache.variant_to_shader.emplace_yield(variant.hash, source_hash, shader_hash);
	}

	cache.shader_to_layout.emplace_yield(shader_hash, layout);
}

#ifndef GRANITE_SHIPPING
void ShaderTemplate::recompile()
{
	// Recompile all variants.
	if (!device->get_system_handles().filesystem)
		return;
	auto newcompiler = std::make_unique<Granite::GLSLCompiler>(*device->get_system_handles().filesystem);
	newcompiler->set_target(device->get_device_features().device_api_core_version >= VK_API_VERSION_1_3 ?
	                        Granite::Target::Vulkan13 : Granite::Target::Vulkan11);
	if (!newcompiler->set_source_from_file(path, Granite::Stage(force_stage)))
		return;
	newcompiler->set_include_directories(&include_directories);
	if (!newcompiler->preprocess())
	{
		LOGE("Failed to preprocess updated shader: %s\n", path.c_str());
		return;
	}
	compiler = std::move(newcompiler);
	source_hash = compiler->get_source_hash();

	for (auto &variant : variants.get_read_only())
		recompile_variant(variant);
	for (auto &variant : variants.get_read_write())
		recompile_variant(variant);
}
#endif

void ShaderTemplate::register_dependencies(ShaderManager &manager)
{
	if (compiler)
		for (auto &dep : compiler->get_dependencies())
			manager.register_dependency_nolock(this, dep);
}
#endif

void ShaderProgram::set_stage(Vulkan::ShaderStage stage, ShaderTemplate *shader)
{
	stages[static_cast<unsigned>(stage)] = shader;
	VK_ASSERT(variant_cache.begin() == variant_cache.end());
}

ShaderProgramVariant::ShaderProgramVariant(Device *device_)
	: device(device_)
{
#ifndef GRANITE_SHIPPING
	for (auto &inst : shader_instance)
		inst.store(0, std::memory_order_relaxed);
	program.store(nullptr, std::memory_order_relaxed);
#endif
}

Vulkan::Shader *ShaderTemplateVariant::resolve(Vulkan::Device &device) const
{
	if (precompiled_shader)
		return precompiled_shader;
	else if (spirv.empty())
		return device.request_shader_by_hash(spirv_hash);
	else
		return device.request_shader(spirv.data(), spirv.size() * sizeof(uint32_t));
}

Vulkan::Program *ShaderProgramVariant::get_program_compute()
{
	Vulkan::Program *ret;

	auto *comp = stages[Util::ecast(Vulkan::ShaderStage::Compute)];
#ifdef GRANITE_SHIPPING
	ret = device->request_program(comp->resolve(*device), sampler_bank.get());
#else
	auto &comp_instance = shader_instance[Util::ecast(Vulkan::ShaderStage::Compute)];

	// If we have observed all possible compilation instances,
	// we can safely read program directly.
	// comp->instance will only ever be incremented in the main thread on an inotify, so this is fine.
	// If comp->instance changes in the interim, we are at least guaranteed to read a sensible value for program.
	unsigned loaded_instance = comp_instance.load(std::memory_order_acquire);
	if (loaded_instance == comp->instance)
		return program.load(std::memory_order_relaxed);

	instance_lock.lock_write();
	if (comp_instance.load(std::memory_order_relaxed) != comp->instance)
	{
		ret = device->request_program(comp->resolve(*device), sampler_bank.get());
		program.store(ret, std::memory_order_relaxed);
		comp_instance.store(comp->instance, std::memory_order_release);
	}
	else
	{
		ret = program.load(std::memory_order_relaxed);
	}
	instance_lock.unlock_write();
#endif

	return ret;
}

Vulkan::Program *ShaderProgramVariant::get_program_graphics()
{
	Vulkan::Program *ret;
	auto *vert = stages[Util::ecast(Vulkan::ShaderStage::Vertex)];
	auto *mesh = stages[Util::ecast(Vulkan::ShaderStage::Mesh)];
	auto *task = stages[Util::ecast(Vulkan::ShaderStage::Task)];
	auto *frag = stages[Util::ecast(Vulkan::ShaderStage::Fragment)];

#ifdef GRANITE_SHIPPING
	if (mesh && frag)
	{
		ret = device->request_program(task ? task->resolve(*device) : nullptr,
		                              mesh->resolve(*device),
		                              frag->resolve(*device),
		                              sampler_bank.get());
	}
	else if (vert && frag)
	{
		ret = device->request_program(vert->resolve(*device),
		                              frag->resolve(*device),
		                              sampler_bank.get());
	}
	else
		return nullptr;
#else
	auto &vert_instance = shader_instance[Util::ecast(Vulkan::ShaderStage::Vertex)];
	auto &frag_instance = shader_instance[Util::ecast(Vulkan::ShaderStage::Fragment)];
	auto &mesh_instance = shader_instance[Util::ecast(Vulkan::ShaderStage::Mesh)];
	auto &task_instance = shader_instance[Util::ecast(Vulkan::ShaderStage::Task)];

	unsigned loaded_vert_instance = vert_instance.load(std::memory_order_acquire);
	unsigned loaded_mesh_instance = mesh_instance.load(std::memory_order_acquire);
	unsigned loaded_task_instance = task_instance.load(std::memory_order_acquire);
	unsigned loaded_frag_instance = frag_instance.load(std::memory_order_acquire);

	// If we have observed all possible compilation instances,
	// we can safely read program directly.
	// comp->instance will only ever be incremented in the main thread on an inotify, so this is fine.
	// If comp->instance changes in the interim, we are at least guaranteed to read a sensible value for program.
	if (mesh && frag)
	{
		if ((!task || (loaded_task_instance == task->instance)) &&
		    loaded_mesh_instance == mesh->instance &&
		    loaded_frag_instance == frag->instance)
		{
			return program.load(std::memory_order_relaxed);
		}
	}
	else if (vert && frag)
	{
		if (loaded_vert_instance == vert->instance && loaded_frag_instance == frag->instance)
			return program.load(std::memory_order_relaxed);
	}
	else
		return nullptr;

	instance_lock.lock_write();

	if (mesh)
	{
		if ((task && task_instance.load(std::memory_order_relaxed) != task->instance) ||
		    mesh_instance.load(std::memory_order_relaxed) != mesh->instance ||
		    frag_instance.load(std::memory_order_relaxed) != frag->instance)
		{
			ret = device->request_program(task ? task->resolve(*device) : nullptr,
			                              mesh->resolve(*device),
			                              frag->resolve(*device),
			                              sampler_bank.get());
			program.store(ret, std::memory_order_relaxed);
			if (task)
				task_instance.store(task->instance, std::memory_order_release);
			mesh_instance.store(mesh->instance, std::memory_order_release);
			frag_instance.store(frag->instance, std::memory_order_release);
		}
		else
		{
			ret = program.load(std::memory_order_relaxed);
		}
	}
	else
	{
		if (vert_instance.load(std::memory_order_relaxed) != vert->instance ||
		    frag_instance.load(std::memory_order_relaxed) != frag->instance)
		{
			ret = device->request_program(vert->resolve(*device),
			                              frag->resolve(*device),
			                              sampler_bank.get());
			program.store(ret, std::memory_order_relaxed);
			vert_instance.store(vert->instance, std::memory_order_release);
			frag_instance.store(frag->instance, std::memory_order_release);
		}
		else
		{
			ret = program.load(std::memory_order_relaxed);
		}
	}

	instance_lock.unlock_write();
#endif

	return ret;
}

Vulkan::Program *ShaderProgramVariant::get_program()
{
	auto *frag = stages[static_cast<unsigned>(Vulkan::ShaderStage::Fragment)];
	auto *comp = stages[static_cast<unsigned>(Vulkan::ShaderStage::Compute)];

	if (comp)
		return get_program_compute();
	else if (frag)
		return get_program_graphics();
	else
		return nullptr;
}

ShaderProgramVariant *ShaderProgram::register_variant(const std::vector<std::pair<std::string, int>> &defines,
                                                      const ImmutableSamplerBank *sampler_bank)
{
	return register_variant(nullptr, defines, sampler_bank);
}

ShaderProgramVariant *ShaderProgram::register_precompiled_variant(Shader *comp,
                                                                  const std::vector<std::pair<std::string, int>> &defines,
                                                                  const ImmutableSamplerBank *sampler_bank)
{
	Shader *shaders[int(ShaderStage::Count)] = {};
	shaders[int(ShaderStage::Compute)] = comp;
	return register_variant(shaders, defines, sampler_bank);
}

ShaderProgramVariant *ShaderProgram::register_precompiled_variant(Shader *task, Shader *mesh, Shader *frag,
                                                                  const std::vector<std::pair<std::string, int>> &defines,
                                                                  const ImmutableSamplerBank *sampler_bank)
{
	Shader *shaders[int(ShaderStage::Count)] = {};
	shaders[int(ShaderStage::Task)] = task;
	shaders[int(ShaderStage::Mesh)] = mesh;
	shaders[int(ShaderStage::Fragment)] = frag;
	return register_variant(shaders, defines, sampler_bank);
}

ShaderProgramVariant *ShaderProgram::register_precompiled_variant(Vulkan::Shader *vert, Vulkan::Shader *frag,
                                                                  const std::vector<std::pair<std::string, int>> &defines,
                                                                  const Vulkan::ImmutableSamplerBank *sampler_bank)
{
	Shader *shaders[int(ShaderStage::Count)] = {};
	shaders[int(ShaderStage::Vertex)] = vert;
	shaders[int(ShaderStage::Fragment)] = frag;
	return register_variant(shaders, defines, sampler_bank);
}

ShaderProgramVariant *ShaderProgram::register_variant(Shader * const *precompiled_shaders,
                                                      const std::vector<std::pair<std::string, int>> &defines,
                                                      const ImmutableSamplerBank *sampler_bank)
{
	Hasher h;
	for (auto &define : defines)
	{
		h.string(define.first);
		h.s32(define.second);
	}

	ImmutableSamplerBank::hash(h, sampler_bank);

	auto hash = h.get();

	if (auto *variant = variant_cache.find(hash))
		return variant;

	auto *new_variant = variant_cache.allocate(device);
	if (sampler_bank)
		new_variant->sampler_bank.reset(new ImmutableSamplerBank(*sampler_bank));

	for (int i = 0; i < int(Vulkan::ShaderStage::Count); i++)
	{
		if (stages[i])
		{
			new_variant->stages[i] = stages[i]->register_variant(
					&defines, precompiled_shaders ? precompiled_shaders[i] : nullptr);
		}
	}

	// Make sure it's compiled correctly.
	new_variant->get_program();

	new_variant = variant_cache.insert_yield(hash, new_variant);
	return new_variant;
}

ShaderProgram *ShaderManager::register_compute(const std::string &compute)
{
	auto *tmpl = get_template(compute, ShaderStage::Compute);
	if (!tmpl)
		return nullptr;

	Util::Hasher h;
	h.u64(tmpl->get_path_hash());
	auto hash = h.get();

	auto *ret = programs.find(hash);
	if (!ret)
		ret = programs.emplace_yield(hash, device, tmpl);
	return ret;
}

ShaderTemplate *ShaderManager::get_template(const std::string &path, ShaderStage force_stage)
{
	Hasher hasher;
	hasher.string(path);
	auto hash = hasher.get();

	auto *ret = shaders.find(hash);
	if (!ret)
	{
		auto *shader = shaders.allocate(device, path, force_stage,
		                                meta_cache, hasher.get(), include_directories);
		if (!shader->init())
		{
			shaders.free(shader);
			return nullptr;
		}

#ifdef GRANITE_VULKAN_SHADER_MANAGER_RUNTIME_COMPILER
		{
			DEPENDENCY_LOCK();
			register_dependency_nolock(shader, path);
			shader->register_dependencies(*this);
		}
#endif
		ret = shaders.insert_yield(hash, shader);
	}
	return ret;
}

ShaderProgram *ShaderManager::register_graphics(const std::string &vertex, const std::string &fragment)
{
	auto *vert_tmpl = get_template(vertex, ShaderStage::Vertex);
	auto *frag_tmpl = get_template(fragment, ShaderStage::Fragment);
	if (!vert_tmpl || !frag_tmpl)
		return nullptr;

	Util::Hasher h;
	h.u64(vert_tmpl->get_path_hash());
	h.u64(frag_tmpl->get_path_hash());
	auto hash = h.get();

	auto *ret = programs.find(hash);
	if (!ret)
		ret = programs.emplace_yield(hash, device, vert_tmpl, frag_tmpl);
	return ret;
}

ShaderProgram *ShaderManager::register_graphics(const std::string &task, const std::string &mesh, const std::string &fragment)
{
	ShaderTemplate *task_tmpl = nullptr;
	if (!task.empty())
		task_tmpl = get_template(task, ShaderStage::Task);
	auto *mesh_tmpl = get_template(mesh, ShaderStage::Mesh);
	auto *frag_tmpl = get_template(fragment, ShaderStage::Fragment);
	if (!mesh_tmpl || !frag_tmpl)
		return nullptr;

	Util::Hasher h;
	h.u64(task_tmpl ? task_tmpl->get_path_hash() : 0);
	h.u64(mesh_tmpl->get_path_hash());
	h.u64(frag_tmpl->get_path_hash());
	auto hash = h.get();

	auto *ret = programs.find(hash);
	if (!ret)
		ret = programs.emplace_yield(hash, device, task_tmpl, mesh_tmpl, frag_tmpl);
	return ret;
}

ShaderManager::~ShaderManager()
{
#if defined(GRANITE_VULKAN_SHADER_MANAGER_RUNTIME_COMPILER) && !defined(GRANITE_SHIPPING)
	for (auto &dir : directory_watches)
		if (dir.second.backend)
			dir.second.backend->uninstall_notification(dir.second.handle);
#endif
}

#ifdef GRANITE_VULKAN_SHADER_MANAGER_RUNTIME_COMPILER
void ShaderManager::register_dependency(ShaderTemplate *shader, const std::string &dependency)
{
	DEPENDENCY_LOCK();
	register_dependency_nolock(shader, dependency);
}

void ShaderManager::register_dependency_nolock(ShaderTemplate *shader, const std::string &dependency)
{
	dependees[dependency].insert(shader);
#ifndef GRANITE_SHIPPING
	add_directory_watch(dependency);
#endif
}

#ifndef GRANITE_SHIPPING
void ShaderManager::recompile(const Granite::FileNotifyInfo &info)
{
	DEPENDENCY_LOCK();
	if (info.type == Granite::FileNotifyType::FileDeleted)
		return;

	auto &deps = dependees[info.path];
	for (auto &dep : deps)
	{
		dep->recompile();
		dep->register_dependencies(*this);
	}
}

void ShaderManager::add_directory_watch(const std::string &source)
{
	if (!device->get_system_handles().filesystem)
		return;
	auto basedir = Granite::Path::basedir(source);
	if (directory_watches.find(basedir) != end(directory_watches))
		return;

	auto paths = Granite::Path::protocol_split(basedir);
	auto *backend = device->get_system_handles().filesystem->get_backend(paths.first);
	if (!backend)
		return;

	Granite::FileNotifyHandle handle = -1;
	handle = backend->install_notification(paths.second, [this](const Granite::FileNotifyInfo &info) {
		recompile(info);
	});

	if (handle >= 0)
		directory_watches[basedir] = { backend, handle };
}
#endif
#endif

void ShaderManager::register_shader_from_variant_hash(Hash variant_hash,
                                                      Hash source_hash,
                                                      Hash shader_hash,
                                                      const ResourceLayout &layout)
{
	auto *var_to_shader = meta_cache.variant_to_shader.emplace_yield(variant_hash, source_hash, shader_hash);
	if (var_to_shader->source_hash != source_hash || var_to_shader->shader_hash != shader_hash)
	{
		// Unsure if this function is even used, but I suppose we'll figure out ...
		LOGW("Mismatch in register_shader_from_variant_hash.\n");
	}
	meta_cache.shader_to_layout.emplace_yield(shader_hash, layout);
}

bool ShaderManager::get_shader_hash_by_variant_hash(Hash variant_hash, Hash &shader_hash) const
{
	auto *shader = meta_cache.variant_to_shader.find(variant_hash);
	if (shader)
	{
		shader_hash = shader->shader_hash;
		return true;
	}
	else
		return false;
}

bool ShaderManager::get_resource_layout_by_shader_hash(Util::Hash shader_hash, ResourceLayout &layout) const
{
	auto *shader = meta_cache.shader_to_layout.find(shader_hash);
	if (shader)
	{
		layout = shader->get();
		return true;
	}
	else
		return false;
}

void ShaderManager::add_include_directory(const std::string &path)
{
	if (find(begin(include_directories), end(include_directories), path) == end(include_directories))
		include_directories.push_back(path);
}

void ShaderManager::promote_read_write_caches_to_read_only()
{
	shaders.move_to_read_only();
	programs.move_to_read_only();
	meta_cache.variant_to_shader.move_to_read_only();
	meta_cache.shader_to_layout.move_to_read_only();
}

static ResourceLayout parse_resource_layout(const rapidjson::Value &layout_obj)
{
	ResourceLayout layout;

	layout.bindless_set_mask = layout_obj["bindlessSetMask"].GetUint();
	layout.input_mask = layout_obj["inputMask"].GetUint();
	layout.output_mask = layout_obj["outputMask"].GetUint();
	layout.push_constant_size = layout_obj["pushConstantSize"].GetUint();
	layout.spec_constant_mask = layout_obj["specConstantMask"].GetUint();

	for (unsigned i = 0; i < VULKAN_NUM_DESCRIPTOR_SETS; i++)
	{
		auto &set_obj = layout_obj["sets"][i];
		auto &set = layout.sets[i];
		set.uniform_buffer_mask = set_obj["uniformBufferMask"].GetUint();
		set.storage_buffer_mask = set_obj["storageBufferMask"].GetUint();
		set.sampled_texel_buffer_mask = set_obj["sampledTexelBufferMask"].GetUint();
		set.storage_texel_buffer_mask = set_obj["storageTexelBufferMask"].GetUint();
		set.sampled_image_mask = set_obj["sampledImageMask"].GetUint();
		set.storage_image_mask = set_obj["storageImageMask"].GetUint();
		set.separate_image_mask = set_obj["separateImageMask"].GetUint();
		set.sampler_mask = set_obj["samplerMask"].GetUint();
		set.input_attachment_mask = set_obj["inputAttachmentMask"].GetUint();
		set.fp_mask = set_obj["fpMask"].GetUint();
		auto &array_size = set_obj["arraySize"];
		for (unsigned j = 0; j < VULKAN_NUM_BINDINGS; j++)
			set.array_size[j] = array_size[j].GetUint();
	}

	return layout;
}

template <typename Alloc>
static rapidjson::Value serialize_resource_layout(const ResourceLayout &layout, Alloc &allocator)
{
	using namespace rapidjson;

	Value layout_obj(kObjectType);
	layout_obj.AddMember("bindlessSetMask", layout.bindless_set_mask, allocator);
	layout_obj.AddMember("inputMask", layout.input_mask, allocator);
	layout_obj.AddMember("outputMask", layout.output_mask, allocator);
	layout_obj.AddMember("pushConstantSize", layout.push_constant_size, allocator);
	layout_obj.AddMember("specConstantMask", layout.spec_constant_mask, allocator);
	Value desc_sets(kArrayType);
	for (auto &set : layout.sets)
	{
		Value set_obj(kObjectType);
		set_obj.AddMember("uniformBufferMask", set.uniform_buffer_mask, allocator);
		set_obj.AddMember("storageBufferMask", set.storage_buffer_mask, allocator);
		set_obj.AddMember("sampledTexelBufferMask", set.sampled_texel_buffer_mask, allocator);
		set_obj.AddMember("storageTexelBufferMask", set.storage_texel_buffer_mask, allocator);
		set_obj.AddMember("sampledImageMask", set.sampled_image_mask, allocator);
		set_obj.AddMember("storageImageMask", set.storage_image_mask, allocator);
		set_obj.AddMember("separateImageMask", set.separate_image_mask, allocator);
		set_obj.AddMember("samplerMask", set.sampler_mask, allocator);
		set_obj.AddMember("inputAttachmentMask", set.input_attachment_mask, allocator);
		set_obj.AddMember("fpMask", set.fp_mask, allocator);
		Value array_size(kArrayType);
		for (auto &arr_size : set.array_size)
			array_size.PushBack(uint32_t(arr_size), allocator);
		set_obj.AddMember("arraySize", array_size, allocator);
		desc_sets.PushBack(set_obj, allocator);
	}
	layout_obj.AddMember("sets", desc_sets, allocator);
	return layout_obj;
}

bool ShaderManager::load_shader_cache(const std::string &path)
{
	if (!device->get_system_handles().filesystem)
		return false;

	using namespace rapidjson;
	std::string json;
	if (!device->get_system_handles().filesystem->read_file_to_string(path, json))
		return false;

	Document doc;
	doc.Parse(json);
	if (doc.HasParseError())
	{
		LOGE("Failed to parse shader cache format!\n");
		return false;
	}

	if (!doc.HasMember("shaderCacheVersion"))
	{
		LOGE("Member shaderCacheVersion does not exist.\n");
		return false;
	}

	unsigned version = doc["shaderCacheVersion"].GetUint();
	if (version != ResourceLayout::Version)
	{
		LOGE("Incompatible shader cache version %u != %u.\n", version, ResourceLayout::Version);
		return false;
	}

	auto &maps = doc["maps"];
	for (auto itr = maps.Begin(); itr != maps.End(); ++itr)
	{
		auto &value = *itr;

		Util::Hash variant_hash = value["variant"].GetUint64();
		Util::Hash spirv_hash = value["spirvHash"].GetUint64();
		Util::Hash source_hash = value["sourceHash"].GetUint64();
		ResourceLayout layout = parse_resource_layout(value["layout"]);
		meta_cache.variant_to_shader.emplace_yield(variant_hash, source_hash, spirv_hash);
		meta_cache.shader_to_layout.emplace_yield(spirv_hash, layout);
	}

	LOGI("Loaded shader manager cache from %s.\n", path.c_str());
	return true;
}

bool ShaderManager::save_shader_cache(const std::string &path)
{
	if (!device->get_system_handles().filesystem)
		return false;

	using namespace rapidjson;
	Document doc;
	doc.SetObject();
	auto &allocator = doc.GetAllocator();
	doc.AddMember("shaderCacheVersion", uint32_t(ResourceLayout::Version), allocator);

	Value maps(kArrayType);

	meta_cache.variant_to_shader.move_to_read_only();
	auto &var_to_shader = meta_cache.variant_to_shader.get_read_only();

	for (auto &entry : var_to_shader)
	{
		Value map_entry(kObjectType);
		map_entry.AddMember("variant", entry.get_hash(), allocator);
		map_entry.AddMember("spirvHash", entry.shader_hash, allocator);
		map_entry.AddMember("sourceHash", entry.source_hash, allocator);

		ResourceLayout layout;
		if (get_resource_layout_by_shader_hash(entry.shader_hash, layout))
		{
			Value layout_obj = serialize_resource_layout(layout, allocator);
			map_entry.AddMember("layout", layout_obj, allocator);
		}
		else
			LOGE("Failed to lookup resource reflection result. This shouldn't happen ...\n");

		maps.PushBack(map_entry, allocator);
	}

	doc.AddMember("maps", maps, allocator);

	StringBuffer buffer;
	PrettyWriter<StringBuffer> writer(buffer);
	doc.Accept(writer);

	if (!device->get_system_handles().filesystem->write_buffer_to_file(path, buffer.GetString(), buffer.GetSize()))
	{
		LOGE("Failed to open %s for writing.\n", path.c_str());
		return false;
	}
	else
		LOGI("Saved shader manager cache to %s.\n", path.c_str());

	return true;
}
}
