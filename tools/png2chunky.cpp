#include "gli/save.hpp"
#include "gli/generate_mipmaps.hpp"
#include "stb_image.h"
#include "util.hpp"
#include <stdio.h>
#include <algorithm>
#include "math.hpp"
#include "tool_util.hpp"

using namespace std;
using namespace Granite;
using namespace Util;


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

struct ASTCBlock
{
	uint8_t data[16];
};

static void write_bits(uint8_t *buffer, uint32_t v, uint32_t count, uint32_t offset)
{
	for (uint32_t i = 0; i < count; i++)
	{
		uint8_t src = uint8_t((v >> i) & 1);
		uint32_t target_byte = (i + offset) >> 3;
		uint32_t target_bit = (i + offset) & 7;
		buffer[target_byte] &= ~(1 << target_bit);
		buffer[target_byte] |= src << target_bit;
	}
}

static ASTCBlock splat_astc_block(const uint8_t *x0, const uint8_t *x1, const uint8_t *x2, const uint8_t *x3)
{
	ASTCBlock astc = {};

	uint16_t block_mode = 0;

	// 2 bit per weight. Could use 1, but 16 weight bits per block is illegal :(
	block_mode |= 2;

	// 4x4 weight mode.
	block_mode |= 2 << 5;

	// Block mode, 4x4 weights.
	write_bits(astc.data, block_mode, 11, 0);

	// Partition count - 1
	write_bits(astc.data, 1, 1, 11);

	// Partition index (top half is 1, bottom half is 0).
	write_bits(astc.data, 17, 10, 13);

	// CEM (LDR RGBA direct)
	write_bits(astc.data, 12, 4, 25);

	// This will result in RGBA4444 with 4 endpoints.

	struct Color
	{
		uint8_t r, g, b, a;
	};

	Color a = {uint8_t(x0[0] >> 4), uint8_t(x0[1] >> 4), uint8_t(x0[2] >> 4), uint8_t(x0[3] >= 128 ? 15 : 0)};
	Color b = {uint8_t(x1[0] >> 4), uint8_t(x1[1] >> 4), uint8_t(x1[2] >> 4), uint8_t(x1[3] >= 128 ? 15 : 0)};
	Color c = {uint8_t(x2[0] >> 4), uint8_t(x2[1] >> 4), uint8_t(x2[2] >> 4), uint8_t(x2[3] >= 128 ? 15 : 0)};
	Color d = {uint8_t(x3[0] >> 4), uint8_t(x3[1] >> 4), uint8_t(x3[2] >> 4), uint8_t(x3[3] >= 128 ? 15 : 0)};

	// Mask to transparent black for now.
	if (a.a == 0)
	{
		a.r = 0;
		a.g = 0;
		a.b = 0;
	}

	if (b.a == 0)
	{
		b.r = 0;
		b.g = 0;
		b.b = 0;
	}

	if (c.a == 0)
	{
		c.r = 0;
		c.g = 0;
		c.b = 0;
	}

	if (d.a == 0)
	{
		d.r = 0;
		d.g = 0;
		d.b = 0;
	}

	// Swap color order to avoid blueshift.
	if (a.r + a.g + a.b > b.r + b.g + b.b)
	{
		swap(a.r, b.r);
		swap(a.g, b.g);
		swap(a.b, b.b);
		swap(a.a, b.a);
		astc.data[15] = 0xf0;
		astc.data[14] = 0xf0;
	}
	else
	{
		astc.data[15] = 0x0f;
		astc.data[14] = 0x0f;
	}

	if (c.r + c.g + c.b > d.r + d.g + d.b)
	{
		swap(c.r, d.r);
		swap(c.g, d.g);
		swap(c.b, d.b);
		swap(c.a, d.a);
		astc.data[13] = 0xf0;
		astc.data[12] = 0xf0;
	}
	else
	{
		astc.data[13] = 0x0f;
		astc.data[12] = 0x0f;
	}

	// Write colors
	const uint8_t colors[] = {
		// Partition 0
		c.r, d.r,
		c.g, d.g,
		c.b, d.b,
		c.a, d.a,

		// Partition 1
		a.r, b.r,
		a.g, b.g,
		a.b, b.b,
		a.a, b.a,
	};

	for (unsigned i = 0; i < 16; i++)
	{
		write_bits(astc.data, colors[i], 4, 29 + 4 * i);
	}

	return astc;
}

