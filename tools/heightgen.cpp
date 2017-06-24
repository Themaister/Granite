#include "gli/save.hpp"
#include "gli/texture2d.hpp"
#include "FastNoise.h"
#include "util.hpp"

using namespace std;

int main(int argc, char *argv[])
{
	if (argc != 4)
	{
		LOGE("Usage: %s output size freq\n", argv[0]);
		return 1;
	}

	float freq = stof(argv[3]);

	FastNoise noise;
	noise.SetFractalOctaves(6);
	noise.SetFrequency(freq);

	int size = stoi(argv[2]);

	gli::texture2d heights(gli::FORMAT_R32_SFLOAT_PACK32, gli::extent2d(size, size), 1);
	float *data = static_cast<float *>(heights.data());

	for (int y = 0; y < size; y++)
		for (int x = 0; x < size; x++)
			*data++ = noise.GetSimplexFractal(x, y);

	if (!gli::save(heights, argv[1]))
	{
		LOGE("Failed to save heightmap: %s\n", argv[1]);
		return 1;
	}
}
