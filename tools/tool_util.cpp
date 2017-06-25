#include "tool_util.hpp"
#include "math.hpp"
#include "util.hpp"

using namespace glm;

namespace Util
{
unsigned num_miplevels(unsigned width, unsigned height)
{
	unsigned size = max(width, height);
	unsigned levels = 0;
	while (size)
	{
		levels++;
		size >>= 1;
	}
	return levels;
}

static float srgb_conv(float v)
{
	if (v <= 0.04045f)
		return v * (1.0f / 12.92f);
	else
		return pow((v + 0.055f) * (1.0f / 1.055f), 2.4f);
}

vec4 skybox_to_fog_color(const gli::texture &cube)
{
	int width = cube.extent().x;
	int height = cube.extent().y;

	vec3 color(0.0f);

	bool srgb = false;
	if (cube.format() == gli::FORMAT_RGBA8_SRGB_PACK8)
		srgb = true;
	else if (cube.format() == gli::FORMAT_RGBA8_UNORM_PACK8)
		srgb = false;
	else
	{
		LOGE("Unrecognized cubemap format, returning white.\n");
		return vec4(1.0f);
	}

	using Pixel = tvec4<uint8_t>;

	for (size_t face = 0; face < cube.faces(); face++)
	{
		for (int y = 0; y < height; y++)
		{
			for (int x = 0; x < width; x++)
			{
				auto pixel = cube.load<Pixel>(gli::extent3d(x, y, 0), 0, face, 0);

				vec3 rgb = vec3(pixel.r, pixel.g, pixel.b) * (1.0f / 255.0f);
				if (srgb)
				{
					rgb.r = srgb_conv(rgb.r);
					rgb.g = srgb_conv(rgb.g);
					rgb.b = srgb_conv(rgb.b);
				}

				color += rgb;
			}
		}
	}

	auto res = color / vec3(cube.faces() * width * height);
	return vec4(res, 1.0f);
}
}