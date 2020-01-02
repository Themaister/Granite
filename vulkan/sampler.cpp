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

#include "sampler.hpp"
#include "device.hpp"

namespace Vulkan
{
Sampler::Sampler(Device *device_, VkSampler sampler_, const SamplerCreateInfo &info)
    : Cookie(device_)
    , device(device_)
    , sampler(sampler_)
    , create_info(info)
{
}

Sampler::~Sampler()
{
	if (sampler)
	{
		if (internal_sync)
			device->destroy_sampler_nolock(sampler);
		else
			device->destroy_sampler(sampler);
	}
}

void SamplerDeleter::operator()(Sampler *sampler)
{
	sampler->device->handle_pool.samplers.free(sampler);
}
}
