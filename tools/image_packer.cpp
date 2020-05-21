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

#include "stb_image.h"
#include "stb_image_write.h"
#include "logging.hpp"
#include <vector>
#include <string.h>
#include <stdint.h>

// Handy tool to convert Gloss/Metallic/AO maps to packed textures suitable for glTF 2.0 PBR.

template <uint8_t value>
static inline void write_constant(stbi_uc *output, int output_component, int x, int y, int stride, const stbi_uc *)
{
	output[4 * (y * stride + x) + output_component] = value;
}

static inline void write_image(stbi_uc *output, int output_component, int x, int y, int stride, const stbi_uc *input)
{
	output[4 * (y * stride + x) + output_component] = input[4 * (y * stride + x)];
}

static inline void write_image_invert(stbi_uc *output, int output_component, int x, int y, int stride, const stbi_uc *input)
{
	output[4 * (y * stride + x) + output_component] = 255 - input[4 * (y * stride + x)];
}

int main(int argc, char *argv[])
{
	if (argc < 4 || (argc & 1))
	{
		LOGE("Usage: %s ([R | G | B | A | INV_R | INV_G | INV_B | INV_A] [<component-image> | ONE | ZERO])... <output-image>\n", argv[0]);
		return 1;
	}

	int input_components = (argc - 2) / 2;
	const char *output_image = argv[argc - 1];

	int width = 0;
	int height = 0;
	std::vector<uint8_t> output_data;
	stbi_uc *image_data[4] = {};

	using pixel_func = void (*)(stbi_uc *, int, int, int, int, const stbi_uc *);

	pixel_func comp_funcs[4] = {
		write_constant<0>,
		write_constant<0>,
		write_constant<0>,
		write_constant<0xff>,
	};

	const auto load_image = [&](int component, const char *value, bool invert) {
		if (strcmp(value, "ZERO") == 0)
			comp_funcs[component] = write_constant<0>;
		else if (strcmp(value, "ONE") == 0)
			comp_funcs[component] = write_constant<0xff>;
		else
		{
			int x, y, chans;
			image_data[component] = stbi_load(value, &x, &y, &chans, 4);
			if (!image_data[component])
			{
				LOGE("Failed to load image: %s\n", value);
				exit(1);
			}

			if (width || height)
			{
				if (x != width || y != height)
				{
					LOGE("Dimension mismatch!\n");
					exit(1);
				}
			}

			width = x;
			height = y;

			if (invert)
				comp_funcs[component] = write_image_invert;
			else
				comp_funcs[component] = write_image;
		}
	};

	for (int i = 0; i < input_components; i++)
	{
		const char *command = argv[1 + 2 * i];
		const char *value = argv[2 + 2 * i];

		if (strcmp(command, "R") == 0)
			load_image(0, value, false);
		else if (strcmp(command, "G") == 0)
			load_image(1, value, false);
		else if (strcmp(command, "B") == 0)
			load_image(2, value, false);
		else if (strcmp(command, "A") == 0)
			load_image(3, value, false);
		else if (strcmp(command, "INV_R") == 0)
			load_image(0, value, true);
		else if (strcmp(command, "INV_G") == 0)
			load_image(1, value, true);
		else if (strcmp(command, "INV_B") == 0)
			load_image(2, value, true);
		else if (strcmp(command, "INV_A") == 0)
			load_image(3, value, true);
		else
		{
			LOGE("Unrecognized command: %s\n", command);
			return 1;
		}
	}

	if (!width || !height)
	{
		LOGE("No image found. Cannot infer geometry.\n");
		return 1;
	}

	output_data.resize(width * height * 4);
	for (int y = 0; y < height; y++)
		for (int x = 0; x < width; x++)
			for (int c = 0; c < 4; c++)
				comp_funcs[c](output_data.data(), c, x, y, width, image_data[c]);

	if (!stbi_write_png(output_image, width, height, 4, output_data.data(), width * 4))
	{
		LOGE("Failed to write image: %s\n", output_image);
		return 1;
	}

	for (auto &image : image_data)
		if (image)
			stbi_image_free(image);
}