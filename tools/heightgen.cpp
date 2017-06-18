#include "stb_image.h"
#include "gli/texture2d.hpp"
#include "gli/save.hpp"
#include "gli/generate_mipmaps.hpp"
#include "util.hpp"
#include "math.hpp"

using namespace glm;

static unsigned num_miplevels(unsigned width, unsigned height)
{
	unsigned size = std::max(width, height);
	unsigned levels = 0;
	while (size)
	{
		levels++;
		size >>= 1;
	}
	return levels;
}

static vec2 get_plane_range(const float *data, unsigned width, unsigned height, unsigned stride)
{
	float maximum = -FLT_MAX;
	float minimum = FLT_MAX;

	for (unsigned y = 0; y < height - 1; y++)
	{
		for (unsigned x = 0; x < width - 1; x++)
		{
			maximum = max(maximum, data[y * stride + x]);
			minimum = min(minimum, data[y * stride + x]);
		}
	}

	return vec2(minimum, maximum);
}

static float get_plane_error(const float *data, unsigned width, unsigned height, unsigned stride)
{
	// Estimate a plane equation, then find the mean error from that estimation.
	double mean_dx = 0.0;
	double mean_dy = 0.0;
	double mean = 0.0;
	for (unsigned y = 0; y < height - 1; y++)
	{
		for (unsigned x = 0; x < width - 1; x++)
		{
			double dx = data[y * stride + (x + 1)] - data[y * stride + x];
			double dy = data[(y + 1) * stride + (x + 1)] - data[y * stride + x];
			mean_dx += dx;
			mean_dy += dy;
			mean += data[y * stride + x];
		}
	}

	mean /= (width - 1) * (height - 1);
	mean_dx /= (width - 1) * (height - 1);
	mean_dy /= (width - 1) * (height - 1);

	dvec2 delta = dvec2(mean_dx, mean_dy);
	double base = mean -
		0.5 * delta.x * (width - 1) -
		0.5 * delta.y * (height - 1);

	double error = 0.0;
	for (unsigned y = 0; y < height; y++)
	{
		for (unsigned x = 0; x < width; x++)
		{
			double h = data[y * stride + x];
			double estimated = base + delta.x * x + delta.y * y;
			error += (h - estimated) * (h - estimated);
		}
	}

	return float(sqrt(error / (width * height)));
}