static void fixup_alpha_test(gli::texture2d &texture, unsigned level)
{
	using Pixel = glm::tvec4<uint8_t>;
	using PixelRGB = glm::tvec3<uint8_t>;

	int width = texture[level].extent().x;
	int height = texture[level].extent().y;
	gli::image image(gli::FORMAT_RGBA8_SRGB_PACK8, gli::extent3d(width, height, 1));

	const auto coord = [=](int x, int y) {
		return gli::extent2d(clamp(x, 0, width - 1), clamp(y, 0, height - 1));
	};

	const auto coord3d = [=](int x, int y) {
		return gli::extent3d(clamp(x, 0, width - 1), clamp(y, 0, height - 1), 0);
	};

	for (int y = 0; y < height; y++)
	{
		for (int x = 0; x < width; x++)
		{
			Pixel s11 = texture.load<Pixel>(coord(x + 0, y + 0), level);
			if (s11.a >= 128) // Opaque pixel
			{
				Pixel s((s11.r * s11.a) / 255, (s11.g * s11.a) / 255, (s11.b * s11.a) / 255, s11.a);
				image.store<Pixel>(coord3d(x, y), s11);
			}
			else
			{
				ivec3 rgb(0, 0, 0);
				int weights = 0;

				// Transparent, inherent weighted average from neighbors.
				Pixel s00 = texture.load<Pixel>(coord(x - 1, y - 1), level);
				Pixel s10 = texture.load<Pixel>(coord(x + 0, y - 1), level);
				Pixel s20 = texture.load<Pixel>(coord(x + 1, y - 1), level);
				Pixel s01 = texture.load<Pixel>(coord(x - 1, y + 0), level);
				Pixel s21 = texture.load<Pixel>(coord(x + 1, y + 0), level);
				Pixel s02 = texture.load<Pixel>(coord(x - 1, y + 1), level);
				Pixel s12 = texture.load<Pixel>(coord(x + 0, y + 1), level);
				Pixel s22 = texture.load<Pixel>(coord(x + 1, y + 1), level);

				const auto accum = [&](const Pixel &pix) {
					rgb += ivec3(pix.r, pix.g, pix.b) * ivec3(pix.a);
					weights += pix.a;
				};

				accum(s00);
				accum(s10);
				accum(s20);
				accum(s01);
				accum(s21);
				accum(s02);
				accum(s12);
				accum(s22);

				if (weights)
				{
					rgb.r = uint8_t(rgb.r / weights);
					rgb.g = uint8_t(rgb.g / weights);
					rgb.b = uint8_t(rgb.b / weights);
					image.store<Pixel>(coord3d(x, y), Pixel(rgb, s11.a));
				}
				else
					image.store<Pixel>(coord3d(x, y), s11);
			}
		}
	}

	assert(image.size() == texture[level].size());
	memcpy(texture[level].data(), image.data(), image.size());
}

