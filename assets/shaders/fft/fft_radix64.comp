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

// Basically the same as FFT16, but 2xFFT-8. See comments in fft_radix16.comp for more.

void FFT64_p1_horiz(uvec2 i)
{
    uint octa_samples = gl_NumWorkGroups.x * gl_WorkGroupSize.x;
    uint offset = i.y * octa_samples * 64u;

    uint fft = gl_LocalInvocationID.x;
    uint block = gl_LocalInvocationID.z;
    uint base = get_shared_base(fft);

#ifdef FFT_INPUT_TEXTURE
    cfloat a = load_texture(i + uvec2((block +  0u) * octa_samples, 0u));
    cfloat b = load_texture(i + uvec2((block +  8u) * octa_samples, 0u));
    cfloat c = load_texture(i + uvec2((block + 16u) * octa_samples, 0u));
    cfloat d = load_texture(i + uvec2((block + 24u) * octa_samples, 0u));
    cfloat e = load_texture(i + uvec2((block + 32u) * octa_samples, 0u));
    cfloat f = load_texture(i + uvec2((block + 40u) * octa_samples, 0u));
    cfloat g = load_texture(i + uvec2((block + 48u) * octa_samples, 0u));
    cfloat h = load_texture(i + uvec2((block + 56u) * octa_samples, 0u));
#else
    cfloat a = load_global(offset + i.x + (block +  0u) * octa_samples);
    cfloat b = load_global(offset + i.x + (block +  8u) * octa_samples);
    cfloat c = load_global(offset + i.x + (block + 16u) * octa_samples);
    cfloat d = load_global(offset + i.x + (block + 24u) * octa_samples);
    cfloat e = load_global(offset + i.x + (block + 32u) * octa_samples);
    cfloat f = load_global(offset + i.x + (block + 40u) * octa_samples);
    cfloat g = load_global(offset + i.x + (block + 48u) * octa_samples);
    cfloat h = load_global(offset + i.x + (block + 56u) * octa_samples);
#endif
    FFT8_p1(a, b, c, d, e, f, g, h);

    store_shared(a, b, c, d, e, f, g, h, block, base);
    load_shared(a, b, c, d, e, f, g, h, block, base);

    const uint p = 8u;
    FFT8(a, b, c, d, e, f, g, h, FFT_OUTPUT_STEP * block, p);

    uint k = (FFT_OUTPUT_STEP * block) & (p - 1u);
    uint j = ((FFT_OUTPUT_STEP * block - k) * 8u) + k;

#ifndef FFT_OUTPUT_IMAGE
    store_global(offset + 64u * i.x + ((j + 0u * p) >> FFT_OUTPUT_SHIFT), a);
    store_global(offset + 64u * i.x + ((j + 1u * p) >> FFT_OUTPUT_SHIFT), e);
    store_global(offset + 64u * i.x + ((j + 2u * p) >> FFT_OUTPUT_SHIFT), c);
    store_global(offset + 64u * i.x + ((j + 3u * p) >> FFT_OUTPUT_SHIFT), g);
    store_global(offset + 64u * i.x + ((j + 4u * p) >> FFT_OUTPUT_SHIFT), b);
    store_global(offset + 64u * i.x + ((j + 5u * p) >> FFT_OUTPUT_SHIFT), f);
    store_global(offset + 64u * i.x + ((j + 6u * p) >> FFT_OUTPUT_SHIFT), d);
    store_global(offset + 64u * i.x + ((j + 7u * p) >> FFT_OUTPUT_SHIFT), h);
#endif
}

