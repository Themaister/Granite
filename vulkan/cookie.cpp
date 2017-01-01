#include "cookie.hpp"
#include "device.hpp"

namespace Vulkan
{
Cookie::Cookie(Device *device)
    : cookie(device->allocate_cookie())
{
}
}
