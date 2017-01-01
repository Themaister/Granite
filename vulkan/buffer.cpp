#include "buffer.hpp"
#include "device.hpp"

namespace Vulkan
{
Buffer::Buffer(Device *device, VkBuffer buffer, const DeviceAllocation &alloc, const BufferCreateInfo &info)
    : Cookie(device)
    , device(device)
    , buffer(buffer)
    , alloc(alloc)
    , info(info)
{
}

Buffer::~Buffer()
{
	device->destroy_buffer(buffer);
	device->free_memory(alloc);
}

BufferView::BufferView(Device *device, VkBufferView view, const BufferViewCreateInfo &create_info)
    : Cookie(device)
    , device(device)
    , view(view)
    , info(create_info)
{
}

BufferView::~BufferView()
{
	if (view != VK_NULL_HANDLE)
		device->destroy_buffer_view(view);
}
}