int main(int argc, char *argv[])
{
	if (argc != 5)
	{
		LOGE("Usage: %s input <output-height> <output-normals> <meta-data>\n", argv[0]);
		return 1;
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
	gli::texture2d heights(gli::FORMAT_R32_SFLOAT_PACK32, gli::extent2d(width, height), levels);
	gli::texture2d heights16(gli::FORMAT_R16_SFLOAT_PACK16, gli::extent2d(width, height), levels);
	auto *data = static_cast<float *>(heights.data(0, 0, 0));
	for (int y = 0; y < height; y++)
		for (int x = 0; x < width; x++)
			data[y * width + x] = buffer[4 * (y * width + x)] / 255.0f;

	stbi_image_free(buffer);

	const auto clamp_x = [&](int c, int level = 0) -> int {
		int w = max(width >> level, 1);
		return clamp(c, 0, w - 1);
	};

	const auto clamp_y = [&](int c, int level = 0) -> int {
		int h = max(height >> level, 1);
		return clamp(c, 0, h - 1);
	};

	static const int block_size = 64;
	int blocks_x = width / block_size;
	int blocks_y = height / block_size;

	std::vector<float> biases;
	std::vector<vec2> ranges;
	biases.reserve(blocks_x * blocks_y);
	ranges.reserve(blocks_x * blocks_y);

	unsigned block_index = 0;
	for (int block_y = 0; block_y < blocks_y; block_y++)
	{
		for (int block_x = 0; block_x < blocks_x; block_x++, block_index++)
		{
			int extra_block_x = block_x + 1 < blocks_x ? 1 : 0;
			int extra_block_y = block_y + 1 < blocks_y ? 1 : 0;

			float mean_error = get_plane_error(data + block_x * block_size + block_y * block_size * width,
			                                   block_size + extra_block_x, block_size + extra_block_y, width);

			vec2 range = get_plane_range(data + block_x * block_size + block_y * block_size * width,
			                             block_size + extra_block_x, block_size + extra_block_y, width);

			float bias = -log2(mean_error + 0.00001f) - 5.0f;
			biases.push_back(bias);
			ranges.push_back(range);
		}
	}

	file = fopen(argv[4], "w");
	if (!file)
	{
		LOGE("Failed to write bias data to %s\n", argv[4]);
		return 1;
	}

	fprintf(file, "{ \"bias\" : [\n");
	for (auto &bias : biases)
	{
		fprintf(file, "  %.3f", bias);
		if (&bias != &biases.back())
			fprintf(file, ",\n");
	}
	fprintf(file, "],\n \"range\" : [\n");
	for (auto &range : ranges)
	{
		fprintf(file, "  [%f, %f]", range.x, range.y);
		if (&range != &ranges.back())
			fprintf(file, ",\n");
	}
	fprintf(file, "] }\n");
	fclose(file);

	for (unsigned level = 1; level < levels; level++)
	{
		const float *src = static_cast<float *>(heights.data(0, 0, level - 1));
		float *dst = static_cast<float *>(heights.data(0, 0, level));
		int mip_width = heights.extent(level).x;
		int mip_height = heights.extent(level).y;
		int prev_width = heights.extent(level - 1).x;

		for (int y = 0; y < mip_height; y++)
		{
			for (int x = 0; x < mip_width; x++)
			{
				float h00 = src[clamp_y(2 * y - 1, level - 1) * prev_width + clamp_x(2 * x - 1, level - 1)];
				float h10 = src[clamp_y(2 * y - 1, level - 1) * prev_width + clamp_x(2 * x + 0, level - 1)];
				float h20 = src[clamp_y(2 * y - 1, level - 1) * prev_width + clamp_x(2 * x + 1, level - 1)];
				float h01 = src[clamp_y(2 * y + 0, level - 1) * prev_width + clamp_x(2 * x - 1, level - 1)];
				float h11 = src[clamp_y(2 * y + 0, level - 1) * prev_width + clamp_x(2 * x + 0, level - 1)];
				float h21 = src[clamp_y(2 * y + 0, level - 1) * prev_width + clamp_x(2 * x + 1, level - 1)];
				float h02 = src[clamp_y(2 * y + 1, level - 1) * prev_width + clamp_x(2 * x - 1, level - 1)];
				float h12 = src[clamp_y(2 * y + 1, level - 1) * prev_width + clamp_x(2 * x + 0, level - 1)];
				float h22 = src[clamp_y(2 * y + 1, level - 1) * prev_width + clamp_x(2 * x + 1, level - 1)];
				float h = 0.25f * h11 + 0.125f * (h01 + h10 + h12 + h21) + 0.0625f * (h00 + h20 + h02 + h22);
				dst[y * mip_width + x] = h;
			}
		}
	}

	for (unsigned level = 0; level < levels; level++)
	{
		int mip_width = heights.extent(level).x;
		int mip_height = heights.extent(level).y;
		const float *src = static_cast<float *>(heights.data(0, 0, level));
		uint16_t *dst = static_cast<uint16_t *>(heights16.data(0, 0, level));

		for (int y = 0; y < mip_height; y++)
		{
			for (int x = 0; x < mip_width; x++)
			{
				dst[y * mip_width + x] = packHalf1x16(src[y * mip_width + x]);
			}
		}
	}

	if (!gli::save(heights16, argv[2]))
	{
		LOGE("Failed to save heightmap: %s\n", argv[2]);
		return 1;
	}

	gli::texture2d normals(gli::FORMAT_RGBA32_SFLOAT_PACK32, gli::extent2d(width, height), levels);
	vec4 *normal_data = static_cast<vec4 *>(normals.data(0, 0, 0));

	for (int y = 0; y < height; y++)
	{
		for (int x = 0; x < width; x++)
		{
			float h00 = data[clamp_y(y + 0) * width + clamp_x(x + 0)];
			float h10 = data[clamp_y(y + 0) * width + clamp_x(x + 1)];
			float h01 = data[clamp_y(y + 1) * width + clamp_x(x + 0)];
			float h11 = data[clamp_y(y + 1) * width + clamp_x(x + 1)];

			float x0 = 0.5f * (h00 + h01);
			float x1 = 0.5f * (h10 + h11);
			float y0 = 0.5f * (h00 + h10);
			float y1 = 0.5f * (h01 + h11);

			vec3 tangent_normal = normalize(vec3(x0 - x1, y0 - y1, 1.0f));
			normal_data[y * width + x] = vec4(tangent_normal, 0.0);
		}
	}

	normals = gli::generate_mipmaps(normals, gli::FILTER_LINEAR);
	gli::texture2d normal10(gli::FORMAT_RGB10A2_UNORM_PACK32, gli::extent2d(width, height), levels);

	for (unsigned level = 0; level < normals.levels(); level++)
	{
		int mip_width = normals.extent(level).x;
		int mip_height = normals.extent(level).y;
		uint32_t *dst = static_cast<uint32_t *>(normal10.data(0, 0, level));
		const vec4 *src = static_cast<const vec4 *>(normals.data(0, 0, level));

		for (int i = 0; i < mip_width * mip_height; i++)
		{
			vec3 normalized = normalize(vec3(src[i].x, src[i].y, src[i].z));
			uvec3 quantized = uvec3(round(clamp(1023.0f * (normalized * 0.5f + 0.5f), vec3(0.0f), vec3(1023.0f))));
			dst[i] = (quantized.x << 0) | (quantized.y << 10) | (quantized.z << 20);
		}
	}

	if (!gli::save(normal10, argv[3]))
	{
		LOGE("Failed to save normal map: %s\n", argv[3]);
		return 1;
	}
}