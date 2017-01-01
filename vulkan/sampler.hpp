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

class Sampler : public IntrusivePtrEnabled<Sampler>, public Cookie
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
using SamplerHandle = IntrusivePtr<Sampler>;
}
