/* Copyright (c) 2015-2023 Hans-Kristian Arntzen
 *
 * Permission is hereby granted, free of charge,
 * to any person obtaining a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef FFT_COMMON_H_
#define FFT_COMMON_H_

#ifdef FFT_INPUT_TEXTURE
layout(set = 0, binding = 0) uniform sampler2D fft_input;
#else
layout(set = 0, binding = 0) readonly buffer FFTInput
{
	cfloat_data data[];
} fft_input;
#endif

#ifdef FFT_OUTPUT_TEXTURE
layout(set = 0, binding = 1) writeonly uniform image2D fft_output;
#else
layout(set = 0, binding = 1) writeonly buffer FFTOutput
{
	cfloat_data data[];
} fft_output;
#endif

layout(set = 0, binding = 2) readonly buffer Twiddles
{
	cfloat_data data[];
} twiddles;

layout(set = 0, binding = 3) uniform Constants
{
	uint element_stride; // N / RADIX_COMPOSITE
	uint input_row_stride;
	uint input_layer_stride;
	uint output_row_stride;
	uint output_layer_stride;
	uint p;
} constants;

#if defined(FFT_INPUT_TEXTURE) || defined(FFT_OUTPUT_TEXTURE)
layout(set = 0, binding = 4) uniform TextureConstants
{
    highp vec2 offset;
    highp vec2 scale;
    ivec2 store_offset;
} texture_constants;
#endif

layout(constant_id = 3) const float PI_DIR_MULT = -1.0;
const float PI = 3.14159265359;
const float SQRT_1_2 = 0.70710678118;
#define TWIDDLE_1_8 cfloat(SQRT_1_2, SQRT_1_2 * PI_DIR_MULT)
#define TWIDDLE_3_8 cfloat(-SQRT_1_2, SQRT_1_2 * PI_DIR_MULT)

const int DIMENSION_1D = 0;
const int DIMENSION_2D = 1;
const int DIMENSION_3D = 2;

layout(constant_id = 4) const uint RADIX_CTL = 0;

const int RADIX_FIRST_LOG2 = int(RADIX_CTL & 0xfu);
const int RADIX_SECOND_LOG2 = int((RADIX_CTL >> 4u) & 0xfu);
const int RADIX_THIRD_LOG2 = int((RADIX_CTL >> 8u) & 0xfu);
const int RADIX_FIRST = 1 << RADIX_FIRST_LOG2;
const int RADIX_SECOND = 1 << RADIX_SECOND_LOG2;
const int RADIX_THIRD = 1 << RADIX_THIRD_LOG2;
const int RADIX_COMPOSITE = RADIX_FIRST * RADIX_SECOND * RADIX_THIRD;
const int RADIX_DIMENSION = int((RADIX_CTL >> 12u) & 0xfu);
const bool P_FIRST = ((RADIX_CTL >> 16u) & 1u) != 0u;
const bool DISPATCH_2D = ((RADIX_CTL >> 17u) & 1u) != 0u;
const bool DISPATCH_3D = ((RADIX_CTL >> 18u) & 1u) != 0u;
const bool RADIX_R2C = ((RADIX_CTL >> 19u) & 1u) != 0u;
const bool RADIX_C2R = ((RADIX_CTL >> 20u) & 1u) != 0u;

cfloat twiddle(uint i, uint p)
{
	return decode(twiddles.data[i + p]);
}

cfloat cmul(cfloat a, cfloat b)
{
	cfloat r3 = a.yx;
	cfloat r1 = b.xx;
	cfloat R0 = a * r1;
	cfloat r2 = b.yy;
	cfloat R1 = r2 * r3;
	return R0 + cfloat(-R1.x, R1.y);
}

cfloat cmul_dir_j(cfloat v)
{
	return cfloat(-v.y, v.x) * cfloat(PI_DIR_MULT);
}

#endif