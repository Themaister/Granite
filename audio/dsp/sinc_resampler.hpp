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

#include <vector>
#include <stdint.h>
#include <stddef.h>

namespace Granite
{
namespace Audio
{
namespace DSP
{

class SincResampler
{
public:
	enum class Quality
	{
		Low,
		Medium,
		High
	};
	SincResampler(float out_rate, float in_rate, Quality quality);
	~SincResampler();
	size_t process_and_accumulate(float *outputs, const float *inputs, size_t out_frames) noexcept;

	void operator=(const SincResampler &) = delete;
	SincResampler(const SincResampler &) = delete;

	size_t get_maximum_input_for_output_frames(size_t out_frames) const noexcept;
	size_t get_current_input_for_output_frames(size_t out_frames) const noexcept;

private:
	unsigned phase_bits = 0;
	unsigned subphase_bits = 0;
	unsigned subphase_mask = 0;
	unsigned taps = 0;
	unsigned ptr = 0;
	uint32_t time = 0;
	uint32_t fixed_ratio = 0;
	uint32_t phases = 0;
	float subphase_mod = 0.0f;

	float *main_buffer = nullptr;
	float *phase_table = nullptr;
	float *window_buffer = nullptr;

	void init_table_kaiser(double cutoff, unsigned phase_count, unsigned num_taps, double beta);
};

}
}
}
