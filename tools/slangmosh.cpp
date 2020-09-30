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

#include "cli_parser.hpp"
#include "global_managers.hpp"
#include "rapidjson_wrapper.hpp"
#include "logging.hpp"
#include "filesystem.hpp"
#include "compiler.hpp"
#include "path.hpp"
#include "thread_group.hpp"
#include "hash.hpp"
#include <sstream>
#include <iomanip>
#include <iostream>
#include <unordered_map>
#include <string>
#include <vector>
#include <assert.h>

using namespace Util;
using namespace Granite;

static void print_help()
{
	LOGE("slangmosh <desc.json> [-O] [--strip] [--vk11] [--output header.hpp] [--help]\n");
}

struct ShaderVariant
{
	std::string define;
	unsigned count = 0;
	bool resolve = false;
};

struct Shader
{
	std::string path;
	std::string name;
	std::vector<ShaderVariant> variants;
	std::vector<std::string> include;
	bool compute = false;

	size_t total_permutations() const;
	void dispatch_variants(std::vector<uint32_t> *output_spirv, Target target, bool opt, bool strip) const;
	int permutation_to_variant_define(size_t permutation, size_t variant_index) const;
	size_t stride_for_variant_index(size_t variant_index) const;
};

size_t Shader::stride_for_variant_index(size_t variant_index) const
{
	size_t stride = 1;
	for (size_t i = 0; i < variant_index; i++)
		stride *= variants[i].count;
	return stride;
}

int Shader::permutation_to_variant_define(size_t permutation, size_t variant_index) const
{
	size_t stride = stride_for_variant_index(variant_index);
	size_t unstrided_index = permutation / stride;
	size_t wrapped_index = unstrided_index % variants[variant_index].count;
	return int(wrapped_index);
}

void Shader::dispatch_variants(std::vector<uint32_t> *output_spirv, Target target, bool opt, bool strip) const
{
	if (variants.empty())
	{
		Global::thread_group()->create_task([=]() {
			GLSLCompiler comp;
			comp.set_source_from_file(path);
			comp.set_target(target);
			comp.set_optimization(opt ? GLSLCompiler::Optimization::ForceOn : GLSLCompiler::Optimization::ForceOff);
			comp.set_strip(strip);
			comp.set_include_directories(&include);
			if (!comp.preprocess())
			{
				LOGE("Failed to preprocess shader: %s.\n", path.c_str());
				return;
			}

			std::string error_message;
			output_spirv[0] = comp.compile(error_message, nullptr);
			if (output_spirv[0].empty())
			{
				LOGE("Failed to compile shader: %s.\n", path.c_str());
				LOGE("%s\n", error_message.c_str());
				return;
			}
		});
	}
	else
	{
		size_t num_permutations = total_permutations();
		for (size_t perm = 0; perm < num_permutations; perm++)
		{
			Global::thread_group()->create_task([=]() {
				GLSLCompiler comp;
				comp.set_source_from_file(path);
				comp.set_target(target);
				comp.set_optimization(opt ? GLSLCompiler::Optimization::ForceOn : GLSLCompiler::Optimization::ForceOff);
				comp.set_strip(strip);
				comp.set_include_directories(&include);
				if (!comp.preprocess())
				{
					LOGE("Failed to preprocess shader: %s.\n", path.c_str());
					return;
				}

				std::vector<std::pair<std::string, int>> defines;
				defines.resize(variants.size());
				for (size_t i = 0; i < defines.size(); i++)
					defines[i] = { variants[i].define, permutation_to_variant_define(perm, i) };

				std::string error_message;
				output_spirv[perm] = comp.compile(error_message, &defines);
				if (output_spirv[perm].empty())
				{
					LOGE("Failed to compile shader: %s with defines:\n", path.c_str());
					for (auto &def : defines)
						LOGE("  #define %s %d.\n", def.first.c_str(), def.second);
					LOGE("%s\n", error_message.c_str());
					return;
				}
			});
		}
	}
}

size_t Shader::total_permutations() const
{
	if (variants.empty())
		return 1;

	size_t perm = 1;
	for (auto &var : variants)
		perm *= var.count;

	assert(perm > 0);
	return perm;
}

