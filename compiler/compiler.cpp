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

#include "compiler.hpp"
#include <shaderc/shaderc.hpp>
#include <path.hpp>
#include "logging.hpp"
#include "filesystem.hpp"
#include "string_helpers.hpp"

#include "spirv-tools/libspirv.hpp"

using namespace std;

namespace Granite
{

Stage GLSLCompiler::stage_from_path(const std::string &path)
{
	auto ext = Path::ext(path);
	if (ext == "vert")
		return Stage::Vertex;
	else if (ext == "tesc")
		return Stage::TessControl;
	else if (ext == "tese")
		return Stage::TessEvaluation;
	else if (ext == "geom")
		return Stage::Geometry;
	else if (ext == "frag")
		return Stage::Fragment;
	else if (ext == "comp")
		return Stage::Compute;
	else
		return Stage::Unknown;
}

bool GLSLCompiler::set_source_from_file(const string &path)
{
	if (!Global::filesystem()->read_file_to_string(path, source))
	{
		LOGE("Failed to load shader: %s\n", path.c_str());
		return false;
	}

	source_path = path;
	stage = stage_from_path(path);
	return stage != Stage::Unknown;
}

void GLSLCompiler::set_include_directories(const std::vector<std::string> *include_directories_)
{
	include_directories = include_directories_;
}

bool GLSLCompiler::find_include_path(const string &source_path_, const string &include_path,
                                     string &included_path, string &included_source)
{
	auto relpath = Path::relpath(source_path_, include_path);
	if (Global::filesystem()->read_file_to_string(relpath, included_source))
	{
		included_path = relpath;
		return true;
	}

	if (include_directories)
	{
		for (auto &include_dir : *include_directories)
		{
			auto path = Path::join(include_dir, include_path);
			if (Global::filesystem()->read_file_to_string(path, included_source))
			{
				included_path = path;
				return true;
			}
		}
	}

	return false;
}

bool GLSLCompiler::parse_variants(const string &source_, const string &path)
{
	auto lines = Util::split(source_, "\n");

	unsigned line_index = 0;
	for (auto &line : lines)
	{
		if (line.find("#include \"") == 0)
		{
			auto include_path = line.substr(10);
			if (!include_path.empty() && include_path.back() == '"')
				include_path.pop_back();

			string included_source;
			if (!find_include_path(path, include_path, include_path, included_source))
			{
				LOGE("Failed to include GLSL file: %s\n", include_path.c_str());
				return false;
			}

			preprocessed_source += Util::join("#line ", 1, " \"", include_path, "\"\n");
			if (!parse_variants(included_source, include_path))
				return false;
			preprocessed_source += Util::join("#line ", line_index + 2, " \"", path, "\"\n");

			dependencies.insert(include_path);
		}
		else if (line.find("#pragma optimize off") == 0)
		{
			optimization = Optimization::ForceOff;
			preprocessed_source += "// #pragma optimize off";
			preprocessed_source += '\n';
		}
		else if (line.find("#pragma optimize on") == 0)
		{
			optimization = Optimization::ForceOn;
			preprocessed_source += "// #pragma optimize on";
			preprocessed_source += '\n';
		}
		else
		{
			preprocessed_source += line;
			preprocessed_source += '\n';

			auto first_non_space = line.find_first_not_of(' ');
			if (first_non_space != string::npos && line[first_non_space] == '#')
			{
				auto keywords = Util::split(line.substr(first_non_space + 1), " ");
				if (keywords.size() == 1)
				{
					auto &word = keywords.front();
					if (word == "endif")
						preprocessed_source += Util::join("#line ", line_index + 2, " \"", path, "\"\n");
				}
			}
		}

		line_index++;
	}
	return true;
}

bool GLSLCompiler::preprocess()
{
	preprocessed_source.clear();
	return parse_variants(source, source_path);
}

vector<uint32_t> GLSLCompiler::compile(std::string &error_message, const vector<pair<string, int>> *defines) const
{
	shaderc::Compiler compiler;
	shaderc::CompileOptions options;
	if (preprocessed_source.empty())
	{
		LOGE("Need to preprocess source first.\n");
		return {};
	}

	if (defines)
		for (auto &define : *defines)
			options.AddMacroDefinition(define.first, to_string(define.second));

#if GRANITE_COMPILER_OPTIMIZE
	if (optimization != Optimization::ForceOff)
		options.SetOptimizationLevel(shaderc_optimization_level_performance);
	else
		options.SetOptimizationLevel(shaderc_optimization_level_zero);
#else
	options.SetOptimizationLevel(shaderc_optimization_level_zero);
#endif

	if (!strip)
	{
		// Need this for some reflection purposes with immutable samplers.
		options.SetGenerateDebugInfo();
	}

	options.SetTargetEnvironment(shaderc_target_env_vulkan,
	                             target == Target::Vulkan11 ?
	                             shaderc_env_version_vulkan_1_1 :
	                             shaderc_env_version_vulkan_1_0);

	options.SetSourceLanguage(shaderc_source_language_glsl);

	shaderc_shader_kind kind;
	switch (stage)
	{
	case Stage::Vertex:
		kind = shaderc_glsl_vertex_shader;
		break;

	case Stage::TessControl:
		kind = shaderc_glsl_tess_control_shader;
		break;

	case Stage::TessEvaluation:
		kind = shaderc_glsl_tess_evaluation_shader;
		break;

	case Stage::Geometry:
		kind = shaderc_glsl_geometry_shader;
		break;

	case Stage::Fragment:
		kind = shaderc_glsl_fragment_shader;
		break;

	case Stage::Compute:
		kind = shaderc_glsl_compute_shader;
		break;

	default:
		return {};
	}
	shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(preprocessed_source, kind, source_path.c_str(), options);

	error_message.clear();
	if (result.GetCompilationStatus() != shaderc_compilation_status_success)
	{
		error_message = result.GetErrorMessage();
		return {};
	}

	vector<uint32_t> compiled_spirv(result.cbegin(), result.cend());

	spvtools::SpirvTools core(target == Target::Vulkan11 ? SPV_ENV_VULKAN_1_1 : SPV_ENV_VULKAN_1_0);

	core.SetMessageConsumer([&error_message](spv_message_level_t, const char *, const spv_position_t&, const char *message) {
		error_message = message;
	});

	spvtools::ValidatorOptions opts;
	opts.SetScalarBlockLayout(true);
	if (!core.Validate(compiled_spirv.data(), compiled_spirv.size(), opts))
	{
		LOGE("Failed to validate SPIR-V.\n");
		return {};
	}

	return compiled_spirv;
}
}
