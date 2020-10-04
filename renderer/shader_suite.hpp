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

#pragma once

#include "shader_manager.hpp"
#include "intrusive_hash_map.hpp"
#include "mesh.hpp"

namespace Granite
{
class ShaderSuite
{
public:
	void init_graphics(Vulkan::ShaderManager *manager, const std::string &vertex, const std::string &fragment);
	void init_compute(Vulkan::ShaderManager *manager, const std::string &compute);
	Vulkan::Program *get_program(DrawPipeline pipeline, uint32_t attribute_mask, uint32_t texture_mask, uint32_t variant_id = 0);

	std::vector<std::pair<std::string, int>> &get_base_defines()
	{
		return base_defines;
	}

	void bake_base_defines();
	void promote_read_write_cache_to_read_only();

private:
	Util::Hash base_define_hash = 0;
	Vulkan::ShaderManager *manager = nullptr;
	Vulkan::ShaderProgram *program = nullptr;
	Util::ThreadSafeIntrusiveHashMapReadCached<Util::IntrusivePODWrapper<Vulkan::ShaderProgramVariant *>> variants;
	std::vector<std::pair<std::string, int>> base_defines;
};
}