int main(int argc, char *argv[])
{
	if (argc < 3)
	{
		LOGE("Usage: %s file.png file.ktx\n", argv[0]);
		return 1;
	}

	bool astc = false;
	bool etc2 = false;
	if (argc >= 4)
	{
		astc = strcmp(argv[3], "--astc") == 0;
		etc2 = strcmp(argv[3], "--etc2") == 0;
		if (!astc && !etc2)
		{
			LOGE("Invalid argument: %s\n", argv[3]);
			return 1;
		}
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

	if (astc || etc2)
	{
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
	}

	gli::texture2d texture(gli::FORMAT_RGBA8_SRGB_PACK8, gli::texture2d::extent_type(width, height), levels);

	gli::texture2d texture_compressed;
	if (astc)
		texture_compressed = gli::texture2d(gli::FORMAT_RGBA_ASTC_4X4_SRGB_BLOCK16, gli::texture2d::extent_type(width * 2, height * 2), levels);
	else if (etc2)
		texture_compressed = gli::texture2d(gli::FORMAT_RGBA_ETC2_SRGB_BLOCK8, gli::texture2d::extent_type(width * 4, height * 4), levels);
	else
		texture_compressed = gli::texture2d(gli::FORMAT_RGBA8_SRGB_PACK8, gli::texture2d::extent_type(width * 4, height * 4), levels + 2);

	auto *data = texture.data(0, 0, 0);
	memcpy(data, buffer, width * height * 4);
	stbi_image_free(buffer);

	texture = gli::generate_mipmaps(texture, gli::filter::FILTER_LINEAR);

	if (astc)
	{
		for (unsigned level = 0; level < levels; level++)
		{
			auto *src = static_cast<const uint8_t *>(texture[level].data());
			auto *dst = static_cast<ASTCBlock *>(texture_compressed[level].data());

			unsigned mip_width = texture[level].extent().x;
			unsigned mip_height = texture[level].extent().y;

			if (mip_width == 1 && mip_height == 1)
				*dst = splat_astc_block(src, src, src, src);
			else
			{
				for (unsigned y = 0; y < mip_height; y += 2, src += 4 * mip_width)
					for (unsigned x = 0; x < mip_width; x += 2, src += 8, dst++)
						*dst = splat_astc_block(src, src + 4, src + 4 * mip_width, src + 4 * mip_width + 4);
			}
		}
	}
	else if (etc2)
	{
		for (unsigned level = 0; level < levels; level++)
		{
			auto *src = static_cast<const uint8_t *>(texture[level].data());
			auto *dst = static_cast<ETC2Block *>(texture_compressed[level].data());

			unsigned mip_width = texture[level].extent().x;
			unsigned mip_height = texture[level].extent().y;

			for (unsigned y = 0; y < mip_height; y++)
				for (unsigned x = 0; x < mip_width; x++, src += 4, dst++)
					*dst = splat_etc2_block(src);
		}
	}
	else
	{
		for (unsigned level = 0; level < levels; level++)
		{
			auto *src = static_cast<const uint8_t *>(texture[level].data());
			auto *dst = static_cast<ETC2Block *>(texture_compressed[level + 2].data());
			memcpy(dst, src, texture[level].size());
		}

		auto *src = static_cast<const uint32_t *>(texture[0].data());

		auto *dst0 = static_cast<uint32_t *>(texture_compressed[0].data());
		unsigned mip_width0 = texture_compressed[0].extent().x;
		unsigned mip_height0 = texture_compressed[0].extent().y;

		auto *dst1 = static_cast<uint32_t *>(texture_compressed[1].data());
		unsigned mip_width1 = texture_compressed[1].extent().x;
		unsigned mip_height1 = texture_compressed[1].extent().y;

		for (unsigned y = 0; y < mip_height0; y++)
			for (unsigned x = 0; x < mip_width0; x++)
				dst0[y * mip_width0 + x] = src[(y >> 2) * (mip_width0 >> 2) + (x >> 2)];

		for (unsigned y = 0; y < mip_height1; y++)
			for (unsigned x = 0; x < mip_width1; x++)
				dst1[y * mip_width1 + x] = src[(y >> 1) * (mip_width1 >> 1) + (x >> 1)];

		for (unsigned level = 0; level < levels + 2; level++)
			fixup_alpha_test(texture_compressed, level);
	}

	if (!gli::save_ktx(texture_compressed, argv[2]))
	{
		LOGE("Failed to save KTX file: %s\n", argv[2]);
		return 1;
	}

	return 0;
}