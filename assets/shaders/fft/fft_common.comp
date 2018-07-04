/* Copyright (C) 2015 Hans-Kristian Arntzen <maister@archlinux.us>
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

#if defined(FFT_FP16) && defined(GL_ES)
precision mediump float;
#endif

#define BINDING_SSBO_IN 0
#define BINDING_SSBO_OUT 1
#define BINDING_SSBO_AUX 2
#define BINDING_UBO 3
#define BINDING_TEXTURE0 4
#define BINDING_TEXTURE1 5
#define BINDING_IMAGE 6

layout(std140, binding = BINDING_UBO) uniform UBO
{
    uvec4 p_stride_padding;
    vec4 texture_offset_scale;
} constant_data;
#define uStride constant_data.p_stride_padding.y

// cfloat is the "generic" type used to hold complex data.
// GLFFT supports vec2, vec4 and "vec8" for its complex data
// to be able to work on 1, 2 and 4 complex values in a single vector.
// FFT_VEC2, FFT_VEC4, FFT_VEC8 defines which type we're using.
// The shaders are compiled on-demand.

// FP16 values are packed as 2xfp16 in a uint.
// packHalf2x16 and unpackHalf2x16 are used to bitcast between these formats.

// The complex number format is (real, imag, real, imag, ...) in an interleaved fashion.
// For complex-to-real or real-to-complex transforms, we consider two adjacent real samples to be a complex number as-is.
// Separate "resolve" passes are added to make the transform correct.

#if defined(FFT_VEC2)
#define cfloat vec2
#define cfloat_buffer_fp16 uint
#elif defined(FFT_VEC4)
#define cfloat vec4
#define cfloat_buffer_fp16 uvec2
#elif defined(FFT_VEC8)
#if !defined(FFT_INPUT_FP16) || !defined(FFT_OUTPUT_FP16) || !defined(FFT_FP16)
#error FFT_VEC8 must use FP16 everywhere.
#endif
#define cfloat uvec4
#define cfloat_buffer_fp16 uvec4
#else
#error FFT_VEC2, FFT_VEC4 or FFT_VEC8 must be defined.
#endif

#ifdef FFT_INPUT_FP16
#define cfloat_buffer_in cfloat_buffer_fp16
#else
#define cfloat_buffer_in cfloat
#endif

#ifdef FFT_OUTPUT_FP16
#define cfloat_buffer_out cfloat_buffer_fp16
#else
#define cfloat_buffer_out cfloat
#endif

// Normally this would be sqrt(1 / radix), but we'd have to apply normalization
// for every pass instead of just half of them. Also, 1 / 2^n is "lossless" in FP math.
#ifdef FFT_NORMALIZE
#define FFT_NORM_FACTOR (1.0 / float(FFT_RADIX))
#endif

// FFT_CVECTOR_SIZE defines an interleaving stride for the first pass.
// The first FFT pass with stockham autosort needs to do some shuffling around if we're processing
// more than one complex value per vector.
// This is only needed for horizontal transforms since we vectorize horizontally and different elements
// in the vector are from different transforms when we do vertical transforms.

#if defined(FFT_P1) && !defined(FFT_DUAL) && defined(FFT_HORIZ) && defined(FFT_VEC8)
#define FFT_CVECTOR_SIZE 4
#elif defined(FFT_P1) && ((!defined(FFT_DUAL) && defined(FFT_HORIZ) && defined(FFT_VEC4)) || (defined(FFT_DUAL) && defined(FFT_HORIZ) && defined(FFT_VEC8)))
#define FFT_CVECTOR_SIZE 2
#else
#define FFT_CVECTOR_SIZE 1
#endif

#ifdef GL_ES
#define FFT_HIGHP highp
#else
#define FFT_HIGHP
#endif

#ifdef FFT_VEC8

// Currently unlikely to be useful.
uvec4 PADD(uvec4 a, uvec4 b)
{
    return uvec4(
        packHalf2x16(unpackHalf2x16(a.x) + unpackHalf2x16(b.x)),
        packHalf2x16(unpackHalf2x16(a.y) + unpackHalf2x16(b.y)),
        packHalf2x16(unpackHalf2x16(a.z) + unpackHalf2x16(b.z)),
        packHalf2x16(unpackHalf2x16(a.w) + unpackHalf2x16(b.w)));
}

uvec4 PSUB(uvec4 a, uvec4 b)
{
    return uvec4(
        packHalf2x16(unpackHalf2x16(a.x) - unpackHalf2x16(b.x)),
        packHalf2x16(unpackHalf2x16(a.y) - unpackHalf2x16(b.y)),
        packHalf2x16(unpackHalf2x16(a.z) - unpackHalf2x16(b.z)),
        packHalf2x16(unpackHalf2x16(a.w) - unpackHalf2x16(b.w)));
}

uvec4 PMUL(uvec4 a, uvec4 b)
{
    return uvec4(
        packHalf2x16(unpackHalf2x16(a.x) * unpackHalf2x16(b.x)),
        packHalf2x16(unpackHalf2x16(a.y) * unpackHalf2x16(b.y)),
        packHalf2x16(unpackHalf2x16(a.z) * unpackHalf2x16(b.z)),
        packHalf2x16(unpackHalf2x16(a.w) * unpackHalf2x16(b.w)));
}

uvec4 CONJ_SWIZZLE(uvec4 v)
{
    return uvec4(
        packHalf2x16(unpackHalf2x16(v.x).yx),
        packHalf2x16(unpackHalf2x16(v.y).yx),
        packHalf2x16(unpackHalf2x16(v.z).yx),
        packHalf2x16(unpackHalf2x16(v.w).yx));
}

uvec4 LDUP_SWIZZLE(uvec4 v)
{
    return uvec4(
        packHalf2x16(unpackHalf2x16(v.x).xx),
        packHalf2x16(unpackHalf2x16(v.y).xx),
        packHalf2x16(unpackHalf2x16(v.z).xx),
        packHalf2x16(unpackHalf2x16(v.w).xx));
}

uvec4 HDUP_SWIZZLE(uvec4 v)
{
    return uvec4(
        packHalf2x16(unpackHalf2x16(v.x).yy),
        packHalf2x16(unpackHalf2x16(v.y).yy),
        packHalf2x16(unpackHalf2x16(v.z).yy),
        packHalf2x16(unpackHalf2x16(v.w).yy));
}

// Sign-flip. Works for the cases we're interested in.
uvec4 cmul_minus_j(uvec4 v)
{
    return uvec4(0x80000000u) ^ CONJ_SWIZZLE(v);
}

uvec4 cmul_plus_j(uvec4 v)
{
    return uvec4(0x00008000u) ^ CONJ_SWIZZLE(v);
}

uvec4 cmul(uvec4 a, uvec4 b)
{
    uvec4 r3 = CONJ_SWIZZLE(a);
    uvec4 r1 = LDUP_SWIZZLE(b);
    uvec4 R0 = PMUL(a, r1);
    uvec4 r2 = HDUP_SWIZZLE(b);
    uvec4 R1 = PMUL(r2, r3);
    return PADD(R0, uvec4(0x8000u) ^ R1);
}

void butterfly(inout uvec4 a, inout uvec4 b, uvec4 w)
{
    uvec4 t = cmul(b, w);
    b = PSUB(a, t);
    a = PADD(a, t);
}

void butterfly(inout uvec4 a, inout uvec4 b, vec4 w)
{
    uvec4 t = cmul(b, uvec2(packHalf2x16(w.xy), packHalf2x16(w.zw)).xxyy);
    b = PSUB(a, t);
    a = PADD(a, t);
}

void butterfly(inout uvec4 a, inout uvec4 b, vec2 w)
{
    uvec4 t = cmul(b, uvec4(packHalf2x16(w)));
    b = PSUB(a, t);
    a = PADD(a, t);
}

void butterfly_p1(inout uvec4 a, inout uvec4 b)
{
    uvec4 t = b;
    b = PSUB(a, t);
    a = PADD(a, t);
}

void butterfly_p1_minus_j(inout uvec4 a, inout uvec4 b)
{
    uvec4 t = b;
    b = uvec4(0x80000000u) ^ (PSUB(CONJ_SWIZZLE(a), CONJ_SWIZZLE(t)));
    a = PADD(a, t);
}

void butterfly_p1_plus_j(inout uvec4 a, inout uvec4 b)
{
    uvec4 t = b;
    b = uvec4(0x00008000u) ^ (PSUB(CONJ_SWIZZLE(a), CONJ_SWIZZLE(t)));
    a = PADD(a, t);
}
#endif

// Complex multiply.
vec4 cmul(vec4 a, vec4 b)
{
    vec4 r3 = a.yxwz;
    vec4 r1 = b.xxzz;
    vec4 R0 = a * r1;
    vec4 r2 = b.yyww;
    vec4 R1 = r2 * r3;
    return R0 + vec4(-R1.x, R1.y, -R1.z, R1.w);
}

vec2 cmul(vec2 a, vec2 b)
{
    vec2 r3 = a.yx;
    vec2 r1 = b.xx;
    vec2 R0 = a * r1;
    vec2 r2 = b.yy;
    vec2 R1 = r2 * r3;
    return R0 + vec2(-R1.x, R1.y);
}

#ifdef FFT_INPUT_TEXTURE

#ifndef FFT_P1
#error Input texture can only be used when P == 1.
#endif

#ifdef GL_ES
#if defined(FFT_INPUT_FP16) || defined(FFT_FP16)
precision mediump sampler2D;
#else
precision highp sampler2D;
#endif
#endif

#define uTexelOffset constant_data.texture_offset_scale.xy
#define uTexelScale constant_data.texture_offset_scale.zw

layout(binding = BINDING_TEXTURE0) uniform sampler2D uTexture;
#ifdef FFT_CONVOLVE
layout(binding = BINDING_TEXTURE1) uniform sampler2D uTexture2;
#endif

cfloat load_texture(sampler2D sampler, uvec2 coord)
{
    FFT_HIGHP vec2 uv = vec2(coord) * uTexelScale + uTexelOffset;

    // Quite messy, this :)
#if defined(FFT_VEC8)
    #if defined(FFT_INPUT_REAL)
    return uvec4(
        packHalf2x16(vec2(textureLodOffset(sampler, uv, 0.0, ivec2(0, 0)).x, textureLodOffset(sampler, uv, 0.0, ivec2(1, 0)).x)),
        packHalf2x16(vec2(textureLodOffset(sampler, uv, 0.0, ivec2(2, 0)).x, textureLodOffset(sampler, uv, 0.0, ivec2(3, 0)).x)),
        packHalf2x16(vec2(textureLodOffset(sampler, uv, 0.0, ivec2(4, 0)).x, textureLodOffset(sampler, uv, 0.0, ivec2(5, 0)).x)),
        packHalf2x16(vec2(textureLodOffset(sampler, uv, 0.0, ivec2(6, 0)).x, textureLodOffset(sampler, uv, 0.0, ivec2(7, 0)).x)));
    #elif defined(FFT_DUAL)
    vec4 c0 = textureLodOffset(sampler, uv, 0.0, ivec2(0, 0));
    vec4 c1 = textureLodOffset(sampler, uv, 0.0, ivec2(1, 0));
    return uvec4(packHalf2x16(c0.xy), packHalf2x16(c0.zw), packHalf2x16(c1.xy), packHalf2x16(c1.zw));
    #else
    return uvec4(
        packHalf2x16(textureLodOffset(sampler, uv, 0.0, ivec2(0, 0)).xy),
        packHalf2x16(textureLodOffset(sampler, uv, 0.0, ivec2(1, 0)).xy),
        packHalf2x16(textureLodOffset(sampler, uv, 0.0, ivec2(2, 0)).xy),
        packHalf2x16(textureLodOffset(sampler, uv, 0.0, ivec2(3, 0)).xy));
    #endif
#elif defined(FFT_VEC4)
    #if defined(FFT_INPUT_REAL)
    return vec4(
        textureLodOffset(sampler, uv, 0.0, ivec2(0, 0)).x,
        textureLodOffset(sampler, uv, 0.0, ivec2(1, 0)).x,
        textureLodOffset(sampler, uv, 0.0, ivec2(2, 0)).x,
        textureLodOffset(sampler, uv, 0.0, ivec2(3, 0)).x);
    #elif defined(FFT_DUAL)
    return textureLod(sampler, uv, 0.0);
    #else
    return vec4(
        textureLodOffset(sampler, uv, 0.0, ivec2(0, 0)).xy,
        textureLodOffset(sampler, uv, 0.0, ivec2(1, 0)).xy);
    #endif
#elif defined(FFT_VEC2)
    #if defined(FFT_INPUT_REAL)
    return vec2(
        textureLodOffset(sampler, uv, 0.0, ivec2(0, 0)).x,
        textureLodOffset(sampler, uv, 0.0, ivec2(1, 0)).x);
    #else
    return textureLod(sampler, uv, 0.0).xy;
    #endif
#endif
}

cfloat load_texture(uvec2 coord)
{
#ifdef FFT_CONVOLVE
    // Convolution in frequency domain is multiplication.
    cfloat c0 = load_texture(uTexture, coord);
    cfloat c1 = load_texture(uTexture2, coord);
    return cmul(c0, c1);
#else
    return load_texture(uTexture, coord);
#endif
}

// Implement a dummy load_global, or we have to #ifdef out lots of dead code elsewhere.
#ifdef FFT_VEC8
cfloat load_global(uint offset)
{
    return cfloat(0u);
}
#else
cfloat load_global(uint offset)
{
    return cfloat(0.0);
}
#endif

#else

layout(std430, binding = BINDING_SSBO_IN) readonly buffer Block
{
    cfloat_buffer_in data[];
} fft_in;

#ifdef FFT_CONVOLVE
layout(std430, binding = BINDING_SSBO_AUX) readonly buffer Block2
{
    cfloat_buffer_in data[];
} fft_in2;

cfloat load_global(uint offset)
{
    // Convolution in frequency domain is multiplication.
#if defined(FFT_INPUT_FP16) && defined(FFT_VEC2)
    return cmul(unpackHalf2x16(fft_in.data[offset]), unpackHalf2x16(fft_in2.data[offset]));
#elif defined(FFT_INPUT_FP16) && defined(FFT_VEC4)
    uvec2 data = fft_in.data[offset];
    uvec2 data2 = fft_in2.data[offset];
    return cmul(vec4(unpackHalf2x16(data.x), unpackHalf2x16(data.y)), vec4(unpackHalf2x16(data2.x), unpackHalf2x16(data2.y)));
#else
    return cmul(fft_in.data[offset], fft_in2.data[offset]);
#endif
}
#else
cfloat load_global(uint offset)
{
#if defined(FFT_INPUT_FP16) && defined(FFT_VEC2)
    return unpackHalf2x16(fft_in.data[offset]);
#elif defined(FFT_INPUT_FP16) && defined(FFT_VEC4)
    uvec2 data = fft_in.data[offset];
    return vec4(unpackHalf2x16(data.x), unpackHalf2x16(data.y));
#else
    return fft_in.data[offset];
#endif
}
#endif
#endif

#ifndef FFT_OUTPUT_IMAGE
layout(std430, binding = BINDING_SSBO_OUT) writeonly buffer BlockOut
{
    cfloat_buffer_out data[];
} fft_out;

void store_global(uint offset, cfloat v)
{
#ifdef FFT_NORM_FACTOR
#ifdef FFT_VEC8
    v = PMUL(uvec4(packHalf2x16(vec2(FFT_NORM_FACTOR))), v);
#else
    v *= FFT_NORM_FACTOR;
#endif
#endif

#if defined(FFT_OUTPUT_FP16) && defined(FFT_VEC2)
    fft_out.data[offset] = packHalf2x16(v);
#elif defined(FFT_OUTPUT_FP16) && defined(FFT_VEC4)
    fft_out.data[offset] = uvec2(packHalf2x16(v.xy), packHalf2x16(v.zw));
#else
    fft_out.data[offset] = v;
#endif
}
#endif

#ifdef FFT_OUTPUT_IMAGE

#ifdef GL_ES
#ifdef FFT_OUTPUT_REAL
precision highp image2D;
#else
precision mediump image2D;
#endif
precision highp uimage2D;
#endif

//#ifdef FFT_P1
//#error FFT_OUTPUT_IMAGE is not supported in first pass.
//#endif

// Currently, GLFFT only supports outputing to "fixed" formats like these.
// Should be possible to add options for this to at least choose between FP16/FP32 output,
// and maybe rgba8_unorm for FFT_DUAL case.
#if defined(FFT_DUAL)
layout(rgba16f, binding = BINDING_IMAGE) uniform writeonly image2D uImage;
#elif defined(FFT_OUTPUT_REAL)
layout(r32f, binding = BINDING_IMAGE) uniform writeonly image2D uImage;
#else
// GLES 3.1 doesn't support rg16f layout for some reason, so work around it ...
layout(r32ui, binding = BINDING_IMAGE) uniform writeonly uimage2D uImage;
#endif

void store(ivec2 coord, vec4 value)
{
#ifdef FFT_NORM_FACTOR
    value *= FFT_NORM_FACTOR;
#endif

#if defined(FFT_DUAL)
    imageStore(uImage, coord, value);
#elif defined(FFT_HORIZ)
#ifdef FFT_OUTPUT_REAL
    imageStore(uImage, coord * ivec2(2, 1) + ivec2(0, 0), value.xxxx);
    imageStore(uImage, coord * ivec2(2, 1) + ivec2(1, 0), value.yyyy);
    imageStore(uImage, coord * ivec2(2, 1) + ivec2(2, 0), value.zzzz);
    imageStore(uImage, coord * ivec2(2, 1) + ivec2(3, 0), value.wwww);
#else
    imageStore(uImage, coord + ivec2(0, 0), uvec4(packHalf2x16(value.xy)));
    imageStore(uImage, coord + ivec2(1, 0), uvec4(packHalf2x16(value.zw)));
#endif
#elif defined(FFT_VERT)
#ifdef FFT_OUTPUT_REAL
    imageStore(uImage, coord * ivec2(4, 1) + ivec2(0, 0), value.xxxx);
    imageStore(uImage, coord * ivec2(4, 1) + ivec2(1, 0), value.yyyy);
    imageStore(uImage, coord * ivec2(4, 1) + ivec2(2, 0), value.zzzz);
    imageStore(uImage, coord * ivec2(4, 1) + ivec2(3, 0), value.wwww);
#else
    imageStore(uImage, coord * ivec2(2, 1) + ivec2(0, 0), uvec4(packHalf2x16(value.xy)));
    imageStore(uImage, coord * ivec2(2, 1) + ivec2(1, 0), uvec4(packHalf2x16(value.zw)));
#endif
#else
#error Inconsistent defines.
#endif
}

#ifndef FFT_DUAL
void store(ivec2 coord, vec2 value)
{
#ifdef FFT_NORM_FACTOR
    value *= FFT_NORM_FACTOR;
#endif

#if defined(FFT_HORIZ)
#ifdef FFT_OUTPUT_REAL
    imageStore(uImage, coord * ivec2(2, 1) + ivec2(0, 0), value.xxxx);
    imageStore(uImage, coord * ivec2(2, 1) + ivec2(1, 0), value.yyyy);
#else
    imageStore(uImage, coord, uvec4(packHalf2x16(value.xy)));
#endif
#elif defined(FFT_VERT)
#ifdef FFT_OUTPUT_REAL
    imageStore(uImage, coord * ivec2(2, 1) + ivec2(0, 0), value.xxxx);
    imageStore(uImage, coord * ivec2(2, 1) + ivec2(1, 0), value.yyyy);
#else
    imageStore(uImage, coord, uvec4(packHalf2x16(value.xy)));
#endif
#else
#error Inconsistent defines.
#endif
}
#endif

#ifdef FFT_VEC8
void store(ivec2 coord, uvec4 value)
{
#ifdef FFT_NORM_FACTOR
    value = PMUL(value, uvec4(packHalf2x16(vec2(FFT_NORM_FACTOR))));
#endif

#if defined(FFT_DUAL)
#if defined(FFT_HORIZ)
    imageStore(uImage, coord + ivec2(0, 0), vec4(unpackHalf2x16(value.x), unpackHalf2x16(value.y)));
    imageStore(uImage, coord + ivec2(1, 0), vec4(unpackHalf2x16(value.z), unpackHalf2x16(value.w)));
#else
    imageStore(uImage, coord * ivec2(2, 1) + ivec2(0, 0), vec4(unpackHalf2x16(value.x), unpackHalf2x16(value.y)));
    imageStore(uImage, coord * ivec2(2, 1) + ivec2(1, 0), vec4(unpackHalf2x16(value.z), unpackHalf2x16(value.w)));
#endif
#elif defined(FFT_HORIZ)
#ifdef FFT_OUTPUT_REAL
    vec2 value0 = unpackHalf2x16(value.x);
    vec2 value1 = unpackHalf2x16(value.y);
    vec2 value2 = unpackHalf2x16(value.z);
    vec2 value3 = unpackHalf2x16(value.w);
    imageStore(uImage, coord * ivec2(2, 1) + ivec2(0, 0), value0.xxxx);
    imageStore(uImage, coord * ivec2(2, 1) + ivec2(1, 0), value0.yyyy);
    imageStore(uImage, coord * ivec2(2, 1) + ivec2(2, 0), value1.xxxx);
    imageStore(uImage, coord * ivec2(2, 1) + ivec2(3, 0), value1.yyyy);
    imageStore(uImage, coord * ivec2(2, 1) + ivec2(4, 0), value2.xxxx);
    imageStore(uImage, coord * ivec2(2, 1) + ivec2(5, 0), value2.yyyy);
    imageStore(uImage, coord * ivec2(2, 1) + ivec2(6, 0), value3.xxxx);
    imageStore(uImage, coord * ivec2(2, 1) + ivec2(7, 0), value3.yyyy);
#else
    imageStore(uImage, coord + ivec2(0, 0), value.xxxx);
    imageStore(uImage, coord + ivec2(1, 0), value.yyyy);
    imageStore(uImage, coord + ivec2(2, 0), value.zzzz);
    imageStore(uImage, coord + ivec2(3, 0), value.wwww);
#endif
#elif defined(FFT_VERT)
#ifdef FFT_OUTPUT_REAL
    vec2 value0 = unpackHalf2x16(value.x);
    vec2 value1 = unpackHalf2x16(value.y);
    vec2 value2 = unpackHalf2x16(value.z);
    vec2 value3 = unpackHalf2x16(value.w);
    imageStore(uImage, coord * ivec2(8, 1) + ivec2(0, 0), value0.xxxx);
    imageStore(uImage, coord * ivec2(8, 1) + ivec2(1, 0), value0.yyyy);
    imageStore(uImage, coord * ivec2(8, 1) + ivec2(2, 0), value1.xxxx);
    imageStore(uImage, coord * ivec2(8, 1) + ivec2(3, 0), value1.yyyy);
    imageStore(uImage, coord * ivec2(8, 1) + ivec2(4, 0), value2.xxxx);
    imageStore(uImage, coord * ivec2(8, 1) + ivec2(5, 0), value2.yyyy);
    imageStore(uImage, coord * ivec2(8, 1) + ivec2(6, 0), value3.xxxx);
    imageStore(uImage, coord * ivec2(8, 1) + ivec2(7, 0), value3.yyyy);
#else
    imageStore(uImage, coord * ivec2(4, 1) + ivec2(0, 0), value.xxxx);
    imageStore(uImage, coord * ivec2(4, 1) + ivec2(1, 0), value.yyyy);
    imageStore(uImage, coord * ivec2(4, 1) + ivec2(2, 0), value.zzzz);
    imageStore(uImage, coord * ivec2(4, 1) + ivec2(3, 0), value.wwww);
#endif
#else
#error Inconsistent defines.
#endif
}
#endif

#endif

#define PI 3.14159265359
#define SQRT_1_2 0.70710678118

#ifdef FFT_INVERSE
#define PI_DIR (+PI)
#else
#define PI_DIR (-PI)
#endif

// Some GLES implementations have lower trancendental precision than desired which
// significantly affects the overall FFT precision.
// For these implementations it might make sense to add a LUT UBO with twiddle factors,
// which can be used here.

// 4-component FP16 twiddles, pack in uvec4.
#if !defined(FFT_DUAL) && defined(FFT_HORIZ) && defined(FFT_VEC8)
#define FFT_OUTPUT_STEP 4u
#define FFT_OUTPUT_SHIFT 2u
#define ctwiddle uvec4
ctwiddle twiddle(uint k, uint p)
{
    // Trancendentals should always be done in highp.
    FFT_HIGHP vec4 angles = PI_DIR * (float(k) + vec4(0.0, 1.0, 2.0, 3.0)) / float(p);
    FFT_HIGHP vec4 cos_a = cos(angles);
    FFT_HIGHP vec4 sin_a = sin(angles);
    return ctwiddle(
            packHalf2x16(vec2(cos_a.x, sin_a.x)),
            packHalf2x16(vec2(cos_a.y, sin_a.y)),
            packHalf2x16(vec2(cos_a.z, sin_a.z)),
            packHalf2x16(vec2(cos_a.w, sin_a.w)));
}

#ifdef FFT_INVERSE
#define TWIDDLE_1_8 (uvec4(packHalf2x16(vec2(+SQRT_1_2, +SQRT_1_2))))
#define TWIDDLE_3_8 (uvec4(packHalf2x16(vec2(-SQRT_1_2, +SQRT_1_2))))
#else
#define TWIDDLE_1_8 (uvec4(packHalf2x16(vec2(+SQRT_1_2, -SQRT_1_2))))
#define TWIDDLE_3_8 (uvec4(packHalf2x16(vec2(-SQRT_1_2, -SQRT_1_2))))
#endif

// 2-component twiddles, pack in vec4.
#elif (!defined(FFT_DUAL) && defined(FFT_HORIZ) && defined(FFT_VEC4)) || (defined(FFT_DUAL) && defined(FFT_HORIZ) && defined(FFT_VEC8))
#define FFT_OUTPUT_STEP 2u
#define FFT_OUTPUT_SHIFT 1u
#define ctwiddle vec4
ctwiddle twiddle(uint k, uint p)
{
    // Trancendentals should always be done in highp.
    FFT_HIGHP vec2 angles = PI_DIR * (float(k) + vec2(0.0, 1.0)) / float(p);
    FFT_HIGHP vec2 cos_a = cos(angles);
    FFT_HIGHP vec2 sin_a = sin(angles);
    return ctwiddle(cos_a.x, sin_a.x, cos_a.y, sin_a.y);
}

#ifdef FFT_INVERSE
#define TWIDDLE_1_8 (vec2(+SQRT_1_2, +SQRT_1_2).xyxy)
#define TWIDDLE_3_8 (vec2(-SQRT_1_2, +SQRT_1_2).xyxy)
#else
#define TWIDDLE_1_8 (vec2(+SQRT_1_2, -SQRT_1_2).xyxy)
#define TWIDDLE_3_8 (vec2(-SQRT_1_2, -SQRT_1_2).xyxy)
#endif

// 1-component twiddle, pack in vec2.
#else

#define FFT_OUTPUT_STEP 1u
#define FFT_OUTPUT_SHIFT 0u
#define ctwiddle vec2
ctwiddle twiddle(uint k, uint p)
{
    // Trancendentals should always be done in highp.
    FFT_HIGHP float angle = PI_DIR * float(k) / float(p);
    return ctwiddle(cos(angle), sin(angle));
}

#ifdef FFT_INVERSE
#define TWIDDLE_1_8 (vec2(+SQRT_1_2, +SQRT_1_2))
#define TWIDDLE_3_8 (vec2(-SQRT_1_2, +SQRT_1_2))
#else
#define TWIDDLE_1_8 (vec2(+SQRT_1_2, -SQRT_1_2))
#define TWIDDLE_3_8 (vec2(-SQRT_1_2, -SQRT_1_2))
#endif

#endif

// Complex multiply by v * -j. Trivial case which can avoid mul/add.
vec4 cmul_minus_j(vec4 v)
{
    return vec4(v.y, -v.x, v.w, -v.z);
}

vec2 cmul_minus_j(vec2 v)
{
    return vec2(v.y, -v.x);
}

// Complex multiply by v * +j. Trivial case which can avoid mul/add.
vec4 cmul_plus_j(vec4 v)
{
    return vec4(-v.y, v.x, -v.w, v.z);
}

vec2 cmul_plus_j(vec2 v)
{
    return vec2(-v.y, v.x);
}

#ifdef FFT_INVERSE
#define cmul_dir_j(v) cmul_plus_j(v)
#else
#define cmul_dir_j(v) cmul_minus_j(v)
#endif

// Calculate an in-place butterfly with twiddle factors.
// a ----------- a + wb
//        \   /
//         \ /
//          X
//         / \
//        /   \
// w * b ------- a - wb
//
void butterfly(inout vec4 a, inout vec4 b, vec4 w)
{
    vec4 t = cmul(b, w);
    b = a - t;
    a = a + t;
}

// Computes butterflies, but the twiddle factors for the two butterflies are
// identical.
void butterfly(inout vec4 a, inout vec4 b, vec2 w)
{
    butterfly(a, b, w.xyxy);
}

void butterfly(inout vec2 a, inout vec2 b, vec2 w)
{
    vec2 t = cmul(b, w);
    b = a - t;
    a = a + t;
}

// First pass butterfly, special case where w = 1.
void butterfly_p1(inout vec4 a, inout vec4 b)
{
    vec4 t = b;
    b = a - t;
    a = a + t;
}

// First pass butterfly, but also multiply in a twiddle factor of -j to b afterwards.
// Used in P == 1 transforms for radix-4, radix-8 etc.
void butterfly_p1_minus_j(inout vec4 a, inout vec4 b)
{
    vec4 t = b;
    b = vec4(1.0, -1.0, 1.0, -1.0) * (a.yxwz - t.yxwz);
    a = a + t;
}

void butterfly_p1_plus_j(inout vec4 a, inout vec4 b)
{
    vec4 t = b;
    b = vec4(-1.0, 1.0, -1.0, 1.0) * (a.yxwz - t.yxwz);
    a = a + t;
}

void butterfly_p1(inout vec2 a, inout vec2 b)
{
    vec2 t = b;
    b = a - t;
    a = a + t;
}

void butterfly_p1_minus_j(inout vec2 a, inout vec2 b)
{
    vec2 t = b;
    b = vec2(1.0, -1.0) * (a.yx - t.yx);
    a = a + t;
}

void butterfly_p1_plus_j(inout vec2 a, inout vec2 b)
{
    vec2 t = b;
    b = vec2(-1.0, 1.0) * (a.yx - t.yx);
    a = a + t;
}

#ifdef FFT_INVERSE
#define butterfly_p1_dir_j(a, b) butterfly_p1_plus_j(a, b)
#else
#define butterfly_p1_dir_j(a, b) butterfly_p1_minus_j(a, b)
#endif

#ifdef FFT_RESOLVE_REAL_TO_COMPLEX
vec2 r2c_twiddle(uint i, uint p)
{
    vec2 w = -twiddle(i, p);
    return vec2(-w.y, w.x);
}

// See http://www.engineeringproductivitytools.com/stuff/T0001/PT10.HTM for
// how the real-to-complex and complex-to-real resolve passes work.
// The final real-to-complex transform pass is done by extracting two interleaved FFTs by conjugate symmetry.

// If we have a real sequence:
// (r0, r1, r2, r3, r4, ...), we merge two adjacent real values to a sequence of complex numbers.
// We take the FFT of this complex sequence as normal.
// What we end up with really is:
// FFT((r0, r2, r4, r6, ...)) + FFT(j * (r1, r3, r5, r7, ...)).
// If we know the individual FFTs of the even and the odds we can complete the FFT by a single decimation-in-frequency stage.
// By conjugate symmetry, we can extract the even and odd FFTs and complex our transform.
// Complex-to-real is just the same thing, but in reverse.

void FFT_real_to_complex(uvec2 i)
{
    uint stride = gl_NumWorkGroups.x * gl_WorkGroupSize.x;
    uint offset = i.y * stride;

    if (i.x == 0u)
    {
#ifdef FFT_INPUT_TEXTURE
        vec2 x = load_texture(i);
#else
        vec2 x = load_global(offset);
#endif

#ifdef FFT_OUTPUT_IMAGE
        store(ivec2(i), vec2(x.x + x.y, 0.0));
        store(ivec2(i) + ivec2(stride, 0), vec2(x.x - x.y, 0.0));
#else
        store_global(2u * offset, vec2(x.x + x.y, 0.0));
        store_global(2u * offset + stride, vec2(x.x - x.y, 0.0));
#endif
    }
    else
    {
#ifdef FFT_INPUT_TEXTURE
        vec2 a = load_texture(i);
        vec2 b = load_texture(uvec2(stride - i.x, i.y));
#else
        vec2 a = load_global(offset + i.x);
        vec2 b = load_global(offset + stride - i.x);
#endif
        b = vec2(b.x, -b.y);
        vec2 fe = a + b;
        vec2 fo = cmul(a - b, r2c_twiddle(i.x, stride));

#ifdef FFT_OUTPUT_IMAGE
        store(ivec2(i), 0.5 * (fe + fo));
#else
        store_global(2u * offset + i.x, 0.5 * (fe + fo));
#endif
    }
}
#endif

#ifdef FFT_RESOLVE_COMPLEX_TO_REAL
vec2 c2r_twiddle(uint i, uint p)
{
    vec2 w = twiddle(i, p);
    return vec2(-w.y, w.x);
}

void FFT_complex_to_real(uvec2 i)
{
    uint stride = gl_NumWorkGroups.x * gl_WorkGroupSize.x;
    uint offset = i.y * stride;

#ifdef FFT_INPUT_TEXTURE
    vec2 a = load_texture(i);
    vec2 b = load_texture(uvec2(stride - i.x, i.y));
#else
    vec2 a = load_global(2u * offset + i.x);
    vec2 b = load_global(2u * offset + stride - i.x);
#endif
    b = vec2(b.x, -b.y);
    vec2 even = a + b;
    vec2 odd = cmul(a - b, c2r_twiddle(i.x, stride));

    store_global(offset + i.x, even + odd);
}
#endif

