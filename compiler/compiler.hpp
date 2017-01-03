#pragma once
#include <string>
#include <vector>
#include <unordered_map>
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

	void set_define(const std::string &define, const std::string &value = "")
	{
		defines[define] = value;
	}

	void clear_defines()
	{
		defines.clear();
	}

	bool compile(std::vector<uint32_t> &data);

private:
	std::string source;
	std::string source_path;
	Stage stage = Stage::Compute;
	std::unordered_map<std::string, std::string> defines;
	Filesystem *fs = nullptr;

	static Stage stage_from_path(const std::string &path);
};
}
