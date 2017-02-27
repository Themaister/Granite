#include "texture_manager.hpp"
#include "device.hpp"
#include "stb_image.h"

using namespace std;

namespace Vulkan
{
Texture::Texture(Device *device, const std::string &path, VkFormat format)
	: VolatileSource(path), device(device), format(format)
{
	init();
}

void Texture::update(const void *data, size_t size)
{
	int width, height;
	int components;
	auto *buffer = stbi_load_from_memory(static_cast<const stbi_uc *>(data), size, &width, &height, &components, 4);

	if (!buffer)
		throw runtime_error("stbi_load_from_memory failed.");

	handle.reset();
	auto desc = ImageCreateInfo::immutable_2d_image(unsigned(width), unsigned(height),
	                                                format != VK_FORMAT_UNDEFINED ? format : VK_FORMAT_R8G8B8A8_SRGB,
	                                                true);

	ImageInitialData initial = {};
	initial.data = buffer;
	handle = device->create_image(desc, &initial);
	stbi_image_free(buffer);
}

TextureManager::TextureManager(Device *device)
	: device(device)
{
}

Texture *TextureManager::request_texture(const std::string &path)
{
	auto itr = textures.find(path);
	if (itr == end(textures))
	{
		unique_ptr<Texture> texture(new Texture(device, path));
		auto *ret = texture.get();
		textures[path] = move(texture);
		return ret;
	}
	else
		return itr->second.get();
}

}