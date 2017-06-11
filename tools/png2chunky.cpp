#include "gli/save.hpp"
#include "gli/generate_mipmaps.hpp"
#include "stb_image.h"
#include "util.hpp"
#include <stdio.h>

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

struct ETC2Color
{
	uint32_t header;
	uint32_t pixels;
};

struct ETC2Block
{
	ETC2Color color;
};

static inline uint32_t flip_bytes(uint32_t v)
{
	return
		((v & 0xffu) << 24) |
		((v & 0xff00u) << 8) |
		((v & 0xff0000u) >> 8) |
		((v & 0xff000000u) >> 24);
}

static ETC2Block splat_etc2_block(const uint8_t *color)
{
	ETC2Block block = {};

	auto r = color[0] >> 3;
	auto g = color[1] >> 3;
	auto b = color[2] >> 3;
	bool opaque = color[3] >= 128;

	block.color.header = (r << (59 - 32)) | (g << (51 - 32)) | (b << (43 - 32));
	block.color.pixels = opaque ? 0xffffu : 0xffff0000u;

	block.color.header = flip_bytes(block.color.header);
	block.color.pixels = flip_bytes(block.color.pixels);

	return block;
}

int main(int argc, char *argv[])
{
	if (argc != 3)
	{
		LOGE("Usage: %s file.png file.ktx\n", argv[0]);
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

	gli::texture2d texture(gli::FORMAT_RGBA8_SRGB_PACK8, gli::texture2d::extent_type(width, height), levels);
	gli::texture2d texture_etc2(gli::FORMAT_RGBA_ETC2_SRGB_BLOCK8, gli::texture2d::extent_type(width * 4, height * 4), levels);

	auto *data = texture.data(0, 0, 0);
	memcpy(data, buffer, width * height * 4);
	stbi_image_free(buffer);

	texture = gli::generate_mipmaps(texture, gli::filter::FILTER_LINEAR);

	for (unsigned level = 0; level < levels; level++)
	{
		auto *src = static_cast<const uint8_t *>(texture[level].data());
		auto *dst = static_cast<ETC2Block *>(texture_etc2[level].data());

		unsigned mip_width = texture[level].extent().x;
		unsigned mip_height = texture[level].extent().y;

		for (unsigned y = 0; y < mip_height; y++)
			for (unsigned x = 0; x < mip_width; x++, src += 4, dst++)
				*dst = splat_etc2_block(src);
	}

	if (!gli::save_ktx(texture_etc2, argv[2]))
	{
		LOGE("Failed to save KTX file: %s\n", argv[2]);
		return 1;
	}

	return 0;
}