void FFT64_horiz(uvec2 i, uint p)
{
    uint octa_samples = gl_NumWorkGroups.x * gl_WorkGroupSize.x;
    uint offset = i.y * octa_samples * 64u;

    uint fft = gl_LocalInvocationID.x;
    uint block = gl_LocalInvocationID.z;
    uint base = get_shared_base(fft);

    cfloat a = load_global(offset + i.x + (block +  0u) * octa_samples);
    cfloat b = load_global(offset + i.x + (block +  8u) * octa_samples);
    cfloat c = load_global(offset + i.x + (block + 16u) * octa_samples);
    cfloat d = load_global(offset + i.x + (block + 24u) * octa_samples);
    cfloat e = load_global(offset + i.x + (block + 32u) * octa_samples);
    cfloat f = load_global(offset + i.x + (block + 40u) * octa_samples);
    cfloat g = load_global(offset + i.x + (block + 48u) * octa_samples);
    cfloat h = load_global(offset + i.x + (block + 56u) * octa_samples);

    FFT8(a, b, c, d, e, f, g, h, FFT_OUTPUT_STEP * i.x, p);

    store_shared(a, b, c, d, e, f, g, h, block, base);
    load_shared(a, b, c, d, e, f, g, h, block, base);

    uint k = (FFT_OUTPUT_STEP * i.x) & (p - 1u);
    uint j = ((FFT_OUTPUT_STEP * i.x - k) * 64u) + k;

    FFT8(a, b, c, d, e, f, g, h, k + block * p, 8u * p);

#ifdef FFT_OUTPUT_IMAGE
    store(ivec2(j + (block +  0u) * p, i.y), a);
    store(ivec2(j + (block +  8u) * p, i.y), e);
    store(ivec2(j + (block + 16u) * p, i.y), c);
    store(ivec2(j + (block + 24u) * p, i.y), g);
    store(ivec2(j + (block + 32u) * p, i.y), b);
    store(ivec2(j + (block + 40u) * p, i.y), f);
    store(ivec2(j + (block + 48u) * p, i.y), d);
    store(ivec2(j + (block + 56u) * p, i.y), h);
#else
    store_global(offset + ((j + (block +  0u) * p) >> FFT_OUTPUT_SHIFT), a);
    store_global(offset + ((j + (block +  8u) * p) >> FFT_OUTPUT_SHIFT), e);
    store_global(offset + ((j + (block + 16u) * p) >> FFT_OUTPUT_SHIFT), c);
    store_global(offset + ((j + (block + 24u) * p) >> FFT_OUTPUT_SHIFT), g);
    store_global(offset + ((j + (block + 32u) * p) >> FFT_OUTPUT_SHIFT), b);
    store_global(offset + ((j + (block + 40u) * p) >> FFT_OUTPUT_SHIFT), f);
    store_global(offset + ((j + (block + 48u) * p) >> FFT_OUTPUT_SHIFT), d);
    store_global(offset + ((j + (block + 56u) * p) >> FFT_OUTPUT_SHIFT), h);
#endif
}

void FFT64_p1_vert(uvec2 i)
{
    uvec2 octa_samples = gl_NumWorkGroups.xy * gl_WorkGroupSize.xy;
    uint stride = uStride;
    uint y_stride = stride * octa_samples.y;
    uint offset = stride * i.y;

    uint fft = gl_LocalInvocationID.x;
    uint block = gl_LocalInvocationID.z;
    uint base = get_shared_base(fft);

#ifdef FFT_INPUT_TEXTURE
    cfloat a = load_texture(i + uvec2(0u, (block +  0u) * octa_samples.y));
    cfloat b = load_texture(i + uvec2(0u, (block +  8u) * octa_samples.y));
    cfloat c = load_texture(i + uvec2(0u, (block + 16u) * octa_samples.y));
    cfloat d = load_texture(i + uvec2(0u, (block + 24u) * octa_samples.y));
    cfloat e = load_texture(i + uvec2(0u, (block + 32u) * octa_samples.y));
    cfloat f = load_texture(i + uvec2(0u, (block + 40u) * octa_samples.y));
    cfloat g = load_texture(i + uvec2(0u, (block + 48u) * octa_samples.y));
    cfloat h = load_texture(i + uvec2(0u, (block + 56u) * octa_samples.y));
#else
    cfloat a = load_global(offset + i.x + (block +  0u) * y_stride);
    cfloat b = load_global(offset + i.x + (block +  8u) * y_stride);
    cfloat c = load_global(offset + i.x + (block + 16u) * y_stride);
    cfloat d = load_global(offset + i.x + (block + 24u) * y_stride);
    cfloat e = load_global(offset + i.x + (block + 32u) * y_stride);
    cfloat f = load_global(offset + i.x + (block + 40u) * y_stride);
    cfloat g = load_global(offset + i.x + (block + 48u) * y_stride);
    cfloat h = load_global(offset + i.x + (block + 56u) * y_stride);
#endif

    FFT8_p1(a, b, c, d, e, f, g, h);

    store_shared(a, b, c, d, e, f, g, h, block, base);
    load_shared(a, b, c, d, e, f, g, h, block, base);

    const uint p = 8u;
    FFT8(a, b, c, d, e, f, g, h, block, p);

#ifndef FFT_OUTPUT_IMAGE
    store_global((64u * i.y + block +  0u) * stride + i.x, a);
    store_global((64u * i.y + block +  8u) * stride + i.x, e);
    store_global((64u * i.y + block + 16u) * stride + i.x, c);
    store_global((64u * i.y + block + 24u) * stride + i.x, g);
    store_global((64u * i.y + block + 32u) * stride + i.x, b);
    store_global((64u * i.y + block + 40u) * stride + i.x, f);
    store_global((64u * i.y + block + 48u) * stride + i.x, d);
    store_global((64u * i.y + block + 56u) * stride + i.x, h);
#endif
}

