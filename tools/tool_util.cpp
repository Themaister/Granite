#include "tool_util.hpp"
#include "math.hpp"
#include "util.hpp"
#include "fft.h"
#include <complex>

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

void filter_tiling_artifacts(gli::texture &target, unsigned level, const gli::image &image)
{
	gli::image result(image.format(), image.extent());

	int width = image.extent().x;
	int height = image.extent().y;

	if (width & (width - 1))
		throw std::logic_error("Width needs to be POT.");
	if (height & (height - 1))
		throw std::logic_error("Height needs to be POT.");

	float inv_scale = 1.0f / (width * height);

	auto *fft_input = static_cast<float *>(mufft_alloc(2 * sizeof(float) * width * height)); // Required for c2r.
	auto *fft_output = static_cast<std::complex<float> *>(mufft_alloc(sizeof(std::complex<float>) * width * height));

	using Pixel = glm::tvec4<uint8_t>;

	mufft_plan_2d *forward_plan = mufft_create_plan_2d_r2c(width, height, 0);
	mufft_plan_2d *inverse_plan = mufft_create_plan_2d_c2r(width, height, 0);

	if (!forward_plan || !inverse_plan)
	{
		memset(target.data(0, 0, level), 0, target.size(level));
		return;
	}

	std::vector<float> freq_domain(width * height);
	for (int y = 0; y <= height / 2; y++)
	{
		for (int x = 0; x <= width / 2; x++)
		{
			float response = 1.0f;
			if (x == width / 2 || y == height / 2)
				response = 0.0f;
			else if (x == 0 && y)
				response = 0.0f;
			else if (y == 0 && x)
				response = 0.0f;
			else if (x || y) // Keep the DC
				response = sqrt(4.0f * (x * x + y * y) / (width * width + height * height));

			freq_domain[y * width + x] = inv_scale * response;
		}

		// Mirror in frequency domain.
		if (y && (y < height / 2))
			memcpy(&freq_domain[(height - y) * width], &freq_domain[y * width], sizeof(float) * width);
	}

	for (unsigned component = 0; component < 4; component++)
	{
		auto *src = static_cast<const Pixel *>(image.data());
		for (int i = 0; i < width * height; i++)
			fft_input[i] = src[i][component] * (1.0f / 255.0f);
		mufft_execute_plan_2d(forward_plan, fft_output, fft_input);

		for (unsigned i = 0; i < width * height; i++)
			fft_output[i] *= freq_domain[i];

		mufft_execute_plan_2d(inverse_plan, fft_input, fft_output);
		for (int i = 0; i < width * height; i++)
			static_cast<Pixel *>(target.data(0, 0, level))[i][component] = uint8_t(round(clamp(fft_input[i] * 255.0f, 0.0f, 255.0f)));
	}

	mufft_free_plan_2d(forward_plan);
	mufft_free_plan_2d(inverse_plan);
	mufft_free(fft_input);
	mufft_free(fft_output);
}
}