/* Copyright (c) 2017-2024 Hans-Kristian Arntzen
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
#include "global_managers.hpp"
#include "event.hpp"
#include "descriptor_set.hpp"
#include "asset_manager.hpp"
#include "application_wsi_events.hpp"
#include "hashmap.hpp"
#include <assert.h>
#include <stddef.h>

namespace Vulkan
{
class CommandBuffer;
};

namespace Granite
{
static constexpr uint32_t MaterialPayloadSize = 32;

struct MaterialOffsets
{
	uint16_t texture_offset;
	uint16_t uniform_offset;
};

class MaterialManager final : public MaterialManagerInterface, public EventHandler
{
public:
	MaterialManager();
	void iterate(AssetManagerInterface *iface) override;

	MaterialOffsets register_material(const AssetID *assets, unsigned count,
	                                  const void *payload_data, size_t payload_size,
	                                  bool force_unique = false /* can be used for "animating" material properties */);

	template <typename T>
	inline T &get_material_payload(const MaterialOffsets &offsets)
	{
		static_assert(sizeof(T) <= MaterialPayloadSize, "sizeof(T) is too large.");
		static_assert(alignof(T) <= alignof(max_align_t), "alignof(T) is too large.");
		assert(offsets.uniform_offset < material_payload.size());
		return reinterpret_cast<T &>(material_payload[offsets.uniform_offset]);
	}

	const AssetID *get_asset_ids(const MaterialOffsets &offsets)
	{
		assert(offsets.texture_offset < bindless_texture_assets.size());
		return bindless_texture_assets.data() + offsets.texture_offset;
	}

	void set_bindless(Vulkan::CommandBuffer &cmd, unsigned set_index);
	void set_material_payloads(Vulkan::CommandBuffer &cmd, unsigned set_index, unsigned binding);

private:
	Vulkan::BindlessAllocator allocator;
	Vulkan::Device *device = nullptr;

	void on_device_created(const Vulkan::DeviceCreatedEvent &e);
	void on_device_destroyed(const Vulkan::DeviceCreatedEvent &e);

	struct MaterialRawPayload
	{
		MaterialRawPayload() {}
		uint32_t raw[MaterialPayloadSize / sizeof(uint32_t)];
	};

	std::vector<MaterialRawPayload> material_payload;
	std::vector<AssetID> bindless_texture_assets;
	struct MaterialGroup : Util::IntrusiveHashMapEnabled<MaterialGroup>
	{
		MaterialOffsets offsets;
	};
	Util::IntrusiveHashMap<MaterialGroup> material;
	VkDescriptorSet vk_set = VK_NULL_HANDLE;
};
}
