/* Copyright (c) 2017-2018 Hans-Kristian Arntzen
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
ShaderTemplate::ShaderTemplate(Device *device, const std::string &shader_path,
                               PrecomputedShaderCache &cache,
                               Util::Hash path_hash,
                               const std::vector<std::string> &include_directories)
	: device(device), path(shader_path), cache(cache), path_hash(path_hash)
#ifdef GRANITE_VULKAN_SHADER_MANAGER_RUNTIME_COMPILER
	, include_directories(include_directories)
#endif
{
#ifdef GRANITE_VULKAN_SHADER_MANAGER_RUNTIME_COMPILER
	compiler = make_unique<Granite::GLSLCompiler>();
	if (device->get_device_features().supports_vulkan_11_device)
		compiler->set_target(Granite::Target::Vulkan11);
	compiler->set_source_from_file(shader_path);
	compiler->set_include_directories(&include_directories);
	if (!compiler->preprocess())
		throw runtime_error(Util::join("Failed to pre-process shader: ", shader_path));
#else
	(void)include_directories;
#endif
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
			variant->spirv = compiler->compile(defines);
			if (variant->spirv.empty())
			{
				LOGE("Shader error:\n%s\n", compiler->get_error_message().c_str());
				variants.free(variant);
				throw runtime_error("Shader compile failed.");
			}
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
void ShaderTemplate::recompile()
{
	// Recompile all variants.
	try
	{
		auto newcompiler = make_unique<Granite::GLSLCompiler>();
		if (device->get_device_features().supports_vulkan_11_device)
			compiler->set_target(Granite::Target::Vulkan11);
		newcompiler->set_source_from_file(path);
		newcompiler->set_include_directories(&include_directories);
		if (!newcompiler->preprocess())
		{
			LOGE("Failed to preprocess updated shader: %s\n", path.c_str());
			return;
		}
		compiler = move(newcompiler);

		for (auto &variant : variants)
		{
			auto newspirv = compiler->compile(&variant.defines);
			if (newspirv.empty())
			{
				LOGE("Failed to compile shader: %s\n%s\n", path.c_str(), compiler->get_error_message().c_str());
				for (auto &define : variant.defines)
					LOGE("  Define: %s = %d\n", define.first.c_str(), define.second);
				continue;
			}

			variant.spirv = move(newspirv);
			variant.instance++;
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
		manager.register_dependency_nolock(this, dep);
}
#endif

void ShaderProgram::set_stage(Vulkan::ShaderStage stage, ShaderTemplate *shader)
{
	stages[static_cast<unsigned>(stage)] = shader;
	VK_ASSERT(variants.empty());
}

Vulkan::Program *ShaderProgram::get_program(unsigned variant)
{
	auto &var = variants[variant];
	auto *vert = var.stages[static_cast<unsigned>(Vulkan::ShaderStage::Vertex)];
	auto *frag = var.stages[static_cast<unsigned>(Vulkan::ShaderStage::Fragment)];
	auto *comp = var.stages[static_cast<unsigned>(Vulkan::ShaderStage::Compute)];
	if (comp)
	{
		auto &comp_instance = var.shader_instance[static_cast<unsigned>(Vulkan::ShaderStage::Compute)];
#ifdef GRANITE_VULKAN_MT
		var.instance_lock->lock_read();
#endif
		if (comp_instance != comp->instance)
		{
#ifdef GRANITE_VULKAN_MT
			var.instance_lock->promote_reader_to_writer();
#endif
			if (comp_instance != comp->instance)
			{
				comp_instance = comp->instance;
				if (comp->spirv.empty())
				{
					auto *shader = device->request_shader_by_hash(comp->spirv_hash);
					var.program = device->request_program(shader);
				}
				else
				{
					var.program = device->request_program(comp->spirv.data(), comp->spirv.size() * sizeof(uint32_t));
					auto spirv_hash = var.program->get_shader(ShaderStage::Compute)->get_hash();
					cache.emplace_replace(comp->hash, spirv_hash);
				}
			}
			auto ret = var.program;
#ifdef GRANITE_VULKAN_MT
			var.instance_lock->unlock_write();
#endif
			return ret;
		}
		else
		{
			auto ret = var.program;
#ifdef GRANITE_VULKAN_MT
			var.instance_lock->unlock_read();
#endif
			return ret;
		}
	}
	else if (vert && frag)
	{
		auto &vert_instance = var.shader_instance[static_cast<unsigned>(Vulkan::ShaderStage::Vertex)];
		auto &frag_instance = var.shader_instance[static_cast<unsigned>(Vulkan::ShaderStage::Fragment)];
#ifdef GRANITE_VULKAN_MT
		var.instance_lock->lock_read();
#endif

		if (vert_instance != vert->instance || frag_instance != frag->instance)
		{
#ifdef GRANITE_VULKAN_MT
			var.instance_lock->promote_reader_to_writer();
#endif
			if (vert_instance != vert->instance || frag_instance != frag->instance)
			{
				vert_instance = vert->instance;
				frag_instance = frag->instance;
				Shader *vert_shader = nullptr;
				Shader *frag_shader = nullptr;

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

				var.program = device->request_program(vert_shader, frag_shader);
			}
			auto ret = var.program;
#ifdef GRANITE_VULKAN_MT
			var.instance_lock->unlock_write();
#endif
			return ret;
		}
		else
		{
			auto ret = var.program;
#ifdef GRANITE_VULKAN_MT
			var.instance_lock->unlock_read();
#endif
			return ret;
		}
	}

	return {};
}

unsigned ShaderProgram::register_variant(const std::vector<std::pair<std::string, int>> &defines)
{
	Hasher h;
	for (auto &define : defines)
	{
		h.u64(hash<string>()(define.first));
		h.s32(define.second);
	}

	auto hash = h.get();

#ifdef GRANITE_VULKAN_MT
	variant_lock.lock_read();
#endif
	auto itr = find(begin(variant_hashes), end(variant_hashes), hash);
	if (itr != end(variant_hashes))
	{
		auto ret = unsigned(itr - begin(variant_hashes));
#ifdef GRANITE_VULKAN_MT
		variant_lock.unlock_read();
#endif
		return ret;
	}

#ifdef GRANITE_VULKAN_MT
	variant_lock.promote_reader_to_writer();
#endif
	auto index = unsigned(variants.size());
	variants.emplace_back();
	auto &var = variants.back();
	variant_hashes.push_back(hash);

	for (unsigned i = 0; i < static_cast<unsigned>(Vulkan::ShaderStage::Count); i++)
		if (stages[i])
			var.stages[i] = stages[i]->register_variant(&defines);

	// Make sure it's compiled correctly.
	get_program(index);
#ifdef GRANITE_VULKAN_MT
	variant_lock.unlock_write();
#endif

	return index;
}

ShaderProgram *ShaderManager::register_compute(const std::string &compute)
{
	auto *tmpl = get_template(compute);

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
	if (backend)
	{
		handle = backend->install_notification(paths.second, [this](const Granite::FileNotifyInfo &info) {
			recompile(info);
		});
	}

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

bool ShaderManager::load_shader_cache(const string &path)
{
	using namespace rapidjson;

	string json;
	if (!Granite::Global::filesystem()->read_file_to_string(path, json))
	{
		LOGE("Failed to load shader cache %s from disk. Skipping ...\n", path.c_str());
		return false;
	}

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
