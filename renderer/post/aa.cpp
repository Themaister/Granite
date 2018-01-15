/* Copyright (c) 2017 Hans-Kristian Arntzen
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

#include "aa.hpp"
#include "temporal.hpp"
#include "fxaa.hpp"
#include "smaa.hpp"

namespace Granite
{
bool setup_before_post_chain_antialiasing(PostAAType type, RenderGraph &graph, TemporalJitter &jitter,
                                          const std::string &input, const std::string &input_depth,
                                          const std::string &output)
{
	switch (type)
	{
	case PostAAType::TAA:
		setup_taa_resolve(graph, jitter, input, input_depth, output);
		return true;

	default:
		return false;
	}
}

bool setup_after_post_chain_antialiasing(PostAAType type, RenderGraph &graph, TemporalJitter &jitter,
                                         const std::string &input, const std::string &input_depth,
                                         const std::string &output)
{
	switch (type)
	{
	case PostAAType::None:
		jitter.init(TemporalJitter::Type::None, vec2(0.0f));
		return false;

	case PostAAType::FXAA:
		setup_fxaa_postprocess(graph, input, output);
		return true;

	case PostAAType::FXAA_2Phase:
		setup_fxaa_2phase_postprocess(graph, jitter, input, input_depth, output);
		return true;

	case PostAAType::SMAA_Low:
		setup_smaa_postprocess(graph, jitter, input, input_depth, output, SMAAPreset::Low);
		return true;

	case PostAAType::SMAA_Medium:
		setup_smaa_postprocess(graph, jitter, input, input_depth, output, SMAAPreset::Medium);
		return true;

	case PostAAType::SMAA_High:
		setup_smaa_postprocess(graph, jitter, input, input_depth, output, SMAAPreset::High);
		return true;

	case PostAAType::SMAA_Ultra:
		setup_smaa_postprocess(graph, jitter, input, input_depth, output, SMAAPreset::Ultra);
		return true;

	case PostAAType::SMAA_Ultra_T2X:
		setup_smaa_postprocess(graph, jitter, input, input_depth, output, SMAAPreset::Ultra_T2X);
		return true;

	default:
		return false;
	}
}
}