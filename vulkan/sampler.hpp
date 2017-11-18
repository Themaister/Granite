/* Copyright (c) 2017 Hans-Kristian Arntzen
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

#include "cookie.hpp"
#include "intrusive.hpp"
#include "vulkan.hpp"

namespace Vulkan
{
enum class StockSampler
{
	NearestClamp,
	LinearClamp,
	TrilinearClamp,
	NearestWrap,
	LinearWrap,
	TrilinearWrap,
	NearestShadow,
	LinearShadow,
	Count
};

struct SamplerCreateInfo
{
	VkFilter magFilter;
	VkFilter minFilter;
	VkSamplerMipmapMode mipmapMode;
	VkSamplerAddressMode addressModeU;
	VkSamplerAddressMode addressModeV;
	VkSamplerAddressMode addressModeW;
	float mipLodBias;
	VkBool32 anisotropyEnable;
	float maxAnisotropy;
	VkBool32 compareEnable;
	VkCompareOp compareOp;
	float minLod;
	float maxLod;
	VkBorderColor borderColor;
	VkBool32 unnormalizedCoordinates;
};

class Sampler : public Util::IntrusivePtrEnabled<Sampler>, public Cookie, public InternalSyncEnabled
{
public:
	Sampler(Device *device, VkSampler sampler, const SamplerCreateInfo &info);
	~Sampler();

	VkSampler get_sampler() const
	{
		return sampler;
	}

	const SamplerCreateInfo &get_create_info() const
	{
		return create_info;
	}

private:
	Device *device;
	VkSampler sampler;
	SamplerCreateInfo create_info;
};
using SamplerHandle = Util::IntrusivePtr<Sampler>;
}