static std::vector<Shader> parse_shaders(const std::string &path)
{
	std::vector<Shader> parsed_shaders;

	std::string input_json;
	if (!Global::filesystem()->read_file_to_string(path, input_json))
	{
		LOGE("Failed to read file: %s.\n", path.c_str());
		return parsed_shaders;
	}

	rapidjson::Document doc;
	doc.Parse(input_json);

	if (doc.HasParseError())
	{
		LOGE("Failed to parse JSON.\n");
		return parsed_shaders;
	}

	std::vector<std::string> base_include;
	if (doc.HasMember("include"))
	{
		auto &includes = doc["include"];
		for (auto include_itr = includes.Begin(); include_itr != includes.End(); ++include_itr)
			base_include.emplace_back(Path::relpath(path, include_itr->GetString()));
	}

	auto &shaders = doc["shaders"];
	for (auto itr = shaders.Begin(); itr != shaders.End(); ++itr)
	{
		auto &shader = *itr;
		Shader parsed_shader;
		parsed_shader.path = Path::relpath(path, shader["path"].GetString());
		parsed_shader.name = shader["name"].GetString();
		parsed_shader.include = base_include;

		if (shader.HasMember("variants"))
		{
			auto &variants = shader["variants"];
			for (auto var_itr = variants.Begin(); var_itr != variants.End(); ++var_itr)
			{
				ShaderVariant parsed_var;
				auto &variant = *var_itr;
				parsed_var.define = variant["define"].GetString();
				parsed_var.count = variant["count"].GetUint();
				if (variant.HasMember("resolve"))
					parsed_var.resolve = variant["resolve"].GetBool();
				parsed_shader.variants.push_back(std::move(parsed_var));
			}
		}

		if (shader.HasMember("compute"))
			parsed_shader.compute = shader["compute"].GetBool();

		if (shader.HasMember("include"))
		{
			auto &includes = shader["include"];
			for (auto include_itr = includes.Begin(); include_itr != includes.End(); ++include_itr)
				parsed_shader.include.emplace_back(Path::relpath(path, include_itr->GetString()));
		}

		parsed_shaders.push_back(std::move(parsed_shader));
	}

	return parsed_shaders;
}

static std::string generate_header(const std::vector<Shader> &shaders,
                                   const std::vector<std::vector<std::vector<uint32_t>>> &spirv_for_shaders_and_variants,
                                   const std::string &generated_namespace)
{
	std::ostringstream str;

	std::vector<uint32_t> spirv_bank;
	std::unordered_map<Hash, std::pair<size_t, size_t>> shader_hash_to_size_offset;
	std::vector<std::vector<std::pair<size_t, size_t>>> variant_to_offset_size;

	variant_to_offset_size.resize(spirv_for_shaders_and_variants.size());
	for (size_t i = 0; i < variant_to_offset_size.size(); i++)
		variant_to_offset_size[i].resize(spirv_for_shaders_and_variants[i].size());

	for (size_t i = 0; i < spirv_for_shaders_and_variants.size(); i++)
	{
		for (size_t j = 0; j < spirv_for_shaders_and_variants[i].size(); j++)
		{
			auto &perm = spirv_for_shaders_and_variants[i][j];
			auto &offset_size = variant_to_offset_size[i][j];

			Hasher h;
			h.data(perm.data(), perm.size() * sizeof(uint32_t));
			auto hash = h.get();

			auto itr = shader_hash_to_size_offset.find(hash);
			if (itr != shader_hash_to_size_offset.end())
			{
				offset_size = itr->second;
			}
			else
			{
				offset_size.first = spirv_bank.size();
				offset_size.second = perm.size();
				shader_hash_to_size_offset[hash] = offset_size;
				spirv_bank.insert(spirv_bank.end(), perm.begin(), perm.end());
			}
		}
	}

	str << "// Autogenerated from slangmosh, do not edit.\n";
	str << "#pragma once\n";
	str << "#include <stdint.h>\n";
	str << "namespace ";
	if (generated_namespace.empty())
		str << "ShaderBank";
	else
		str << generated_namespace;
	str << "\n{\n";

	str << "static const uint32_t spirv_bank[] =\n{\n";
	str << std::hex;
	for (size_t i = 0; i < spirv_bank.size(); i++)
	{
		if ((i & 7) == 0)
			str << "\t";

		str << "0x" << std::setfill('0') << std::setw(8) << spirv_bank[i] << "u";
		str << ",";

		if (i + 1 == spirv_bank.size() || ((i & 7) == 7))
			str << "\n";
		else
			str << " ";
	}
	str << std::dec;
	str << "};\n\n";

	str << "template <typename Program, typename Shader>\n";
	str << "struct Shaders\n{\n";

	for (auto &shader : shaders)
	{
		str << "\t";
		if (shader.compute)
			str << "Program ";
		else
			str << "Shader ";

		str << shader.name;
		for (auto &var : shader.variants)
			if (!var.resolve)
				str << "[" << var.count << "]";
		str << " = {};\n";
	}

	str << "\n\ttemplate <typename Device, typename Resolver>\n";
	str << "\tShaders(Device &device, const Resolver &resolver)\n\t{\n";

	for (size_t i = 0; i < shaders.size(); i++)
	{
		auto &shader = shaders[i];

		if (!shader.variants.empty())
		{
			size_t num_perm = shader.total_permutations();
			for (size_t perm = 0; perm < num_perm; perm++)
			{
				bool conditional = false;
				for (auto &var : shader.variants)
					conditional = conditional || var.resolve;

				if (conditional)
				{
					bool first = true;
					str << "\t\tif (";
					for (size_t variant_index = 0; variant_index < shader.variants.size(); variant_index++)
					{
						if (shader.variants[variant_index].resolve)
						{
							if (!first)
								str << " &&\n\t\t    ";
							first = false;

							str << "resolver(\"" << shader.name << "\"" << ", " << "\"" <<
							    shader.variants[variant_index].define << "\") == " <<
							    shader.permutation_to_variant_define(perm, variant_index);
						}
					}
					str << ")\n\t";
				}

				str << "\t\tthis->" << shader.name;
				for (size_t variant_index = 0; variant_index < shader.variants.size(); variant_index++)
					if (!shader.variants[variant_index].resolve)
						str << "[" << shader.permutation_to_variant_define(perm, variant_index) << "]";

				str << " = device.request_" << (shader.compute ? "program" : "shader") <<
					"(spirv_bank + " << variant_to_offset_size[i][perm].first <<
					", " << variant_to_offset_size[i][perm].second * sizeof(uint32_t) << ");\n";
			}
		}
		else
		{
			str << "\t\tthis->" << shader.name << " = device.request_" << (shader.compute ? "program" : "shader") <<
			    "(spirv_bank + " << variant_to_offset_size[i][0].first << ", " <<
			    variant_to_offset_size[i][0].second * sizeof(uint32_t) << ");\n";
		}
	}

	str << "\t}\n";

	str << "};\n";
	str << "}\n";
	return str.str();
}

