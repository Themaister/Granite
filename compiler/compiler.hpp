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

#pragma once

#include <string>
#include <vector>
#include <unordered_set>
#include <stdint.h>
#include "small_vector.hpp"
#include "global_managers.hpp"
#include "hash.hpp"

namespace Granite
{
enum class Stage
{
	Vertex,
	TessControl,
	TessEvaluation,
	Geometry,
	Fragment,
	Compute,
	Task,
	Mesh,
	Unknown
};

enum class Target
{
	Vulkan11,
	Vulkan13
};

class GLSLCompiler
{
public:
	explicit GLSLCompiler(FilesystemInterface &iface);

	void set_target(Target target_)
	{
		target = target_;
	}

	void set_stage(Stage stage_)
	{
		stage = stage_;
	}

	void set_source(std::string source_, std::string path)
	{
		source = std::move(source_);
		source_path = std::move(path);
	}

	void set_include_directories(const std::vector<std::string> *include_directories);

	bool set_source_from_file(const std::string &path, Stage stage = Stage::Unknown);
	bool set_source_from_file_multistage(const std::string &path);
	bool preprocess();
	Util::Hash get_source_hash() const;

	std::vector<uint32_t> compile(std::string &error_message, const std::vector<std::pair<std::string, int>> *defines = nullptr) const;

	const std::unordered_set<std::string> &get_dependencies() const
	{
		return dependencies;
	}

	enum class Optimization
	{
		ForceOff,
		ForceOn,
		Default
	};

	void set_optimization(Optimization opt)
	{
		optimization = opt;
	}

	void set_strip(bool strip_)
	{
		strip = strip_;
	}

	const std::vector<std::string> &get_user_pragmas() const
	{
		return pragmas;
	}

private:
	FilesystemInterface &iface;
	std::string source;
	std::string source_path;
	const std::vector<std::string> *include_directories = nullptr;
	Stage stage = Stage::Unknown;

	std::unordered_set<std::string> dependencies;
	struct Section
	{
		Stage stage;
		std::string source;
	};
	Util::SmallVector<Section> preprocessed_sections;
	std::string preprocessed_source;
	Stage preprocessing_active_stage = Stage::Unknown;

	std::vector<std::pair<size_t, size_t>> preprocessed_lines;
	std::vector<std::string> pragmas;

	Target target = Target::Vulkan11;

	bool parse_variants(const std::string &source, const std::string &path);

	Optimization optimization = Optimization::Default;
	bool strip = false;

	bool find_include_path(const std::string &source_path, const std::string &include_path,
	                       std::string &included_path, std::string &included_source);
};
}
