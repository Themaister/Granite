/* Copyright (c) 2017-2022 Hans-Kristian Arntzen
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

#include "semaphore.hpp"
#include "device.hpp"

namespace Vulkan
{
SemaphoreHolder::~SemaphoreHolder()
{
	recycle_semaphore();
}

void SemaphoreHolder::recycle_semaphore()
{
	if (timeline == 0 && semaphore)
	{
		if (internal_sync)
		{
			if (is_signalled())
				device->destroy_semaphore_nolock(semaphore);
			else if (external_compatible)
				device->recycle_external_semaphore_nolock(semaphore);
			else
				device->recycle_semaphore_nolock(semaphore);
		}
		else
		{
			if (is_signalled())
				device->destroy_semaphore(semaphore);
			else if (external_compatible)
				device->recycle_external_semaphore(semaphore);
			else
				device->recycle_semaphore(semaphore);
		}
	}
}

SemaphoreHolder &SemaphoreHolder::operator=(SemaphoreHolder &&other) noexcept
{
	if (this == &other)
		return *this;

	assert(device == other.device);
	recycle_semaphore();

	semaphore = other.semaphore;
	timeline = other.timeline;
	signalled = other.signalled;
	pending_wait = other.pending_wait;

	other.semaphore = VK_NULL_HANDLE;
	other.timeline = 0;
	other.signalled = false;
	other.pending_wait = false;

	return *this;
}

ExternalHandle SemaphoreHolder::export_to_opaque_handle()
{
	ExternalHandle h;

	if (!external_compatible)
	{
		LOGE("Semaphore is not external compatible.\n");
		return h;
	}

	if (!semaphore)
	{
		LOGE("Semaphore has already been consumed.\n");
		return h;
	}

	// Technically we can export early with reference transference, but it's a bit dubious.
	// We want to remain compatible with copy transference for later, e.g. SYNC_FD.
	if (!signalled)
	{
		LOGE("Cannot export payload from a semaphore that is not queued up for signal.\n");
		return h;
	}

#ifdef _WIN32
	VkSemaphoreGetWin32HandleInfoKHR handle_info = { VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR };
	handle_info.semaphore = semaphore;
	handle_info.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;

	if (device->get_device_table().vkGetSemaphoreWin32HandleKHR(device->get_device(), &handle_info, &h.handle) != VK_SUCCESS)
	{
		LOGE("Failed to export to opaque handle.\n");
		h.handle = nullptr;
	}
#else
	VkSemaphoreGetFdInfoKHR fd_info = { VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR };
	fd_info.semaphore = semaphore;
	fd_info.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;

	if (device->get_device_table().vkGetSemaphoreFdKHR(device->get_device(), &fd_info, &h.handle) != VK_SUCCESS)
	{
		LOGE("Failed to export to opaque FD.\n");
		h.handle = -1;
	}
#endif

	return h;
}

bool SemaphoreHolder::import_from_opaque_handle(ExternalHandle handle)
{
	if (!external_compatible)
	{
		LOGE("Semaphore is not external compatible.\n");
		return false;
	}

	if (!semaphore)
	{
		LOGE("Semaphore has already been consumed.\n");
		return false;
	}

	if (signalled)
	{
		LOGE("Cannot import payload to semaphore that is already signalled.\n");
		return false;
	}

#ifdef _WIN32
	VkImportSemaphoreWin32HandleInfoKHR import = { VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR };
	import.handle = handle.handle;
	import.semaphore = semaphore;
	import.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
	import.flags = VK_SEMAPHORE_IMPORT_TEMPORARY_BIT;
	if (device->get_device_table().vkImportSemaphoreWin32HandleKHR(device->get_device(), &import) != VK_SUCCESS)
	{
		LOGE("Failed to import semaphore handle %p!\n", handle.handle);
		return false;
	}

	// Consume the handle, since the VkSemaphore holds a reference on Win32.
	::CloseHandle(handle.handle);
#else
	VkImportSemaphoreFdInfoKHR import = { VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR };
	import.fd = handle.handle;
	import.semaphore = semaphore;
	import.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
	import.flags = VK_SEMAPHORE_IMPORT_TEMPORARY_BIT;
	if (device->get_device_table().vkImportSemaphoreFdKHR(device->get_device(), &import) != VK_SUCCESS)
	{
		LOGE("Failed to import semaphore FD %d!\n", handle.handle);
		return false;
	}
#endif

	signal_external();
	return true;
}

void SemaphoreHolderDeleter::operator()(Vulkan::SemaphoreHolder *semaphore)
{
	semaphore->device->handle_pool.semaphores.free(semaphore);
}
}
