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

#include "gli/load.hpp"
#include "gli/save.hpp"
#include "util.hpp"
#include <vector>
#include <gli/texture_cube.hpp>

using namespace std;

int main(int argc, char *argv[])
{
	if (argc != 8)
	{
		LOGE("Usage: %s <output> <inputs> x 6...\n", argv[0]);
		return 1;
	}

	vector<gli::texture> inputs;
	gli::format fmt = gli::FORMAT_UNDEFINED;
	int width = 0;
	int height = 0;
	unsigned levels = 0;

	for (int i = 2; i < argc; i++)
	{
		gli::texture tex = gli::load(argv[i]);
		if (tex.empty())
		{
			LOGE("Failed to load texture: %s\n", argv[i]);
			return 1;
		}

		if (fmt != gli::FORMAT_UNDEFINED)
		{
			if (tex.format() != fmt)
			{
				LOGE("Format mismatch!\n");
				return 1;
			}

			if (tex.extent().x != width)
			{
				LOGE("Mismatch width\n");
				return 1;
			}

			if (tex.extent().y != height)
			{
				LOGE("Mismatch height\n");
				return 1;
			}

			if (tex.levels() != levels)
			{
				LOGE("Mismatch levels\n");
				return 1;
			}
		}

		if (tex.target() != gli::TARGET_2D)
		{
			LOGE("Input can only be 2D textures\n");
			return 1;
		}

		fmt = tex.format();
		width = tex.extent().x;
		height = tex.extent().y;
		levels = tex.levels();
		inputs.push_back(move(tex));

		if (width != height)
		{
			LOGE("Input can only be square\n");
			return 1;
		}
	}

	gli::texture_cube cube(fmt, gli::extent2d(width, height), levels);
	for (unsigned level = 0; level < levels; level++)
	{
		for (unsigned face = 0; face < 6; face++)
		{
			auto *dst = cube.data(0, face, level);
			auto *src = inputs[face].data(0, 0, level);

			size_t dst_size = cube.size(level);
			size_t src_size = inputs[face].size(level);
			if (dst_size != src_size)
			{
				LOGE("Size mismatch!\n");
				return 1;
			}

			memcpy(dst, src, dst_size);
		}
	}

	if (!gli::save(cube, argv[1]))
	{
		LOGE("Failed to save file: %s\n", argv[1]);
		return 1;
	}

	return 0;
}