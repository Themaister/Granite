#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <stdint.h>
#include "filesystem.hpp"

namespace Granite
{
enum class Stage
{
	Vertex,
	TessControl,
	TessEvaluation,
	Geometry,
	Fragment,
	Compute
};

class GLSLCompiler
{
public:
	void set_stage(Stage stage)
	{
		this->stage = stage;
	}

	void set_source(std::string source, std::string path)
	{
		this->source = std::move(source);
		source_path = std::move(path);
	}

	void set_source_from_file(Filesystem &fs, const std::string &path);
	bool preprocess();

	void set_variant(const std::string &variant, int value);
	void reset_variants();
	std::vector<uint32_t> compile();

	const std::unordered_set<std::string> &get_dependencies() const
	{
		return dependencies;
	}

	struct ShaderVariant
	{
		int minimum;
		int maximum;
		int current;
	};

	const std::unordered_map<std::string, ShaderVariant> &get_variants() const
	{
		return variants;
	}

	const std::string &get_error_message() const
	{
		return error_message;
	}

private:
	std::string source;
	std::string source_path;
	Stage stage = Stage::Compute;
	Filesystem *fs = nullptr;

	std::unordered_set<std::string> dependencies;
	std::unordered_map<std::string, ShaderVariant> variants;
	std::string preprocessed_source;
	std::string error_message;

	static Stage stage_from_path(const std::string &path);
	bool parse_variants(const std::string &source, const std::string &path);
};
}
