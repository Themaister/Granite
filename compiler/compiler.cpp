/* Copyright (c) 2017-2024 Hans-Kristian Arntzen
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
#include "shaderc/shaderc.hpp"
#include "path_utils.hpp"
#include "logging.hpp"
#include "string_helpers.hpp"

#include "spirv-tools/libspirv.hpp"

namespace Granite
{
GLSLCompiler::GLSLCompiler(FilesystemInterface &iface_)
	: iface(iface_)
{
}

static Stage stage_from_path(const std::string &path)
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
	else if (ext == "task")
		return Stage::Task;
	else if (ext == "mesh")
		return Stage::Mesh;
	else
		return Stage::Unknown;
}

static Stage convert_stage(const std::string &stage)
{
	if (stage == "vertex")
		return Stage::Vertex;
	else if (stage == "tess_control")
		return Stage::TessControl;
	else if (stage == "tess_evaluation")
		return Stage::TessEvaluation;
	else if (stage == "geometry")
		return Stage::Geometry;
	else if (stage == "compute")
		return Stage::Compute;
	else if (stage == "fragment")
		return Stage::Fragment;
	else if (stage == "task")
		return Stage::Task;
	else if (stage == "mesh")
		return Stage::Mesh;
	else
		return Stage::Unknown;
}

bool GLSLCompiler::set_source_from_file(const std::string &path, Stage forced_stage)
{
	if (!iface.load_text_file(path, source))
	{
		LOGE("Failed to load shader: %s\n", path.c_str());
		return false;
	}

	source_path = path;

	if (forced_stage != Stage::Unknown)
		stage = forced_stage;
	else
		stage = stage_from_path(path);

	return stage != Stage::Unknown;
}

bool GLSLCompiler::set_source_from_file_multistage(const std::string &path)
{
	if (iface.load_text_file(path, source))
	{
		LOGE("Failed to load shader: %s\n", path.c_str());
		return false;
	}

	source_path = path;
	stage = Stage::Unknown;
	return true;
}

void GLSLCompiler::set_include_directories(const std::vector<std::string> *include_directories_)
{
	include_directories = include_directories_;
}

bool GLSLCompiler::find_include_path(const std::string &source_path_, const std::string &include_path,
                                     std::string &included_path, std::string &included_source)
{
	auto relpath = Path::relpath(source_path_, include_path);
	if (iface.load_text_file(relpath, included_source))
	{
		included_path = relpath;
		return true;
	}

	if (include_directories)
	{
		for (auto &include_dir : *include_directories)
		{
			auto path = Path::join(include_dir, include_path);
			if (iface.load_text_file(path, included_source))
			{
				included_path = path;
				return true;
			}
		}
	}

	return false;
}

bool GLSLCompiler::parse_variants(const std::string &source_, const std::string &path)
{
	auto lines = Util::split(source_, "\n");

	unsigned line_index = 1;
	size_t offset;

	for (auto &line : lines)
	{
		// This check, followed by the include statement check below isn't even remotely correct,
		// but we only have to care about shaders we control here.
		if ((offset = line.find("//")) != std::string::npos)
			line = line.substr(0, offset);

		if ((offset = line.find("#include \"")) != std::string::npos)
		{
			auto include_path = line.substr(offset + 10);
			if (!include_path.empty() && include_path.back() == '"')
				include_path.pop_back();

			std::string included_source;
			if (!find_include_path(path, include_path, include_path, included_source))
			{
				LOGE("Failed to include GLSL file: %s\n", include_path.c_str());
				return false;
			}

			preprocessed_source += Util::join("#line ", 1, " \"", include_path, "\"\n");
			if (!parse_variants(included_source, include_path))
				return false;
			preprocessed_source += Util::join("#line ", line_index + 1, " \"", path, "\"\n");

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
		else if (line.find("#pragma stage ") == 0)
		{
			if (!preprocessed_source.empty())
			{
				preprocessed_sections.push_back({ preprocessing_active_stage, std::move(preprocessed_source) });
				preprocessed_source = {};
			}
			preprocessing_active_stage = convert_stage(line.substr(14));
			preprocessed_source += Util::join("#line ", line_index + 1, " \"", path, "\"\n");
		}
		else if (line.find("#pragma ") == 0)
		{
			pragmas.push_back(line.substr(8));
			preprocessed_source += "// ";
			preprocessed_source += line;
			preprocessed_source += '\n';
		}
		else
		{
			preprocessed_source += line;
			preprocessed_source += '\n';

			auto first_non_space = line.find_first_not_of(' ');
			if (first_non_space != std::string::npos && line[first_non_space] == '#')
			{
				auto keywords = Util::split(line.substr(first_non_space + 1), " ");
				if (keywords.size() == 1)
				{
					auto &word = keywords.front();
					if (word == "endif")
						preprocessed_source += Util::join("#line ", line_index + 1, " \"", path, "\"\n");
				}
			}
		}

		line_index++;
	}
	return true;
}

bool GLSLCompiler::preprocess()
{
	// Use a custom preprocessor where we only resolve includes once.
	// The builtin glslang preprocessor is not suitable for this task,
	// since we need to defer resolving defines.

	preprocessed_source.clear();
	pragmas.clear();
	preprocessed_sections.clear();
	preprocessing_active_stage = Stage::Unknown;

	bool ret = parse_variants(source, source_path);

	if (ret && !preprocessed_source.empty())
	{
		preprocessed_sections.push_back({ preprocessing_active_stage, std::move(preprocessed_source) });
		preprocessed_source = {};
	}

	return ret;
}

Util::Hash GLSLCompiler::get_source_hash() const
{
	Util::Hasher h;
	for (auto &section : preprocessed_sections)
	{
		h.u32(uint32_t(section.stage));
		h.string(section.source);
	}
	h.string(preprocessed_source);
	return h.get();
}

std::vector<uint32_t> GLSLCompiler::compile(std::string &error_message, const std::vector<std::pair<std::string, int>> *defines) const
{
	shaderc::Compiler compiler;
	shaderc::CompileOptions options;
	if (preprocessed_sections.empty())
	{
		error_message = "Need to preprocess source first.";
		return {};
	}

	if (defines)
		for (auto &define : *defines)
			options.AddMacroDefinition(define.first, std::to_string(define.second));

#if GRANITE_COMPILER_OPTIMIZE
	if (optimization != Optimization::ForceOff)
		options.SetOptimizationLevel(shaderc_optimization_level_performance);
	else
		options.SetOptimizationLevel(shaderc_optimization_level_zero);
#else
	options.SetOptimizationLevel(shaderc_optimization_level_zero);
#endif

	if (!strip && optimization == Optimization::ForceOff)
		options.SetGenerateDebugInfo();

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

	case Stage::Task:
		kind = shaderc_glsl_task_shader;
		break;

	case Stage::Mesh:
		kind = shaderc_glsl_mesh_shader;
		break;

	default:
		error_message = "Invalid shader stage.";
		return {};
	}

	options.SetTargetEnvironment(shaderc_target_env_vulkan,
	                             target == Target::Vulkan13 ?
	                             shaderc_env_version_vulkan_1_3 : shaderc_env_version_vulkan_1_1);
	options.SetTargetSpirv(target == Target::Vulkan13 ? shaderc_spirv_version_1_6 : shaderc_spirv_version_1_3);
	options.SetSourceLanguage(shaderc_source_language_glsl);

	shaderc::SpvCompilationResult result;

	if (preprocessed_sections.size() == 1)
	{
		if (preprocessed_sections.front().stage != Stage::Unknown && preprocessed_sections.front().stage != stage)
		{
			error_message = "No preprocessed sections available.";
			return {};
		}
		result = compiler.CompileGlslToSpv(preprocessed_sections.front().source, kind, source_path.c_str(), options);
	}
	else
	{
		std::string combined_source;
		for (auto &section : preprocessed_sections)
			if (section.stage == Stage::Unknown || section.stage == stage)
				combined_source += section.source;
		if (combined_source.empty())
		{
			error_message = "No preprocessed sections available.";
			return {};
		}
		result = compiler.CompileGlslToSpv(combined_source, kind, source_path.c_str(), options);
	}

	error_message.clear();
	if (result.GetCompilationStatus() != shaderc_compilation_status_success)
	{
		error_message = result.GetErrorMessage();
		return {};
	}

	std::vector<uint32_t> compiled_spirv(result.cbegin(), result.cend());

#if 0
	spvtools::SpirvTools core(target == Target::Vulkan13 ? SPV_ENV_VULKAN_1_3 : SPV_ENV_VULKAN_1_1);
	core.SetMessageConsumer([&error_message](spv_message_level_t, const char *, const spv_position_t&, const char *message) {
		error_message = message;
	});

	spvtools::ValidatorOptions opts;
	opts.SetScalarBlockLayout(true);
	if (!core.Validate(compiled_spirv.data(), compiled_spirv.size(), opts))
	{
		error_message += "\nFailed to validate SPIR-V.\n";
		return {};
	}
#endif

	return compiled_spirv;
}
}
