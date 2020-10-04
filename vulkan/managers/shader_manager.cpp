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

#include <cstring>
#include <algorithm>
#include "path.hpp"
#include "shader_manager.hpp"
#include "device.hpp"
#include "rapidjson_wrapper.hpp"

using namespace std;
using namespace Util;

#ifdef GRANITE_VULKAN_MT
#define DEPENDENCY_LOCK() std::lock_guard<std::mutex> holder{dependency_lock}
#else
#define DEPENDENCY_LOCK() ((void)0)
#endif

namespace Vulkan
{
ShaderTemplate::ShaderTemplate(Device *device_, const std::string &shader_path,
                               PrecomputedShaderCache &cache_,
                               Util::Hash path_hash_,
                               const std::vector<std::string> &include_directories_)
	: device(device_), path(shader_path), cache(cache_), path_hash(path_hash_)
#ifdef GRANITE_VULKAN_SHADER_MANAGER_RUNTIME_COMPILER
	, include_directories(include_directories_)
#endif
{
}

bool ShaderTemplate::init()
{
#ifdef GRANITE_VULKAN_SHADER_MANAGER_RUNTIME_COMPILER
	compiler = make_unique<Granite::GLSLCompiler>();
	if (device->get_device_features().supports_vulkan_11_device)
		compiler->set_target(Granite::Target::Vulkan11);
	if (!compiler->set_source_from_file(path))
		return false;
	compiler->set_include_directories(&include_directories);
	if (!compiler->preprocess())
	{
		LOGE("Failed to pre-process shader: %s\n", path.c_str());
		compiler.reset();
		return false;
	}
#else
	(void)include_directories;
#endif

	return true;
}

const ShaderTemplate::Variant *ShaderTemplate::register_variant(const std::vector<std::pair<std::string, int>> *defines)
{
	Hasher h;
	if (defines)
	{
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

		if (!cache.find_and_consume_pod(complete_hash, variant->spirv_hash))
		{
#ifdef GRANITE_VULKAN_SHADER_MANAGER_RUNTIME_COMPILER
			if (compiler)
			{
				std::string error_message;
				variant->spirv = compiler->compile(error_message, defines);
				if (variant->spirv.empty())
				{
					LOGE("Shader error:\n%s\n", error_message.c_str());
					variants.free(variant);
					return nullptr;
				}
			}
			else
				return nullptr;
#else
			LOGE("Could not find shader variant for %s in cache.\n", path.c_str());
			variants.free(variant);
			return nullptr;
#endif
		}

		variant->instance++;
		if (defines)
			variant->defines = *defines;

		ret = variants.insert_yield(hash, variant);
	}
	return ret;
}

#ifdef GRANITE_VULKAN_SHADER_MANAGER_RUNTIME_COMPILER
void ShaderTemplate::recompile_variant(Variant &variant)
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

	variant.spirv = move(newspirv);
	variant.instance++;
}

void ShaderTemplate::recompile()
{
	// Recompile all variants.
	auto newcompiler = make_unique<Granite::GLSLCompiler>();
	if (device->get_device_features().supports_vulkan_11_device)
		newcompiler->set_target(Granite::Target::Vulkan11);
	if (!newcompiler->set_source_from_file(path))
		return;
	newcompiler->set_include_directories(&include_directories);
	if (!newcompiler->preprocess())
	{
		LOGE("Failed to preprocess updated shader: %s\n", path.c_str());
		return;
	}
	compiler = move(newcompiler);

#ifdef GRANITE_VULKAN_MT
	for (auto &variant : variants.get_read_only())
		recompile_variant(variant);
	for (auto &variant : variants.get_read_write())
		recompile_variant(variant);
#else
	for (auto &variant : variants)
		recompile_variant(variant);
#endif
}

void ShaderTemplate::register_dependencies(ShaderManager &manager)
{
	for (auto &dep : compiler->get_dependencies())
		manager.register_dependency_nolock(this, dep);
}
#endif

void ShaderProgram::set_stage(Vulkan::ShaderStage stage, ShaderTemplate *shader)
{
	stages[static_cast<unsigned>(stage)] = shader;
	VK_ASSERT(variant_cache.begin() == variant_cache.end());
}

ShaderProgramVariant::ShaderProgramVariant(Device *device_, PrecomputedShaderCache &cache_)
	: device(device_), cache(cache_)
{
	for (auto &inst : shader_instance)
		inst.store(0, std::memory_order_relaxed);
	program.store(nullptr, std::memory_order_relaxed);
}

