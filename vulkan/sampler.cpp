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

#include "sampler.hpp"
#include "device.hpp"

namespace Vulkan
{
Sampler::Sampler(Device *device_, VkSampler sampler_, const SamplerCreateInfo &info, bool immutable_)
    : Cookie(device_)
    , device(device_)
    , sampler(sampler_)
    , create_info(info)
    , immutable(immutable_)
{
	if (device->get_device_features().supports_descriptor_buffer)
	{
		payload = device->managers.descriptor_buffer.alloc_sampler();
		VkDescriptorGetInfoEXT get_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT };
		get_info.type = VK_DESCRIPTOR_TYPE_SAMPLER;
		get_info.data.pSampler = &sampler;
		device->get_device_table().vkGetDescriptorEXT(
				device->get_device(),
				&get_info,
				device->get_device_features().descriptor_buffer_properties.samplerDescriptorSize,
				payload.ptr);
	}
}

Sampler::~Sampler()
{
	if (sampler)
	{
		if (immutable)
			device->get_device_table().vkDestroySampler(device->get_device(), sampler, nullptr);
		else if (internal_sync)
			device->destroy_sampler_nolock(sampler);
		else
			device->destroy_sampler(sampler);
	}

	if (payload)
	{
		if (internal_sync)
			device->free_cached_descriptor_payload_nolock(payload);
		else
			device->free_cached_descriptor_payload(payload);
	}
}

void SamplerDeleter::operator()(Sampler *sampler)
{
	sampler->device->handle_pool.samplers.free(sampler);
}

SamplerCreateInfo Sampler::fill_sampler_info(const VkSamplerCreateInfo &info)
{
	SamplerCreateInfo sampler_info = {};

	sampler_info.mag_filter = info.magFilter;
	sampler_info.min_filter = info.minFilter;
	sampler_info.mipmap_mode = info.mipmapMode;
	sampler_info.address_mode_u = info.addressModeU;
	sampler_info.address_mode_v = info.addressModeV;
	sampler_info.address_mode_w = info.addressModeW;
	sampler_info.mip_lod_bias = info.mipLodBias;
	sampler_info.anisotropy_enable = info.anisotropyEnable;
	sampler_info.max_anisotropy = info.maxAnisotropy;
	sampler_info.compare_enable = info.compareEnable;
	sampler_info.compare_op = info.compareOp;
	sampler_info.min_lod = info.minLod;
	sampler_info.max_lod = info.maxLod;
	sampler_info.border_color = info.borderColor;
	sampler_info.unnormalized_coordinates = info.unnormalizedCoordinates;
	return sampler_info;
}

VkSamplerCreateInfo Sampler::fill_vk_sampler_info(const SamplerCreateInfo &sampler_info)
{
	VkSamplerCreateInfo info = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };

	info.magFilter = sampler_info.mag_filter;
	info.minFilter = sampler_info.min_filter;
	info.mipmapMode = sampler_info.mipmap_mode;
	info.addressModeU = sampler_info.address_mode_u;
	info.addressModeV = sampler_info.address_mode_v;
	info.addressModeW = sampler_info.address_mode_w;
	info.mipLodBias = sampler_info.mip_lod_bias;
	info.anisotropyEnable = sampler_info.anisotropy_enable;
	info.maxAnisotropy = sampler_info.max_anisotropy;
	info.compareEnable = sampler_info.compare_enable;
	info.compareOp = sampler_info.compare_op;
	info.minLod = sampler_info.min_lod;
	info.maxLod = sampler_info.max_lod;
	info.borderColor = sampler_info.border_color;
	info.unnormalizedCoordinates = sampler_info.unnormalized_coordinates;
	return info;
}

ImmutableSampler::ImmutableSampler(Util::Hash hash, Device *device_, const SamplerCreateInfo &sampler_info,
                                   const ImmutableYcbcrConversion *ycbcr_)
	: HashedObject<ImmutableSampler>(hash), device(device_), ycbcr(ycbcr_)
{
	VkSamplerYcbcrConversionInfo conv_info = { VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO };
	auto info = Sampler::fill_vk_sampler_info(sampler_info);

	if (ycbcr)
	{
		conv_info.conversion = ycbcr->get_conversion();
		info.pNext = &conv_info;
	}

	VkSampler vk_sampler = VK_NULL_HANDLE;
	if (device->get_device_table().vkCreateSampler(device->get_device(), &info, nullptr, &vk_sampler) != VK_SUCCESS)
		LOGE("Failed to create sampler.\n");

#ifdef GRANITE_VULKAN_FOSSILIZE
	device->register_sampler(vk_sampler, hash, info);
#endif

	sampler = SamplerHandle(device->handle_pool.samplers.allocate(device, vk_sampler, sampler_info, true));
}

ImmutableYcbcrConversion::ImmutableYcbcrConversion(Util::Hash hash, Device *device_,
                                                   const VkSamplerYcbcrConversionCreateInfo &info)
	: HashedObject<ImmutableYcbcrConversion>(hash), device(device_)
{
	if (device->get_device_features().vk11_features.samplerYcbcrConversion)
	{
		if (device->get_device_table().vkCreateSamplerYcbcrConversion(device->get_device(), &info, nullptr,
		                                                              &conversion) != VK_SUCCESS)
		{
			LOGE("Failed to create YCbCr conversion.\n");
		}
		else
		{
#ifdef GRANITE_VULKAN_FOSSILIZE
			device->register_sampler_ycbcr_conversion(conversion, info);
#endif
		}
	}
	else
		LOGE("Ycbcr conversion is not supported on this device.\n");
}

ImmutableYcbcrConversion::~ImmutableYcbcrConversion()
{
	if (conversion)
		device->get_device_table().vkDestroySamplerYcbcrConversion(device->get_device(), conversion, nullptr);
}
}
