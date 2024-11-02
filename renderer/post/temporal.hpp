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
#include "render_graph.hpp"
#include "math.hpp"

namespace Granite
{
class RenderContext;
class TemporalJitter
{
public:
	enum class Type
	{
		FXAA_2Phase,
		SMAA_T2X,
		TAA_8Phase,
		TAA_16Phase,
		Custom,
		None
	};
	TemporalJitter();
	void reset();
	void init(Type type_, vec2 backbuffer_resolution);
	void init_custom(const vec2 *phases, unsigned phase_count, vec2 backbuffer_resolution);

	void step(const mat4 &projection, const mat4 &view);
	const mat4 &get_jitter_matrix() const;
	const mat4 &get_jittered_projection() const;
	const mat4 &get_history_view_proj(int frames) const;
	const mat4 &get_history_inv_view_proj(int frames) const;
	const mat4 &get_history_jittered_view_proj(int frames) const;
	const mat4 &get_history_jittered_inv_view_proj(int frames) const;
	unsigned get_jitter_phase() const;

	Type get_jitter_type() const
	{
		return type;
	}

private:
	unsigned phase = 0;
	unsigned jitter_count = 0;
	enum { MaxJitterPhases = 16 };

	void init_banks();
	std::vector<mat4> jitter_table;
	std::vector<mat4> saved_jittered_view_proj;
	std::vector<mat4> saved_jittered_inv_view_proj;
	std::vector<mat4> saved_view_proj;
	std::vector<mat4> saved_inv_view_proj;
	mat4 saved_jittered_projection;
	Type type = Type::None;

	unsigned get_offset_phase(int frames) const;
};

void setup_fxaa_2phase_postprocess(RenderGraph &graph, TemporalJitter &jitter, const std::string &input,
                                   const std::string &input_depth, const std::string &output);

enum class TAAQuality
{
	Low,
	Medium,
	High
};
void setup_taa_resolve(RenderGraph &graph, TemporalJitter &jitter, float scaling_factor,
                       const std::string &input, const std::string &input_depth, const std::string &input_mv,
                       const std::string &output, TAAQuality quality);

void setup_fsr2_pass(RenderGraph &graph, TemporalJitter &jitter,
                     const RenderContext &context,
                     float scaling_factor,
                     const std::string &input, const std::string &input_depth, const std::string &input_mv,
                     const std::string &output);
}