Vulkan::Program *ShaderProgramVariant::get_program_compute()
{
	Vulkan::Program *ret;

	auto *comp = stages[Util::ecast(Vulkan::ShaderStage::Compute)];
	auto &comp_instance = shader_instance[Util::ecast(Vulkan::ShaderStage::Compute)];

	// If we have observed all possible compilation instances,
	// we can safely read program directly.
	// comp->instance will only ever be incremented in the main thread on an inotify, so this is fine.
	// If comp->instance changes in the interim, we are at least guaranteed to read a sensible value for program.
	unsigned loaded_instance = comp_instance.load(std::memory_order_acquire);
	if (loaded_instance == comp->instance)
		return program.load(std::memory_order_relaxed);

#ifdef GRANITE_VULKAN_MT
	instance_lock.lock_write();
#endif
	if (comp_instance.load(std::memory_order_relaxed) != comp->instance)
	{
		Vulkan::Program *new_program;
		if (comp->spirv.empty())
		{
			auto *shader = device->request_shader_by_hash(comp->spirv_hash);
			new_program = device->request_program(shader);
		}
		else
		{
			new_program = device->request_program(comp->spirv.data(), comp->spirv.size() * sizeof(uint32_t));
			auto spirv_hash = new_program->get_shader(ShaderStage::Compute)->get_hash();
			cache.emplace_replace(comp->hash, spirv_hash);
		}

		program.store(new_program, std::memory_order_relaxed);
		ret = new_program;
		comp_instance.store(comp->instance, std::memory_order_release);
	}
	else
	{
		ret = program.load(std::memory_order_relaxed);
	}
#ifdef GRANITE_VULKAN_MT
	instance_lock.unlock_write();
#endif

	return ret;
}

Vulkan::Program *ShaderProgramVariant::get_program_graphics()
{
	Vulkan::Program *ret;
	auto *vert = stages[Util::ecast(Vulkan::ShaderStage::Vertex)];
	auto *frag = stages[Util::ecast(Vulkan::ShaderStage::Fragment)];
	auto &vert_instance = shader_instance[Util::ecast(Vulkan::ShaderStage::Vertex)];
	auto &frag_instance = shader_instance[Util::ecast(Vulkan::ShaderStage::Fragment)];

	unsigned loaded_vert_instance = vert_instance.load(std::memory_order_acquire);
	unsigned loaded_frag_instance = frag_instance.load(std::memory_order_acquire);

	// If we have observed all possible compilation instances,
	// we can safely read program directly.
	// comp->instance will only ever be incremented in the main thread on an inotify, so this is fine.
	// If comp->instance changes in the interim, we are at least guaranteed to read a sensible value for program.
	if (loaded_vert_instance == vert->instance && loaded_frag_instance == frag->instance)
		return program.load(std::memory_order_relaxed);

#ifdef GRANITE_VULKAN_MT
	instance_lock.lock_write();
#endif
	if (vert_instance.load(std::memory_order_relaxed) != vert->instance ||
	    frag_instance.load(std::memory_order_relaxed) != frag->instance)
	{
		Shader *vert_shader;
		Shader *frag_shader;

		if (vert->spirv.empty())
			vert_shader = device->request_shader_by_hash(vert->spirv_hash);
		else
		{
			vert_shader = device->request_shader(vert->spirv.data(), vert->spirv.size() * sizeof(uint32_t));
			cache.emplace_replace(vert->hash, vert_shader->get_hash());
		}

		if (frag->spirv.empty())
			frag_shader = device->request_shader_by_hash(frag->spirv_hash);
		else
		{
			frag_shader = device->request_shader(frag->spirv.data(), frag->spirv.size() * sizeof(uint32_t));
			cache.emplace_replace(frag->hash, frag_shader->get_hash());
		}

		auto *new_program = device->request_program(vert_shader, frag_shader);
		program.store(new_program, std::memory_order_relaxed);
		ret = new_program;
		vert_instance.store(vert->instance, std::memory_order_release);
		frag_instance.store(frag->instance, std::memory_order_release);
	}
	else
	{
		ret = program.load(std::memory_order_relaxed);
	}
#ifdef GRANITE_VULKAN_MT
	instance_lock.unlock_write();
#endif

	return ret;
}

Vulkan::Program *ShaderProgramVariant::get_program()
{
	auto *vert = stages[static_cast<unsigned>(Vulkan::ShaderStage::Vertex)];
	auto *frag = stages[static_cast<unsigned>(Vulkan::ShaderStage::Fragment)];
	auto *comp = stages[static_cast<unsigned>(Vulkan::ShaderStage::Compute)];

	if (comp)
		return get_program_compute();
	else if (vert && frag)
		return get_program_graphics();
	else
		return nullptr;
}

ShaderProgramVariant *ShaderProgram::register_variant(const std::vector<std::pair<std::string, int>> &defines)
{
	Hasher h;
	for (auto &define : defines)
	{
		h.string(define.first);
		h.s32(define.second);
	}

	auto hash = h.get();

	if (auto *variant = variant_cache.find(hash))
		return variant;

	auto *new_variant = variant_cache.allocate(device, cache);

	for (unsigned i = 0; i < static_cast<unsigned>(Vulkan::ShaderStage::Count); i++)
		if (stages[i])
			new_variant->stages[i] = stages[i]->register_variant(&defines);

	// Make sure it's compiled correctly.
	new_variant->get_program();

	new_variant = variant_cache.insert_yield(hash, new_variant);
	return new_variant;
}

