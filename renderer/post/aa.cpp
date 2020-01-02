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

#include "aa.hpp"
#include "temporal.hpp"
#include "fxaa.hpp"
#include "smaa.hpp"
#include <string.h>

namespace Granite
{
bool setup_before_post_chain_antialiasing(PostAAType type, RenderGraph &graph, TemporalJitter &jitter,
                                          const std::string &input, const std::string &input_depth,
                                          const std::string &output)
{
	switch (type)
	{
	case PostAAType::TAA_Low:
		setup_taa_resolve(graph, jitter, input, input_depth, output, TAAQuality::Low);
		return true;

	case PostAAType::TAA_Medium:
		setup_taa_resolve(graph, jitter, input, input_depth, output, TAAQuality::Medium);
		return true;

	case PostAAType::TAA_High:
		setup_taa_resolve(graph, jitter, input, input_depth, output, TAAQuality::High);
		return true;

	case PostAAType::TAA_Ultra:
		setup_taa_resolve(graph, jitter, input, input_depth, output, TAAQuality::Ultra);
		return true;

	case PostAAType::TAA_Extreme:
		setup_taa_resolve(graph, jitter, input, input_depth, output, TAAQuality::Extreme);
		return true;

	case PostAAType::TAA_Nightmare:
		setup_taa_resolve(graph, jitter, input, input_depth, output, TAAQuality::Nightmare);
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

PostAAType string_to_post_antialiasing_type(const char *type)
{
	if (strcmp(type, "fxaa") == 0)
		return PostAAType::FXAA;
	else if (strcmp(type, "fxaa2phase") == 0)
		return PostAAType::FXAA_2Phase;
	else if (strcmp(type, "smaaLow") == 0)
		return PostAAType::SMAA_Low;
	else if (strcmp(type, "smaaMedium") == 0)
		return PostAAType::SMAA_Medium;
	else if (strcmp(type, "smaaHigh") == 0)
		return PostAAType::SMAA_High;
	else if (strcmp(type, "smaaUltra") == 0)
		return PostAAType::SMAA_Ultra;
	else if (strcmp(type, "smaaUltraT2X") == 0)
		return PostAAType::SMAA_Ultra_T2X;
	else if (strcmp(type, "taaLow") == 0)
		return PostAAType::TAA_Low;
	else if (strcmp(type, "taaMedium") == 0)
		return PostAAType::TAA_Medium;
	else if (strcmp(type, "taaHigh") == 0)
		return PostAAType::TAA_High;
	else if (strcmp(type, "taaUltra") == 0)
		return PostAAType::TAA_Ultra;
	else if (strcmp(type, "taaExtreme") == 0)
		return PostAAType::TAA_Extreme;
	else if (strcmp(type, "taaNightmare") == 0)
		return PostAAType::TAA_Nightmare;
	else if (strcmp(type, "none") == 0)
		return PostAAType::None;
	else
	{
		LOGE("Unrecognized AA type: %s\n", type);
		return PostAAType::None;
	}
}
}
