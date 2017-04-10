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

private:
	Device *device;
	std::unordered_map<std::string, std::unique_ptr<ShaderTemplate>> shaders;
	Util::HashMap<std::unique_ptr<ShaderProgram>> programs;

	ShaderTemplate *get_template(const std::string &source);
	std::unordered_map<std::string, std::unordered_set<ShaderTemplate *>> dependees;

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