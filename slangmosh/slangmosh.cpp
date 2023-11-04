/* Copyright (c) 2017-2023 Hans-Kristian Arntzen
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
#include "path_utils.hpp"
#include "cli_parser.hpp"
#include "compiler.hpp"
#include "filesystem.hpp"
#include "global_managers_init.hpp"
#include "hash.hpp"
#include "logging.hpp"
#include "rapidjson_wrapper.hpp"
#include "thread_group.hpp"
#include "shader.hpp"
#include <assert.h>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

using namespace Util;
using namespace Granite;

static void print_help()
{
	LOGE("slangmosh <desc.json> [-O] [--strip] [--spv14] [--output header.hpp] [--help] [--output-interface interface.hpp]\n");
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
		GRANITE_THREAD_GROUP()->create_task([=]() {
			GLSLCompiler comp(*Global::filesystem());
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
			GRANITE_THREAD_GROUP()->create_task([=]() {
				GLSLCompiler comp(*Global::filesystem());
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
	if (!GRANITE_FILESYSTEM()->read_file_to_string(path, input_json))
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
                                   const std::string &generated_namespace, bool interface_header)
{
	std::ostringstream str;

	std::vector<uint32_t> spirv_bank;
	std::vector<uint8_t> reflection_bank;

	struct OutputRange
	{
		size_t shader_offset;
		size_t shader_size;
		size_t reflection_offset;
		size_t reflection_size;
	};

	std::unordered_map<Hash, OutputRange> shader_hash_to_output;
	std::vector<std::vector<OutputRange>> variant_to_output;

	if (!interface_header)
	{
		variant_to_output.resize(spirv_for_shaders_and_variants.size());
		for (size_t i = 0; i < variant_to_output.size(); i++)
			variant_to_output[i].resize(spirv_for_shaders_and_variants[i].size());

		for (size_t i = 0; i < spirv_for_shaders_and_variants.size(); i++)
		{
			for (size_t j = 0; j < spirv_for_shaders_and_variants[i].size(); j++)
			{
				auto &perm = spirv_for_shaders_and_variants[i][j];
				auto &output = variant_to_output[i][j];

				Hasher h;
				h.data(perm.data(), perm.size() * sizeof(uint32_t));
				auto hash = h.get();

				auto itr = shader_hash_to_output.find(hash);
				if (itr != shader_hash_to_output.end())
				{
					output = itr->second;
				}
				else
				{
					output.shader_offset = spirv_bank.size();
					output.shader_size = perm.size();
					output.reflection_offset = reflection_bank.size();
					output.reflection_size = Vulkan::ResourceLayout::serialization_size();

					shader_hash_to_output[hash] = output;
					spirv_bank.insert(spirv_bank.end(), perm.begin(), perm.end());

					Vulkan::ResourceLayout layout;
					Vulkan::Shader::reflect_resource_layout(layout, perm.data(), perm.size() * sizeof(uint32_t));
					reflection_bank.resize(reflection_bank.size() + output.reflection_size);
					layout.serialize(reflection_bank.data() + output.reflection_offset, output.reflection_size);
				}
			}
		}
	}

	str << "// Autogenerated from slangmosh, do not edit.\n";
	str << "#ifndef SLANGMOSH_GENERATED_" << generated_namespace << (interface_header ? "iface_H" : "H") << "\n";
	str << "#define SLANGMOSH_GENERATED_" << generated_namespace << (interface_header ? "iface_H" : "H") << "\n";
	str << "#include <stdint.h>\n";
	str << "namespace Vulkan\n{\n";
	str << "class Program;\n";
	str << "class Shader;\n";
	str << "}\n\n";
	str << "namespace ";
	if (generated_namespace.empty())
		str << "ShaderBank";
	else
		str << generated_namespace;
	str << "\n{\n";

	if (!interface_header)
	{
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

		str << "static const uint8_t reflection_bank[] =\n{\n";
		str << std::hex;
		for (size_t i = 0; i < reflection_bank.size(); i++)
		{
			if ((i & 31) == 0)
				str << "\t";

			str << "0x" << std::setfill('0') << std::setw(2) << unsigned(reflection_bank[i]);
			str << ",";

			if (i + 1 == reflection_bank.size() || ((i & 31) == 31))
				str << "\n";
			else
				str << " ";
		}
		str << std::dec;
		str << "};\n\n";
	}

	if (interface_header)
	{
		str << "template <typename Program = Vulkan::Program *, typename Shader = Vulkan::Shader *>\n";
		str << "struct Shaders\n{\n";

		for (auto &shader: shaders)
		{
			str << "\t";
			if (shader.compute)
				str << "Program ";
			else
				str << "Shader ";

			str << shader.name;
			for (auto &var: shader.variants)
				if (!var.resolve)
					str << "[" << var.count << "]";
			str << " = {};\n";
		}
		str << "\tShaders() = default;\n";
		str << "\n\ttemplate <typename Device, typename Layout, typename Resolver>\n";
		str << "\tShaders(Device &device, Layout &layout, const Resolver &resolver);\n";
		str << "};\n";
		str << "}\n";
	}
	else
	{
		str << "template <typename Program, typename Shader>\n";
		str << "template <typename Device, typename Layout, typename Resolver>\n";
		str << "Shaders<Program, Shader>::Shaders(Device &device, Layout &layout, const Resolver &resolver)\n{\n";
		str << "\t(void)resolver;\n";

		for (size_t i = 0; i < shaders.size(); i++)
		{
			auto &shader = shaders[i];

			if (!shader.variants.empty())
			{
				size_t num_perm = shader.total_permutations();
				for (size_t perm = 0; perm < num_perm; perm++)
				{
					bool conditional = false;
					for (auto &var: shader.variants)
						conditional = conditional || var.resolve;

					if (conditional)
					{
						bool first = true;
						str << "\tif (";
						for (size_t variant_index = 0; variant_index < shader.variants.size(); variant_index++)
						{
							if (shader.variants[variant_index].resolve)
							{
								if (!first)
									str << " &&\n\t    ";
								first = false;

								str << "resolver(\"" << shader.name << "\"" << ", " << "\"" <<
								    shader.variants[variant_index].define << "\") == " <<
								    shader.permutation_to_variant_define(perm, variant_index);
							}
						}
						str << ")\n";
					}

					if (conditional)
						str << "\t{\n";

					if (conditional)
						str << "\t";
					str << "\tlayout.unserialize(" << "reflection_bank + "
					    << variant_to_output[i][perm].reflection_offset <<
					    ", " << variant_to_output[i][perm].reflection_size << ");\n";

					if (conditional)
						str << "\t";
					str << "\tthis->" << shader.name;
					for (size_t variant_index = 0; variant_index < shader.variants.size(); variant_index++)
						if (!shader.variants[variant_index].resolve)
							str << "[" << shader.permutation_to_variant_define(perm, variant_index) << "]";

					str << " = device.request_" << (shader.compute ? "program" : "shader") <<
					    "(spirv_bank + " << variant_to_output[i][perm].shader_offset <<
					    ", " << variant_to_output[i][perm].shader_size * sizeof(uint32_t) << ", &layout);\n";

					if (conditional)
						str << "\t}\n";
				}
			}
			else
			{
				str << "\tlayout.unserialize(" << "reflection_bank + " << variant_to_output[i][0].reflection_offset <<
				    ", " << variant_to_output[i][0].reflection_size << ");\n";

				str << "\tthis->" << shader.name << " = device.request_" << (shader.compute ? "program" : "shader") <<
				    "(spirv_bank + " << variant_to_output[i][0].shader_offset << ", " <<
				    variant_to_output[i][0].shader_size * sizeof(uint32_t) << ", &layout);\n";
			}
		}
		str << "}\n";
		str << "}\n";
	}

	str << "#endif\n";
	return str.str();
}

static int main_inner(int argc, char **argv)
{
	std::string output_path;
	std::string input_path;
	std::string generated_namespace;
	std::string output_interface_path;
	bool strip = false;
	bool opt = false;
	Target target = Target::Vulkan11;

	CLICallbacks cbs;
	cbs.add("--help", [](CLIParser &parser) { parser.end(); });
	cbs.add("--output", [&](CLIParser &parser) { output_path = parser.next_string(); });
	cbs.add("-O", [&](CLIParser &) { opt = true; });
	cbs.add("--strip", [&](CLIParser &) { strip = true; });
	cbs.add("--spv14", [&](CLIParser &) { target = Target::Vulkan11_Spirv14; });
	cbs.add("--namespace", [&](CLIParser &parser) { generated_namespace = parser.next_string(); });
	cbs.add("--output-interface", [&](CLIParser &parser) { output_interface_path = parser.next_string(); });
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
		parsed_shader.dispatch_variants(shader_variants.data(), target, opt, strip);
	}

	GRANITE_THREAD_GROUP()->wait_idle();

	for (auto &shader : spirv_for_shaders_and_variants)
		for (auto &perm : shader)
			if (perm.empty())
				return EXIT_FAILURE;

	auto interface_code = generate_header(parsed_shaders, spirv_for_shaders_and_variants, generated_namespace, true);
	auto generated_code = generate_header(parsed_shaders, spirv_for_shaders_and_variants, generated_namespace, false);

	if (output_interface_path.empty())
	{
		generated_code = interface_code + generated_code;
	}
	else if (!GRANITE_FILESYSTEM()->write_string_to_file(output_interface_path, interface_code))
	{
		LOGE("Failed to write to file: %s.\n", output_interface_path.c_str());
		return EXIT_FAILURE;
	}

	if (output_path.empty())
		printf("%s\n", generated_code.c_str());
	else
	{
		if (!GRANITE_FILESYSTEM()->write_string_to_file(output_path, generated_code))
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