ShaderProgram *ShaderManager::register_compute(const std::string &compute)
{
	auto *tmpl = get_template(compute);
	if (!tmpl)
		return nullptr;

	Util::Hasher h;
	h.u64(tmpl->get_path_hash());
	auto hash = h.get();

	auto *ret = programs.find(hash);
	if (!ret)
		ret = programs.emplace_yield(hash, device, shader_cache, tmpl);
	return ret;
}

ShaderTemplate *ShaderManager::get_template(const std::string &path)
{
	Hasher hasher;
	hasher.string(path);
	auto hash = hasher.get();

	auto *ret = shaders.find(hash);
	if (!ret)
	{
		auto *shader = shaders.allocate(device, path, shader_cache, hasher.get(), include_directories);
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
	auto *vert_tmpl = get_template(vertex);
	auto *frag_tmpl = get_template(fragment);
	if (!vert_tmpl || !frag_tmpl)
		return nullptr;

	Util::Hasher h;
	h.u64(vert_tmpl->get_path_hash());
	h.u64(frag_tmpl->get_path_hash());
	auto hash = h.get();

	auto *ret = programs.find(hash);
	if (!ret)
		ret = programs.emplace_yield(hash, device, shader_cache, vert_tmpl, frag_tmpl);
	return ret;
}

ShaderManager::~ShaderManager()
{
#ifdef GRANITE_VULKAN_SHADER_MANAGER_RUNTIME_COMPILER
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
	add_directory_watch(dependency);
}

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
	auto basedir = Granite::Path::basedir(source);
	if (directory_watches.find(basedir) != end(directory_watches))
		return;

	auto paths = Granite::Path::protocol_split(basedir);
	auto *backend = Granite::Global::filesystem()->get_backend(paths.first);
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

void ShaderManager::register_shader_hash_from_variant_hash(Hash variant_hash, Hash shader_hash)
{
	shader_cache.emplace_replace(variant_hash, shader_hash);
}

bool ShaderManager::get_shader_hash_by_variant_hash(Hash variant_hash, Hash &shader_hash)
{
	return shader_cache.find_and_consume_pod(variant_hash, shader_hash);
}

void ShaderManager::add_include_directory(const string &path)
{
	if (find(begin(include_directories), end(include_directories), path) == end(include_directories))
		include_directories.push_back(path);
}

void ShaderManager::promote_read_write_caches_to_read_only()
{
#ifdef GRANITE_VULKAN_MT
	shaders.move_to_read_only();
	programs.move_to_read_only();
#endif
}

bool ShaderManager::load_shader_cache(const string &path)
{
	using namespace rapidjson;

	string json;
	if (!Granite::Global::filesystem()->read_file_to_string(path, json))
		return false;

	Document doc;
	doc.Parse(json);
	if (doc.HasParseError())
	{
		LOGE("Failed to parse shader cache format!\n");
		return false;
	}

	auto &maps = doc["maps"];
	for (auto itr = maps.Begin(); itr != maps.End(); ++itr)
	{
		auto &value = *itr;
		shader_cache.emplace_replace(value["variant"].GetUint64(), value["spirvHash"].GetUint64());
	}

	LOGI("Loaded shader manager cache from %s.\n", path.c_str());
	return true;
}

bool ShaderManager::save_shader_cache(const string &path)
{
	using namespace rapidjson;
	Document doc;
	doc.SetObject();
	auto &allocator = doc.GetAllocator();

	Value maps(kArrayType);

	for (auto &entry : shader_cache)
	{
		Value map_entry(kObjectType);
		map_entry.AddMember("variant", entry.get_hash(), allocator);
		map_entry.AddMember("spirvHash", entry.get(), allocator);
		maps.PushBack(map_entry, allocator);
	}

	doc.AddMember("maps", maps, allocator);

	StringBuffer buffer;
	PrettyWriter<StringBuffer> writer(buffer);
	doc.Accept(writer);

	auto file = Granite::Global::filesystem()->open(path, Granite::FileMode::WriteOnly);
	if (!file)
	{
		LOGE("Failed to open %s for writing.\n", path.c_str());
		return false;
	}

	void *mapped = file->map_write(buffer.GetSize());
	if (!mapped)
	{
		LOGE("Failed to map buffer %s for writing.\n", path.c_str());
		return false;
	}

	memcpy(mapped, buffer.GetString(), buffer.GetSize());
	file->unmap();

	LOGI("Saved shader manager cache to %s.\n", path.c_str());
	return true;
}
}
