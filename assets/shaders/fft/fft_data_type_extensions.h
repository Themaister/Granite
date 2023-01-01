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

#ifndef FFT_DATA_TYPE_EXTENSIONS_H_
#define FFT_DATA_TYPE_EXTENSIONS_H_

#if defined(FFT_FULL_FP16)
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require
#define cfloat f16vec2
#define cfloat_scalar float16_t
#define cfloat_data f16vec2
#define decode(x) x
#define encode(x) x
precision mediump sampler2D;
precision mediump image2D;
#elif defined(FFT_DATA_FP16)
#define cfloat vec2
#define cfloat_scalar float
#define cfloat_data uint
#define decode(x) unpackHalf2x16(x)
#define encode(x) packHalf2x16(x)
precision mediump float;
precision mediump sampler2D;
precision mediump image2D;
#else
#define cfloat vec2
#define cfloat_scalar float
#define cfloat_data vec2
#define decode(x) x
#define encode(x) x
#endif

#endif
