#include "compiler.hpp"
#include <shaderc/shaderc.hpp>
#include <path.hpp>
#include "util.hpp"

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
		throw logic_error("invalid extension");
}

void GLSLCompiler::set_source_from_file(Filesystem &fs, const string &path)
{
	auto file = fs.open(path);
	if (!file)
		throw runtime_error("file open");
	auto *mapped = static_cast<const char *>(file->map());
	if (!mapped)
		throw runtime_error("file map");
	source = string(mapped, mapped + file->get_size());

	source_path = path;
	stage = stage_from_path(path);
	this->fs = &fs;
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

	class Foo : public shaderc::CompileOptions::IncluderInterface
	{
	public:
		Foo(Filesystem &fs)
			: fs(fs)
		{

		}

		struct Holder
		{
			string path;
			unique_ptr<File> file;
		};

		// Handles shaderc_include_resolver_fn callbacks.
		shaderc_include_result* GetInclude(const char* requested_source,
		                                   shaderc_include_type,
		                                   const char* requesting_source,
		                                   size_t)
		{
			if (!requested_source || !requesting_source)
				return nullptr;
			auto path = Path::relpath(requesting_source, requested_source);
			auto file = fs.open(path);
			if (!file)
				return nullptr;

			auto *result = new shaderc_include_result();
			auto *holder = new Holder{};
			holder->file = move(file);
			holder->path = move(path);
			result->source_name = holder->path.c_str();
			result->source_name_length = holder->path.size();
			result->content = static_cast<const char *>(holder->file->map());
			result->content_length = holder->file->get_size();
			result->user_data = holder;

			if (!result->content)
			{
				delete holder;
				delete result;
				return nullptr;
			}
			return result;
		}

		void ReleaseInclude(shaderc_include_result* data)
		{
			auto *holder = static_cast<Holder *>(data->user_data);
			delete holder;
			delete data;
		}

	private:
		Filesystem &fs;
	};
	options.SetIncluder(unique_ptr<Foo>(new Foo(*fs)));

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