/* Copyright (c) 2017-2022 Hans-Kristian Arntzen
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

#include "image.hpp"
#include "asset_manager.hpp"
#include <mutex>
#include <condition_variable>

namespace Vulkan
{
class MemoryMappedTexture;

class TextureManager : private Granite::AssetInstantiatorInterface
{
public:
	explicit TextureManager(Device *device);
	void init();

	inline const Vulkan::ImageView *get_image_view(Granite::ImageAssetID id) const
	{
		if (id && id.id < views.size())
			return views[id.id];
		else
			return nullptr;
	}

	const Vulkan::ImageView *get_image_view_blocking(Granite::ImageAssetID id);

private:
	Device *device;
	Granite::AssetManager *manager = nullptr;

	void latch_handles() override;
	uint64_t estimate_cost_image_resource(Granite::ImageAssetID id, Granite::File &file) override;
	void instantiate_image_resource(Granite::AssetManager &manager, Granite::ImageAssetID id, Granite::File &file) override;
	void release_image_resource(Granite::ImageAssetID id) override;
	void set_id_bounds(uint32_t bound) override;
	void set_image_class(Granite::ImageAssetID id, Granite::ImageClass image_class) override;

	struct Texture
	{
		ImageHandle image;
		Granite::ImageClass image_class = Granite::ImageClass::Zeroable;
	};

	std::mutex lock;
	std::condition_variable cond;

	std::vector<Texture> textures;
	std::vector<const ImageView *> views;
	std::vector<Granite::ImageAssetID> updates;

	ImageHandle fallback_color;
	ImageHandle fallback_normal;
	ImageHandle fallback_zero;
	ImageHandle fallback_pbr;

	ImageHandle create_gtx(Granite::FileMappingHandle mapping, Granite::ImageAssetID id);
	ImageHandle create_gtx(const MemoryMappedTexture &mapping, Granite::ImageAssetID id);
	ImageHandle create_other(const Granite::FileMapping &mapping, Granite::ImageClass image_class, Granite::ImageAssetID id);
	const ImageHandle &get_fallback_image(Granite::ImageClass image_class);
};
}
