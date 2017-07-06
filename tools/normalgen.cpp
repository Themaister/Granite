#include "gli/load.hpp"
#include "gli/save.hpp"
#include "gli/texture2d.hpp"
#include "gli/generate_mipmaps.hpp"
#include "util.hpp"
#include "math.hpp"

using namespace glm;

int main(int argc, char *argv[])
{
	if (argc != 3)
	{
		LOGE("Usage: %s input output\n", argv[0]);
	}

	gli::texture input = gli::load(argv[1]);
	if (input.empty())
	{
		LOGE("Failed to load texture: %s\n", argv[1]);
		return 1;
	}

	gli::texture2d normals(gli::FORMAT_RGBA32_SFLOAT_PACK32, gli::extent2d(input.extent().x, input.extent().y), input.levels());
	gli::texture2d normal10(gli::FORMAT_RGB10A2_UNORM_PACK32, gli::extent2d(input.extent().x, input.extent().y), input.levels());

	int width = input.extent().x;
	int height = input.extent().y;

	const auto clamp_x = [&](int c, int level = 0) -> int {
		int w = max(width >> level, 1);
		return clamp(c, 0, w - 1);
	};

	const auto clamp_y = [&](int c, int level = 0) -> int {
		int h = max(height >> level, 1);
		return clamp(c, 0, h - 1);
	};

	using Pixel = glm::tvec4<uint8_t>;
	const Pixel *data = static_cast<const Pixel *>(input.data());
	vec4 *normal_data = static_cast<vec4 *>(normals.data());

	for (int y = 0; y < height; y++)
	{
		for (int x = 0; x < width; x++)
		{
			float x0 = data[clamp_y(y) * width + clamp_x(x - 1)].x * (1.0f / 255.0f);
			float x1 = data[clamp_y(y) * width + clamp_x(x + 1)].x * (1.0f / 255.0f);
			float y0 = data[clamp_y(y - 1) * width + clamp_x(x)].x * (1.0f / 255.0f);
			float y1 = data[clamp_y(y + 1) * width + clamp_x(x)].x * (1.0f / 255.0f);

			vec3 tangent_normal = normalize(vec3(x0 - x1, y0 - y1, 1.0f));
			normal_data[y * width + x] = vec4(tangent_normal, 0.0);
		}
	}

	normals = gli::generate_mipmaps(normals, gli::FILTER_LINEAR);

	const float normal_scale_x = 1.0f;
	const float normal_scale_y = 1.0f;
	for (unsigned level = 0; level < normals.levels(); level++)
	{
		int mip_width = normals.extent(level).x;
		int mip_height = normals.extent(level).y;
		uint32_t *dst = static_cast<uint32_t *>(normal10.data(0, 0, level));
		const vec4 *src = static_cast<const vec4 *>(normals.data(0, 0, level));

		for (int i = 0; i < mip_width * mip_height; i++)
		{
			vec3 normalized = normalize(vec3(normal_scale_x * src[i].x, normal_scale_y * src[i].y, src[i].z));
			uvec3 quantized = uvec3(round(clamp(1023.0f * (normalized * 0.5f + 0.5f), vec3(0.0f), vec3(1023.0f))));
			dst[i] = (quantized.x << 0) | (quantized.y << 10) | (quantized.z << 20);
		}
	}

	if (!gli::save(normal10, argv[2]))
	{
		LOGE("Failed to store normals to: %s\n", argv[2]);
		return 1;
	}
}