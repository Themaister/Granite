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

#include "material_manager.hpp"
#include "device.hpp"
#include "limits.hpp"
#include "hashmap.hpp"

namespace Granite
{
MaterialManager::MaterialManager()
{
	EVENT_MANAGER_REGISTER_LATCH(MaterialManager, on_device_created, on_device_destroyed, Vulkan::DeviceCreatedEvent);
	material_payload.reserve(Vulkan::VULKAN_MAX_UBO_SIZE / MaterialPayloadSize);
	bindless_texture_assets.reserve(Vulkan::VULKAN_NUM_BINDINGS_BINDLESS_VARYING);
	allocator.reserve_max_resources_per_pool(256, 8 * Vulkan::VULKAN_NUM_BINDINGS_BINDLESS_VARYING);
	allocator.set_bindless_resource_type(Vulkan::BindlessResourceType::Image);
}

MaterialOffsets MaterialManager::register_material(
		const AssetID *assets, unsigned count, const void *payload_data,
		size_t payload_size, bool force_unique)
{
	Util::Hasher hasher;
	if (!force_unique)
	{
		for (unsigned i = 0; i < count; i++)
			hasher.u32(assets[i].id);
		hasher.data(static_cast<const uint8_t *>(payload_data), payload_size);
	}
	auto *group = force_unique ? nullptr : material.find(hasher.get());

	if (group)
		return group->offsets;

	if (bindless_texture_assets.size() + count > Vulkan::VULKAN_NUM_BINDINGS_BINDLESS_VARYING)
		LOGE("Exceeding number of bindless slots.\n");
	if (payload_size && material_payload.size() >= Vulkan::VULKAN_MAX_UBO_SIZE / MaterialPayloadSize)
		LOGE("Exceeding number of material payload slots.\n");

	MaterialOffsets offsets = {};
	offsets.texture_offset = uint32_t(bindless_texture_assets.size());
	offsets.uniform_offset = payload_size ? uint32_t(material_payload.size()) : UINT16_MAX;

	if (!force_unique)
	{
		group = material.allocate();
		group->offsets = offsets;
		material.insert_replace(hasher.get(), group);
	}

	bindless_texture_assets.insert(bindless_texture_assets.end(), assets, assets + count);

	if (payload_size)
	{
		material_payload.emplace_back();
		memcpy(material_payload.back().raw, payload_data, payload_size);
	}

	return offsets;
}

void MaterialManager::set_material_payloads(Vulkan::CommandBuffer &cmd, unsigned set_index, unsigned binding)
{
	if (material_payload.empty())
	{
		void *data = cmd.allocate_constant_data(set_index, binding, sizeof(MaterialRawPayload));
		memset(data, 0, sizeof(MaterialRawPayload));
	}
	else
	{
		void *data = cmd.allocate_constant_data(set_index, binding, material_payload.size() * sizeof(MaterialRawPayload));
		memcpy(data, material_payload.data(), material_payload.size() * sizeof(MaterialRawPayload));
	}
}

void MaterialManager::set_bindless(Vulkan::CommandBuffer &cmd, unsigned set_index)
{
	if (vk_set == VK_NULL_HANDLE)
	{
		allocator.begin();
		vk_set = allocator.commit(*device);
	}

	cmd.set_bindless(set_index, vk_set);
}

void MaterialManager::iterate(AssetManagerInterface *)
{
	auto &res = device->get_resource_manager();
	if (device->get_device_features().vk12_features.descriptorIndexing)
	{
		allocator.begin();
		for (auto &id: bindless_texture_assets)
			allocator.push(*res.get_image_view(id));
		vk_set = allocator.commit(*device);
	}
}

void MaterialManager::on_device_created(const Vulkan::DeviceCreatedEvent &e)
{
	device = &e.get_device();
}

void MaterialManager::on_device_destroyed(const Vulkan::DeviceCreatedEvent &)
{
	allocator.reset();
	device = nullptr;
}
}
