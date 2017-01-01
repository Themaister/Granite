#include "semaphore.hpp"
#include "device.hpp"

namespace Vulkan
{
SemaphoreHolder::~SemaphoreHolder()
{
	if (semaphore)
		device->destroy_semaphore(semaphore);
}
}
