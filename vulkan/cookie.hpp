#pragma once

#include <stdint.h>

namespace Vulkan
{
class Device;
class Cookie
{
public:
	Cookie(Device *device);
	uint64_t get_cookie() const
	{
		return cookie;
	}

private:
	uint64_t cookie;
};
}
