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
#include "gli/load.hpp"
#include "gli/texture2d.hpp"
#include "FastNoise.h"
#include "util.hpp"
#include "math.hpp"

using namespace std;
using namespace Granite;

int main(int argc, char *argv[])
{
	if (argc != 4)
	{
		LOGE("Usage: %s output splatmap freq\n", argv[0]);
		return 1;
	}

	float freq = stof(argv[3]);

	FastNoise noise[4];
	noise[0].SetFractalOctaves(7);
	noise[0].SetFrequency(freq);
	noise[0].SetFractalGain(0.25f);

	noise[1].SetFractalOctaves(7);
	noise[1].SetFrequency(freq);
	noise[1].SetFractalGain(0.3f);

	noise[2].SetFractalOctaves(7);
	noise[2].SetFrequency(freq);
	noise[2].SetFractalGain(0.4f);

	noise[3].SetFractalOctaves(7);
	noise[3].SetFrequency(freq);
	noise[3].SetFractalGain(0.6f);

	auto splatmap = gli::load(argv[2]);
	if (splatmap.empty())
	{
		LOGE("Failed to load splatmap: %s\n", argv[2]);
		return 1;
	}

	int size = splatmap.extent(0).x;
	if (splatmap.extent(0).x != splatmap.extent(0).y)
	{
		LOGE("Splatmap must be square.\n");
		return 1;
	}

	gli::texture2d heights(gli::FORMAT_R32_SFLOAT_PACK32, gli::extent2d(size, size), 1);
	float *data = static_cast<float *>(heights.data());

	using PixelRGBA = tvec4<uint8_t>;

	for (int y = 0; y < size; y++)
	{
		for (int x = 0; x < size; x++)
		{
			auto splat = splatmap.load<PixelRGBA>(gli::extent3d(x, y, 0), 0, 0, 0);

			float height = 0.0f;
			int w = 0.0f;

			int alpha_w = 255 - glm::max(splat.z, glm::max(splat.x, splat.y));

			height += noise[0].GetSimplexFractal(x, y) * splat.x;
			w += splat.x;
			height += noise[1].GetSimplexFractal(x, y) * splat.y;
			w += splat.y;
			height += noise[2].GetSimplexFractal(x, y) * splat.z;
			w += splat.z;
			height += noise[3].GetSimplexFractal(x, y) * alpha_w;
			w += alpha_w;

			*data++ = height / w;
		}
	}

	if (!gli::save(heights, argv[1]))
	{
		LOGE("Failed to save heightmap: %s\n", argv[1]);
		return 1;
	}
}
