#include "compiler.hpp"
#include <shaderc/shaderc.hpp>
#include "util.hpp"

using namespace std;

namespace Compiler
{

void GLSLCompiler::set_source_from_file(const string &path)
{
	source = Util::read_file_to_string(path);
	source_path = path;
}

bool GLSLCompiler::compile(vector<uint32_t> &blob)
{
	shaderc::Compiler compiler;
	shaderc::CompileOptions options;

	for (auto &def : defines)
	{
		if (def.second.empty())
			options.AddMacroDefinition(def.first);
		else
			options.AddMacroDefinition(def.first, def.second);
	}

	options.SetGenerateDebugInfo();
	options.SetOptimizationLevel(shaderc_optimization_level_zero);
	options.SetTargetEnvironment(shaderc_target_env_vulkan, 1);
	options.SetSourceLanguage(shaderc_source_language_glsl);

#if 0
	class Foo : public shaderc::CompileOptions::IncluderInterface
	{
	public:
		// Handles shaderc_include_resolver_fn callbacks.
		shaderc_include_result* GetInclude(const char* requested_source,
		                                   shaderc_include_type type,
		                                   const char* requesting_source,
		                                   size_t include_depth)
		{
			return nullptr;
		}

		// Handles shaderc_include_result_release_fn callbacks.
		void ReleaseInclude(shaderc_include_result* data)
		{
			(void)data;
		}
	};
	options.SetIncluder(unique_ptr<Foo>(new Foo));
#endif

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
	}
	shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(source, kind, source_path.c_str(), options);

	if (result.GetNumErrors())
	{
		LOG("GLSL error: \n%s\n", result.GetErrorMessage().c_str());
		return false;
	}

	blob.clear();
	blob.insert(begin(blob), result.cbegin(), result.cend());

	return true;
}
}