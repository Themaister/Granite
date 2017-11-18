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

#pragma once

#include "shader.hpp"
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>
#include "compiler.hpp"
#include "filesystem.hpp"
#include "hashmap.hpp"
#include "read_write_lock.hpp"

namespace Vulkan
{
class ShaderManager;
class Device;
class ShaderTemplate
{
public:
	ShaderTemplate(const std::string &shader_path);

	struct Variant
	{
		std::vector<uint32_t> spirv;
		std::vector<std::pair<std::string, int>> defines;
		unsigned instance = 0;
	};

	const Variant *register_variant(const std::vector<std::pair<std::string, int>> *defines = nullptr);
	void recompile();
	void register_dependencies(ShaderManager &manager);

private:
	std::string path;
	std::unique_ptr<Granite::GLSLCompiler> compiler;
	Util::HashMap<std::unique_ptr<Variant>> variants;
};

class ShaderProgram
{
public:
	ShaderProgram(Device *device)
		: device(device)
	{
	}

	Vulkan::ProgramHandle get_program(unsigned variant);
	void set_stage(Vulkan::ShaderStage stage, ShaderTemplate *shader);
	unsigned register_variant(const std::vector<std::pair<std::string, int>> &defines);

private:
	Device *device;

	struct Variant
	{
		const ShaderTemplate::Variant *stages[static_cast<unsigned>(Vulkan::ShaderStage::Count)] = {};
		unsigned shader_instance[static_cast<unsigned>(Vulkan::ShaderStage::Count)] = {};
		Vulkan::ProgramHandle program;
	};

	ShaderTemplate *stages[static_cast<unsigned>(Vulkan::ShaderStage::Count)] = {};
	std::vector<Variant> variants;
	std::vector<Util::Hash> variant_hashes;
	Util::RWSpinLock variant_lock;
};

class ShaderManager
{
public:
	ShaderManager(Device *device)
		: device(device)
	{
	}

	~ShaderManager();
	ShaderProgram *register_graphics(const std::string &vertex, const std::string &fragment);
	ShaderProgram *register_compute(const std::string &compute);

	void register_dependency(ShaderTemplate *shader, const std::string &dependency);
	void register_dependency_nolock(ShaderTemplate *shader, const std::string &dependency);

private:
	Device *device;
	std::unordered_map<std::string, std::unique_ptr<ShaderTemplate>> shaders;
	Util::RWSpinLock template_lock;
	Util::HashMap<std::unique_ptr<ShaderProgram>> programs;
	Util::RWSpinLock programs_lock;

	ShaderTemplate *get_template(const std::string &source);
	std::unordered_map<std::string, std::unordered_set<ShaderTemplate *>> dependees;
	std::mutex dependency_lock;

	struct Notify
	{
		Granite::FilesystemBackend *backend;
		Granite::FileNotifyHandle handle;
	};
	std::unordered_map<std::string, Notify> directory_watches;
	void add_directory_watch(const std::string &source);
	void recompile(const Granite::FileNotifyInfo &info);
};
}