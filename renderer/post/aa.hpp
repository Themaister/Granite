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

#include <string>

namespace Granite
{
class RenderGraph;
class TemporalJitter;
enum class PostAAType
{
	FXAA,
	FXAA_2Phase,
	SMAA_Low,
	SMAA_Medium,
	SMAA_High,
	SMAA_Ultra,
	SMAA_Ultra_T2X,
	TAA_Low,
	TAA_Medium,
	TAA_High,
	TAA_Ultra,
	TAA_Extreme,
	TAA_Nightmare,
	None
};

bool setup_before_post_chain_antialiasing(PostAAType type, RenderGraph &graph, TemporalJitter &jitter,
                                          const std::string &input, const std::string &input_depth,
                                          const std::string &output);
bool setup_after_post_chain_antialiasing(PostAAType type, RenderGraph &graph, TemporalJitter &jitter,
                                         const std::string &input, const std::string &input_depth,
                                         const std::string &output);

PostAAType string_to_post_antialiasing_type(const char *type);
}