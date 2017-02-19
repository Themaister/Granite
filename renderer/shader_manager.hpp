#pragma once

#include "shader.hpp"
#include <memory>
#include <unordered_map>
#include <string>
#include <vector>
#include "compiler.hpp"
#include "device.hpp"
#include "filesystem.hpp"

namespace Granite
{
class ShaderTemplate
{
public:
	ShaderTemplate(const std::string &shader_path);
	~ShaderTemplate();

	unsigned get_instance() const
	{
		return instance;
	}

	const std::vector<uint32_t> &get_spirv() const
	{
		return spirv;
	}

private:
	void recompile(const FileNotifyInfo &info);
	std::unique_ptr<GLSLCompiler> compiler;
	std::vector<uint32_t> spirv;
	unsigned instance = 0;
	FileNotifyHandle notify_handle = -1;
	FilesystemBackend *notify_backend = nullptr;
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
	ShaderProgram *register_graphics(const std::string &vertex, const std::string &fragment);
	ShaderProgram *register_compute(const std::string &compute);
	static ShaderManager &get();

private:
	std::unordered_map<std::string, std::unique_ptr<ShaderTemplate>> shaders;
	Util::HashMap<std::unique_ptr<ShaderProgram>> programs;
	ShaderManager() = default;
};
}