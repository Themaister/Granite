/* Copyright (c) 2017-2022 Hans-Kristian Arntzen
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

#pragma once

#include "shader.hpp"
#include "vulkan_common.hpp"
#include "filesystem.hpp"
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>
#include "hash.hpp"
#ifdef GRANITE_VULKAN_MT
#include "read_write_lock.hpp"
#endif

namespace Granite
{
class GLSLCompiler;
}

namespace Vulkan
{
struct ShaderTemplateVariant;
struct PrecomputedMeta : Util::IntrusiveHashMapEnabled<PrecomputedMeta>
{
	PrecomputedMeta(Util::Hash source_hash_, Util::Hash shader_hash_)
		: source_hash(source_hash_), shader_hash(shader_hash_)
	{
	}
	Util::Hash source_hash;
	Util::Hash shader_hash;
};
using PrecomputedShaderCache = VulkanCacheReadWrite<PrecomputedMeta>;
using ReflectionCache = VulkanCacheReadWrite<Util::IntrusivePODWrapper<ResourceLayout>>;

struct MetaCache
{
	PrecomputedShaderCache variant_to_shader;
	ReflectionCache shader_to_layout;
};

class ShaderManager;
class Device;

struct ShaderTemplateVariant : public Util::IntrusiveHashMapEnabled<ShaderTemplateVariant>
{
	Util::Hash hash = 0;
	Util::Hash spirv_hash = 0;
	std::vector<uint32_t> spirv;
	std::vector<std::pair<std::string, int>> defines;
	std::unique_ptr<ImmutableSamplerBank> sampler_bank;
	unsigned instance = 0;
};

class ShaderTemplate : public Util::IntrusiveHashMapEnabled<ShaderTemplate>
{
public:
	ShaderTemplate(Device *device, const std::string &shader_path, MetaCache &cache,
	               Util::Hash path_hash, const std::vector<std::string> &include_directories);
	~ShaderTemplate();

	bool init();

	const ShaderTemplateVariant *register_variant(const std::vector<std::pair<std::string, int>> *defines = nullptr,
	                                              const ImmutableSamplerBank *sampler_bank = nullptr);
	void recompile();
	void register_dependencies(ShaderManager &manager);

	Util::Hash get_path_hash() const
	{
		return path_hash;
	}

private:
	Device *device;
	std::string path;
	MetaCache &cache;
	Util::Hash path_hash = 0;
#ifdef GRANITE_VULKAN_SHADER_MANAGER_RUNTIME_COMPILER
	std::unique_ptr<Granite::GLSLCompiler> compiler;
	const std::vector<std::string> &include_directories;
	void recompile_variant(ShaderTemplateVariant &variant);
	void update_variant_cache(const ShaderTemplateVariant &variant);
	Util::Hash source_hash = 0;
#endif
	VulkanCache<ShaderTemplateVariant> variants;
};

class ShaderProgramVariant : public Util::IntrusiveHashMapEnabled<ShaderProgramVariant>
{
public:
	explicit ShaderProgramVariant(Device *device);
	Vulkan::Program *get_program();

private:
	friend class ShaderProgram;
	Device *device;
	const ShaderTemplateVariant *stages[static_cast<unsigned>(Vulkan::ShaderStage::Count)] = {};
	std::atomic<unsigned> shader_instance[static_cast<unsigned>(Vulkan::ShaderStage::Count)];
	std::atomic<Vulkan::Program *> program;
#ifdef GRANITE_VULKAN_MT
	Util::RWSpinLock instance_lock;
#endif

	Vulkan::Program *get_program_compute();
	Vulkan::Program *get_program_graphics();
};

class ShaderProgram : public Util::IntrusiveHashMapEnabled<ShaderProgram>
{
public:
	ShaderProgram(Device *device_, ShaderTemplate *compute)
		: device(device_)
	{
		set_stage(Vulkan::ShaderStage::Compute, compute);
	}

	ShaderProgram(Device *device_, ShaderTemplate *vert, ShaderTemplate *frag)
		: device(device_)
	{
		set_stage(Vulkan::ShaderStage::Vertex, vert);
		set_stage(Vulkan::ShaderStage::Fragment, frag);
	}

	void set_stage(Vulkan::ShaderStage stage, ShaderTemplate *shader);
	ShaderProgramVariant *register_variant(const std::vector<std::pair<std::string, int>> &defines,
	                                       const ImmutableSamplerBank *sampler_bank = nullptr);

private:
	Device *device;
	ShaderTemplate *stages[static_cast<unsigned>(Vulkan::ShaderStage::Count)] = {};
	VulkanCacheReadWrite<ShaderProgramVariant> variant_cache;
};

class ShaderManager
{
public:
	explicit ShaderManager(Device *device_)
		: device(device_)
	{
	}

	bool load_shader_cache(const std::string &path);
	bool save_shader_cache(const std::string &path);

	void add_include_directory(const std::string &path);

	~ShaderManager();
	ShaderProgram *register_graphics(const std::string &vertex, const std::string &fragment);
	ShaderProgram *register_compute(const std::string &compute);

#ifdef GRANITE_VULKAN_SHADER_MANAGER_RUNTIME_COMPILER
	void register_dependency(ShaderTemplate *shader, const std::string &dependency);
	void register_dependency_nolock(ShaderTemplate *shader, const std::string &dependency);
#endif

	bool get_shader_hash_by_variant_hash(Util::Hash variant_hash, Util::Hash &shader_hash) const;
	bool get_resource_layout_by_shader_hash(Util::Hash shader_hash, ResourceLayout &layout) const;
	void register_shader_from_variant_hash(Util::Hash variant_hash, Util::Hash source_hash,
	                                       Util::Hash shader_hash, const ResourceLayout &layout);

	Device *get_device()
	{
		return device;
	}

	void promote_read_write_caches_to_read_only();

private:
	Device *device;

	MetaCache meta_cache;
	VulkanCache<ShaderTemplate> shaders;
	VulkanCache<ShaderProgram> programs;
	std::vector<std::string> include_directories;

	ShaderTemplate *get_template(const std::string &source);

#ifdef GRANITE_VULKAN_SHADER_MANAGER_RUNTIME_COMPILER
	std::unordered_map<std::string, std::unordered_set<ShaderTemplate *>> dependees;
#ifdef GRANITE_VULKAN_MT
	std::mutex dependency_lock;
#endif

	struct Notify
	{
		Granite::FilesystemBackend *backend;
		Granite::FileNotifyHandle handle;
	};
	std::unordered_map<std::string, Notify> directory_watches;
	void add_directory_watch(const std::string &source);
	void recompile(const Granite::FileNotifyInfo &info);
#endif
};
}
