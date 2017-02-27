#pragma once

#include "volatile_source.hpp"
#include "image.hpp"

namespace Vulkan
{
class Texture : public Util::VolatileSource<Texture>
{
public:
	Texture(Device *device, const std::string &path, VkFormat format = VK_FORMAT_UNDEFINED);

	ImageHandle get_image()
	{
		return handle;
	}

	void update(const void *data, size_t size);

private:
	Device *device;
	ImageHandle handle;
	VkFormat format;
};

class TextureManager
{
public:
	TextureManager(Device *device);
	Texture *request_texture(const std::string &path);

private:
	Device *device;
	std::unordered_map<std::string, std::unique_ptr<Texture>> textures;
};
}