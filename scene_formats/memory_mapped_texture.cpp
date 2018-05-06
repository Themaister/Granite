/* Copyright (c) 2017-2018 Hans-Kristian Arntzen
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

#include "memory_mapped_texture.hpp"
#include <string.h>

using namespace std;

namespace Granite
{
namespace SceneFormats
{

struct MemoryMappedHeader
{
	char magic[16];
	VkImageType type;
	VkFormat format;
	uint32_t width;
	uint32_t height;
	uint32_t depth;
	uint32_t layers;
	uint32_t levels;
	uint32_t flags;
	uint64_t payload_size;
	uint64_t reserved1;
};
static const size_t header_size = 16 + 8 * 4 + 2 * 8;
static_assert(sizeof(MemoryMappedHeader) == header_size, "Header size is not properly packed.");

static const char MAGIC[16] = "GRANITE TEXFMT1";

void MemoryMappedTexture::set_generate_mipmaps_on_load(bool enable)
{
	mipgen_on_load = enable;
}

MemoryMappedTextureFlags MemoryMappedTexture::get_flags() const
{
	MemoryMappedTextureFlags flags = 0;
	if (cube)
		flags |= MEMORY_MAPPED_TEXTURE_CUBE_MAP_COMPATIBLE_BIT;
	if (mipgen_on_load)
		flags |= MEMORY_MAPPED_TEXTURE_GENERATE_MIPMAP_ON_LOAD_BIT;
	return flags;
}

void MemoryMappedTexture::set_1d(VkFormat format, uint32_t width, uint32_t layers, uint32_t levels)
{
	layout.set_1d(format, width, layers, levels);
	cube = false;
}

void MemoryMappedTexture::set_2d(VkFormat format, uint32_t width, uint32_t height, uint32_t layers, uint32_t levels)
{
	layout.set_2d(format, width, height, layers, levels);
	cube = false;
}

void MemoryMappedTexture::set_3d(VkFormat format, uint32_t width, uint32_t height, uint32_t depth, uint32_t levels)
{
	layout.set_3d(format, width, height, depth, levels);
	cube = false;
}

void MemoryMappedTexture::set_cube(VkFormat format, uint32_t size, uint32_t cube_layers, uint32_t levels)
{
	layout.set_2d(format, size, size, cube_layers * 6, levels);
	cube = true;
}

bool MemoryMappedTexture::map_write(unique_ptr<Granite::File> new_file, void *mapped_)
{
	file = move(new_file);
	auto *mapped = static_cast<uint8_t *>(mapped_);

	MemoryMappedHeader header = {};
	memcpy(header.magic, MAGIC, sizeof(MAGIC));
	header.width = layout.get_width();
	header.height = layout.get_height();
	header.depth = layout.get_depth();
	header.flags = cube ? MEMORY_MAPPED_TEXTURE_CUBE_MAP_COMPATIBLE_BIT : 0;
	header.layers = layout.get_layers();
	header.levels = layout.get_levels();
	header.payload_size = layout.get_required_size();
	header.type = layout.get_image_type();
	header.format = layout.get_format();
	memcpy(mapped, &header, sizeof(header));

	layout.set_buffer(mapped + sizeof(header), layout.get_required_size());
	return true;
}

bool MemoryMappedTexture::map_write(const std::string &path)
{
	if (layout.get_required_size() == 0)
		return false;

	auto new_file = Granite::Filesystem::get().open(path, Granite::FileMode::WriteOnly);
	if (!new_file)
		return false;

	void *mapped = new_file->map_write(get_required_size());
	if (!mapped)
		return false;

	return map_write(move(new_file), mapped);
}

struct ScratchFile : Granite::File
{
	ScratchFile(const void *mapped, size_t size)
	{
		data.resize(size);
		if (mapped)
			memcpy(data.data(), mapped, size);
	}

	void *map() override
	{
		return data.data();
	}

	void *map_write(size_t) override
	{
		return nullptr;
	}

	bool reopen() override
	{
		return true;
	}

	void unmap() override
	{
	}

	size_t get_size() override
	{
		return data.size();
	}

	std::vector<uint8_t> data;
};

bool MemoryMappedTexture::map_write_scratch()
{
	if (layout.get_required_size() == 0)
		return false;

	auto new_file = make_unique<ScratchFile>(nullptr, get_required_size());
	if (new_file->get_size() < sizeof(MemoryMappedHeader))
		return false;
	return map_write(move(new_file), new_file->map());
}

size_t MemoryMappedTexture::get_required_size() const
{
	return layout.get_required_size() + sizeof(MemoryMappedHeader);
}

bool MemoryMappedTexture::map_copy(const void *mapped, size_t size)
{
	auto new_file = make_unique<ScratchFile>(mapped, size);
	if (new_file->get_size() < sizeof(MemoryMappedHeader))
		return false;
	return map_read(move(file), new_file->map());
}

bool MemoryMappedTexture::map_read(unique_ptr<Granite::File> new_file, void *mapped)
{
	file = move(new_file);

	auto *header = reinterpret_cast<const MemoryMappedHeader *>(mapped);
	switch (header->type)
	{
	case VK_IMAGE_TYPE_1D:
		layout.set_1d(header->format, header->width, header->layers, header->levels);
		break;

	case VK_IMAGE_TYPE_2D:
		layout.set_2d(header->format, header->width, header->height, header->layers, header->levels);
		break;

	case VK_IMAGE_TYPE_3D:
		layout.set_3d(header->format, header->width, header->height, header->depth, header->levels);
		break;

	default:
		return false;
	}

	cube = (header->flags & MEMORY_MAPPED_TEXTURE_CUBE_MAP_COMPATIBLE_BIT) != 0;
	mipgen_on_load = (header->flags & MEMORY_MAPPED_TEXTURE_GENERATE_MIPMAP_ON_LOAD_BIT) != 0;

	if ((layout.get_required_size() + sizeof(MemoryMappedHeader)) < file->get_size())
		return false;
	if (header->payload_size != layout.get_required_size())
		return false;

	layout.set_buffer(static_cast<uint8_t *>(mapped) + sizeof(MemoryMappedHeader), header->payload_size);
	return true;
}

bool MemoryMappedTexture::map_read(const std::string &path)
{
	auto loaded_file = Granite::Filesystem::get().open(path, Granite::FileMode::ReadOnly);
	if (!loaded_file)
		return false;

	if (loaded_file->get_size() < sizeof(MemoryMappedHeader))
		return false;

	uint8_t *mapped = static_cast<uint8_t *>(loaded_file->map());
	if (!mapped)
		return false;

	return map_read(move(file), mapped);
}

bool MemoryMappedTexture::is_header(const void *mapped, size_t size)
{
	if (size < sizeof(MemoryMappedHeader))
		return false;
	return memcmp(mapped, MAGIC, sizeof(MAGIC)) == 0;
}
}
}
