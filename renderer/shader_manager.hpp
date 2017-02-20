#pragma once

#include "shader.hpp"
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>
#include "compiler.hpp"
#include "device.hpp"
#include "filesystem.hpp"

namespace Granite
{
class ShaderManager;
class ShaderTemplate
{
public:
	ShaderTemplate(const std::string &shader_path);

	unsigned get_instance() const
	{
		return instance;
	}

	const std::vector<uint32_t> &get_spirv() const
	{
		return spirv;
	}

	void recompile();

	void register_dependencies(ShaderManager &manager);

private:
	std::string path;
	std::unique_ptr<GLSLCompiler> compiler;
	std::vector<uint32_t> spirv;
	unsigned instance = 0;
};

class ShaderProgram
{
public:
	Vulkan::ProgramHandle get_program(Vulkan::Device &device);
	void set_stage(Vulkan::ShaderStage stage, const ShaderTemplate *shader);

	unsigned get_instance() const
	{
		return instance;
	}

private:
	const ShaderTemplate *stages[static_cast<unsigned>(Vulkan::ShaderStage::Count)] = {};
	unsigned shader_instance[static_cast<unsigned>(Vulkan::ShaderStage::Count)] = {};
	Vulkan::ProgramHandle program;
	unsigned instance = 0;
};

class ShaderManager
{
public:
	~ShaderManager();
	ShaderProgram *register_graphics(const std::string &vertex, const std::string &fragment);
	ShaderProgram *register_compute(const std::string &compute);
	static ShaderManager &get();

	void register_dependency(ShaderTemplate *shader, const std::string &dependency);

private:
	std::unordered_map<std::string, std::unique_ptr<ShaderTemplate>> shaders;
	Util::HashMap<std::unique_ptr<ShaderProgram>> programs;
	ShaderManager() = default;

	ShaderTemplate *get_template(const std::string &source);
	std::unordered_map<std::string, std::unordered_set<ShaderTemplate *>> dependees;

	struct Notify
	{
		FilesystemBackend *backend;
		FileNotifyHandle handle;
	};
	std::unordered_map<std::string, Notify> directory_watches;
	void add_directory_watch(const std::string &source);
	void recompile(const FileNotifyInfo &info);
};
}