void FFT64_vert(uvec2 i, uint p)
{
    uvec2 octa_samples = gl_NumWorkGroups.xy * gl_WorkGroupSize.xy;
    uint stride = uStride;
    uint y_stride = stride * octa_samples.y;
    uint offset = stride * i.y;

    uint fft = gl_LocalInvocationID.x;
    uint block = gl_LocalInvocationID.z;
    uint base = get_shared_base(fft);

    cfloat a = load_global(offset + i.x + (block +  0u) * y_stride);
    cfloat b = load_global(offset + i.x + (block +  8u) * y_stride);
    cfloat c = load_global(offset + i.x + (block + 16u) * y_stride);
    cfloat d = load_global(offset + i.x + (block + 24u) * y_stride);
    cfloat e = load_global(offset + i.x + (block + 32u) * y_stride);
    cfloat f = load_global(offset + i.x + (block + 40u) * y_stride);
    cfloat g = load_global(offset + i.x + (block + 48u) * y_stride);
    cfloat h = load_global(offset + i.x + (block + 56u) * y_stride);

    FFT8(a, b, c, d, e, f, g, h, i.y, p);

    store_shared(a, b, c, d, block, base);
    load_shared(a, b, c, d, block, base);

    uint k = i.y & (p - 1u);
    uint j = ((i.y - k) * 64u) + k;

    FFT8(a, b, c, d, e, f, g, h, k + block * p, 8u * p);

#ifdef FFT_OUTPUT_IMAGE
    store(ivec2(i.x, j + (block +  0u) * p), a);
    store(ivec2(i.x, j + (block +  8u) * p), e);
    store(ivec2(i.x, j + (block + 16u) * p), c);
    store(ivec2(i.x, j + (block + 24u) * p), g);
    store(ivec2(i.x, j + (block + 32u) * p), b);
    store(ivec2(i.x, j + (block + 40u) * p), f);
    store(ivec2(i.x, j + (block + 48u) * p), d);
    store(ivec2(i.x, j + (block + 56u) * p), h);
#else
    store_global(stride * (j + (block +  0u) * p) + i.x, a);
    store_global(stride * (j + (block +  8u) * p) + i.x, e);
    store_global(stride * (j + (block + 16u) * p) + i.x, c);
    store_global(stride * (j + (block + 24u) * p) + i.x, g);
    store_global(stride * (j + (block + 32u) * p) + i.x, b);
    store_global(stride * (j + (block + 40u) * p) + i.x, f);
    store_global(stride * (j + (block + 48u) * p) + i.x, d);
    store_global(stride * (j + (block + 56u) * p) + i.x, h);
#endif
}

