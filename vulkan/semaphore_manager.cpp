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

#include "semaphore_manager.hpp"
#include "device.hpp"

namespace Vulkan
{
void SemaphoreManager::test_external_semaphore_handle_type(VkExternalSemaphoreHandleTypeFlagBits handle_type)
{
	if (device->get_device_features().supports_external)
	{
		VkExternalSemaphoreProperties props = { VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES };
		VkPhysicalDeviceExternalSemaphoreInfo info = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO };
		info.handleType = handle_type;
		vkGetPhysicalDeviceExternalSemaphoreProperties(device->get_physical_device(), &info, &props);

		if (props.externalSemaphoreFeatures & VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT)
			exportable_types |= info.handleType;
		if (props.externalSemaphoreFeatures & VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT)
			importable_types |= info.handleType;
	}
}

void SemaphoreManager::init(Device *device_)
{
	device = device_;
	table = &device->get_device_table();

#ifdef _WIN32
	test_external_semaphore_handle_type(VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT);
#else
	test_external_semaphore_handle_type(VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT);
#endif
}

SemaphoreManager::~SemaphoreManager()
{
	for (auto &sem : semaphores)
		table->vkDestroySemaphore(device->get_device(), sem, nullptr);
	for (auto &sem : semaphores_external)
		table->vkDestroySemaphore(device->get_device(), sem, nullptr);
}

void SemaphoreManager::recycle(VkSemaphore sem, bool external)
{
	if (sem != VK_NULL_HANDLE)
	{
		if (external)
			semaphores_external.push_back(sem);
		else
			semaphores.push_back(sem);
	}
}

VkSemaphore SemaphoreManager::request_cleared_semaphore(bool external)
{
	auto &sems = external ? semaphores_external : semaphores;

	if (sems.empty())
	{
		VkSemaphoreCreateInfo info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
		VkSemaphore semaphore;

		if (external)
		{
			if ((exportable_types & importable_types) == 0)
				return VK_NULL_HANDLE;

			VkExportSemaphoreCreateInfo export_info = { VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO };
			export_info.handleTypes = exportable_types;

			// For Win32, use default security attributes.
			info.pNext = &export_info;

			if (table->vkCreateSemaphore(device->get_device(), &info, nullptr, &semaphore) != VK_SUCCESS)
			{
				LOGE("Failed to create external semaphore.\n");
				semaphore = VK_NULL_HANDLE;
			}

			return semaphore;
		}
		else
		{
			if (table->vkCreateSemaphore(device->get_device(), &info, nullptr, &semaphore) != VK_SUCCESS)
			{
				LOGE("Failed to create semaphore.\n");
				semaphore = VK_NULL_HANDLE;
			}

			return semaphore;
		}
	}
	else
	{
		auto sem = sems.back();
		sems.pop_back();
		return sem;
	}
}
}
