#include "gli/save.hpp"
#include "gli/texture2d.hpp"
#include "gli/generate_mipmaps.hpp"
#include "stb_image.h"
#include "util.hpp"

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

int main(int argc, char *argv[])
{
	if (argc != 3)
	{
		LOGE("Usage: %s output input\n", argv[0]);
		return 1;
	}

	int width, height;
	int components;

	FILE *file = fopen(argv[2], "rb");
	if (!file)
	{
		LOGE("Failed to load PNG: %s\n", argv[2]);
		return 1;
	}

	auto *buffer = stbi_load_from_file(file, &width, &height, &components, 4);
	fclose(file);

	unsigned levels = num_miplevels(width, height);

	if (width != height)
	{
		LOGE("Chunky textures must be square.\n");
		return 1;
	}

	if (width & (width - 1))
	{
		LOGE("Chunky textures must be POT.\n");
		return 1;
	}

	auto texture = gli::texture2d(gli::FORMAT_RGBA8_UNORM_PACK8, gli::texture2d::extent_type(width, height), levels);

	auto *data = texture.data(0, 0, 0);
	memcpy(data, buffer, width * height * 4);
	stbi_image_free(buffer);

	texture = gli::generate_mipmaps(texture, gli::filter::FILTER_LINEAR);

	if (!gli::save_ktx(texture, argv[1]))
	{
		LOGE("Failed to save KTX file: %s\n", argv[1]);
		return 1;
	}

	return 0;
}