static int main_inner(int argc, char **argv)
{
	std::string output_path;
	std::string input_path;
	std::string generated_namespace;
	bool strip = false;
	bool opt = false;
	bool vk11 = false;

	CLICallbacks cbs;
	cbs.add("--help", [](CLIParser &parser) { parser.end(); });
	cbs.add("--output", [&](CLIParser &parser) { output_path = parser.next_string(); });
	cbs.add("-O", [&](CLIParser &) { opt = true; });
	cbs.add("--strip", [&](CLIParser &) { strip = true; });
	cbs.add("--vk11", [&](CLIParser &) { vk11 = true; });
	cbs.add("--namespace", [&](CLIParser &parser) { generated_namespace = parser.next_string(); });
	cbs.default_handler = [&](const char *str) { input_path = str; };

	CLIParser parser(std::move(cbs), argc - 1, argv + 1);
	if (!parser.parse())
		return EXIT_FAILURE;
	else if (parser.is_ended_state())
	{
		print_help();
		return EXIT_SUCCESS;
	}

	if (input_path.empty())
	{
		LOGE("Need input path.\n");
		print_help();
		return EXIT_FAILURE;
	}

	auto parsed_shaders = parse_shaders(input_path);
	if (parsed_shaders.empty())
	{
		LOGE("Failed to parse shaders.\n");
		return EXIT_FAILURE;
	}

	std::vector<std::vector<std::vector<uint32_t>>> spirv_for_shaders_and_variants;
	spirv_for_shaders_and_variants.resize(parsed_shaders.size());

	for (size_t shader_index = 0; shader_index < parsed_shaders.size(); shader_index++)
	{
		auto &shader_variants = spirv_for_shaders_and_variants[shader_index];
		auto &parsed_shader = parsed_shaders[shader_index];
		shader_variants.resize(parsed_shader.total_permutations());
		parsed_shader.dispatch_variants(shader_variants.data(), vk11 ? Target::Vulkan11 : Target::Vulkan10, opt, strip);
	}

	Global::thread_group()->wait_idle();

	for (auto &shader : spirv_for_shaders_and_variants)
		for (auto &perm : shader)
			if (perm.empty())
				return EXIT_FAILURE;

	auto generated_code = generate_header(parsed_shaders, spirv_for_shaders_and_variants, generated_namespace);

	if (output_path.empty())
		printf("%s\n", generated_code.c_str());
	else
	{
		if (!Global::filesystem()->write_string_to_file(output_path, generated_code))
		{
			LOGE("Failed to write to file: %s.\n", output_path.c_str());
			return EXIT_FAILURE;
		}
	}

	return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
	Global::init(Global::MANAGER_FEATURE_EVENT_BIT |
	             Global::MANAGER_FEATURE_FILESYSTEM_BIT |
	             Global::MANAGER_FEATURE_THREAD_GROUP_BIT);
	int ret = main_inner(argc, argv);
	Global::deinit();
	return ret;
}
