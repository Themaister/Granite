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

#include "gli/save.hpp"
#include "gli/texture2d.hpp"
#include "gli/generate_mipmaps.hpp"
#include "stb_image.h"
#include "util.hpp"
#include "math.hpp"
#include "tool_util.hpp"

using namespace Granite;
using namespace Util;

static gli::texture2d blur_texture(const gli::texture2d &input)
{
	LOGI("Blurring texture.\n");
	gli::texture2d output(input.format(), input.extent(), input.levels());

	static constexpr int kernel_size = 11;
	static constexpr int kernel_taps = kernel_size * 2 + 1;
	float weights = 0.0f;
	float kernel[kernel_taps][kernel_taps];

	const auto sample_kernel = [](int x, int y) -> float {
		x -= kernel_size;
		y -= kernel_size;
		return glm::exp2(float(x * x + y * y) * -0.05f);
	};

	for (int y = 0; y < kernel_taps; y++)
	{
		for (int x = 0; x < kernel_taps; x++)
		{
			kernel[y][x] = sample_kernel(x, y);
			weights += kernel[y][x];
		}
	}

	for (int y = 0; y < kernel_taps; y++)
		for (int x = 0; x < kernel_taps; x++)
			kernel[y][x] /= weights;

	int width = input.extent().x;
	int height = input.extent().y;

	using Pixel = tvec4<uint8_t>;
	const auto *src = static_cast<const Pixel *>(input.data());
	auto *dst = static_cast<Pixel *>(output.data());

	for (int y = 0; y < height; y++)
	{
		for (int x = 0; x < width; x++)
		{
			vec4 result(0.0f);
			for (int ky = 0; ky < kernel_taps; ky++)
			{
				for (int kx = 0; kx < kernel_taps; kx++)
				{
					int sx = clamp(x + kx - kernel_size, 0, width - 1);
					int sy = clamp(y + ky - kernel_size, 0, height - 1);

					auto &c = src[sy * width + sx];
					result += vec4(c.r, c.g, c.b, c.a) * kernel[ky][kx];
				}
			}

			result = clamp(round(result), vec4(0.0f), vec4(255.0f));
			dst[y * width + x] = Pixel(result.r, result.g, result.b, result.a);
		}
	}

	return output;
}

int main(int argc, char *argv[])
{
	if (argc < 3)
	{
		LOGE("Usage: %s input.png output.ktx [--generate-mipmaps] [--srgb] [--blur-gauss]\n", argv[0]);
		return 1;
	}

	bool generate_mipmaps = false;
	bool srgb = false;
	bool blur_gauss = false;

	for (int i = 3; i < argc; i++)
	{
		if (strcmp(argv[i], "--generate-mipmaps") == 0)
			generate_mipmaps = true;
		else if (strcmp(argv[i], "--srgb") == 0)
			srgb = true;
		else if (strcmp(argv[i], "--blur-gauss") == 0)
			blur_gauss = true;
		else
		{
			LOGE("Invalid option %s\n", argv[i]);
			return 1;
		}
	}

	int width, height;
	int components;

	FILE *file = fopen(argv[1], "rb");
	if (!file)
	{
		LOGE("Failed to load PNG: %s\n", argv[1]);
		return 1;
	}

	auto *buffer = stbi_load_from_file(file, &width, &height, &components, 4);
	fclose(file);

	unsigned levels = num_miplevels(width, height);

	auto texture = gli::texture2d(srgb ? gli::FORMAT_RGBA8_SRGB_PACK8 : gli::FORMAT_RGBA8_UNORM_PACK8,
	                              gli::texture2d::extent_type(width, height), generate_mipmaps ? levels : 1);

	auto *data = static_cast<uint8_t *>(texture.data(0, 0, 0));
	memcpy(data, buffer, width * height * 4);

	stbi_image_free(buffer);

	if (blur_gauss)
		texture = blur_texture(texture);

	if (generate_mipmaps)
		texture = gli::generate_mipmaps(texture, gli::filter::FILTER_LINEAR);

	if (!gli::save_ktx(texture, argv[2]))
	{
		LOGE("Failed to save KTX file: %s\n", argv[2]);
		return 1;
	}

	return 0;
}