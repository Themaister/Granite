/* Copyright (c) 2017-2020 Hans-Kristian Arntzen
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

#pragma once

#include "volatile_source.hpp"
#include "image.hpp"
#include "async_object_sink.hpp"

namespace Granite
{
namespace SceneFormats
{
class MemoryMappedTexture;
}
}

namespace Vulkan
{
class Texture : public Util::VolatileSource<Texture>,
                public Util::IntrusiveHashMapEnabled<Texture>
{
public:
	friend class Util::VolatileSource<Texture>;
	friend class TextureManager;
	friend class Util::ObjectPool<Texture>;

	bool init_texture();
	void set_path(const std::string &path);
	Image *get_image();
	void replace_image(ImageHandle handle);
	void set_enable_notification(bool enable);

private:
	Texture(Device *device, const std::string &path, VkFormat format = VK_FORMAT_UNDEFINED,
	        const VkComponentMapping &swizzle = {
			        VK_COMPONENT_SWIZZLE_R,
			        VK_COMPONENT_SWIZZLE_G,
			        VK_COMPONENT_SWIZZLE_B,
			        VK_COMPONENT_SWIZZLE_A });

	explicit Texture(Device *device);

	Device *device;
	Util::AsyncObjectSink<ImageHandle> handle;
	VkFormat format;
	VkComponentMapping swizzle;
	void update_other(const void *data, size_t size);
	void update_gtx(std::unique_ptr<Granite::File> file, void *mapped);
	void update_gtx(const Granite::SceneFormats::MemoryMappedTexture &texture);
	void update_checkerboard();

	void load();
	void unload();
	void update(std::unique_ptr<Granite::File> file);
	bool enable_notification = true;
};

class TextureManager
{
public:
	TextureManager(Device *device);
	Texture *request_texture(const std::string &path, VkFormat format = VK_FORMAT_UNDEFINED,
	                         const VkComponentMapping &swizzle = {
			                         VK_COMPONENT_SWIZZLE_R,
			                         VK_COMPONENT_SWIZZLE_G,
			                         VK_COMPONENT_SWIZZLE_B,
			                         VK_COMPONENT_SWIZZLE_A });

	Texture *register_deferred_texture(const std::string &path);

	void register_texture_update_notification(const std::string &modified_path,
	                                          std::function<void (Texture &)> func);

	void notify_updated_texture(const std::string &path, Vulkan::Texture &texture);

private:
	Device *device;

	VulkanCache<Texture> textures;
	VulkanCache<Texture> deferred_textures;

	std::unordered_map<std::string, std::vector<std::function<void (Texture &)>>> notifications;
};
}
