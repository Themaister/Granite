#include "sampler.hpp"
#include "device.hpp"

namespace Vulkan
{
Sampler::Sampler(Device *device, VkSampler sampler, const SamplerCreateInfo &info)
    : Cookie(device)
    , device(device)
    , sampler(sampler)
    , create_info(info)
{
}

Sampler::~Sampler()
{
	if (sampler)
		device->destroy_sampler(sampler);
}
}
