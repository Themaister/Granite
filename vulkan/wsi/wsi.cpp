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

#include "wsi.hpp"
#include "vulkan_symbol_wrapper.h"
#include "vulkan_events.hpp"
#include "application_events.hpp"

using namespace std;

namespace Vulkan
{
WSI::WSI()
{
	device.reset(new Device);
	semaphore_manager.reset(new SemaphoreManager);
}

bool WSI::reinit_external_swapchain(std::vector<Vulkan::ImageHandle> external_images)
{
	if (!init_external_swapchain(move(external_images)))
		return false;

	device->init_external_swapchain(this->external_swapchain_images);
	external_acquire.reset();
	external_release.reset();
	return true;
}

bool WSI::init_external(std::unique_ptr<Vulkan::Context> fresh_context,
                        std::vector<Vulkan::ImageHandle> swapchain_images)
{
	context = move(fresh_context);

	width = platform->get_surface_width();
	height = platform->get_surface_height();
	aspect_ratio = platform->get_aspect_ratio();

	if (!init_external_swapchain(move(swapchain_images)))
		return false;

	semaphore_manager->init(context->get_device());
	auto &em = Granite::EventManager::get_global();
	device->init_external_swapchain(this->external_swapchain_images);
	em.enqueue_latched<DeviceCreatedEvent>(device.get());
	platform->get_frame_timer().reset();
	return true;
}

void WSI::set_platform(WSIPlatform *platform)
{
	this->platform = platform;
}

bool WSI::init()
{
	auto instance_ext = platform->get_instance_extensions();
	auto device_ext = platform->get_device_extensions();
	context.reset(new Context(instance_ext.data(), instance_ext.size(), device_ext.data(), device_ext.size()));

	semaphore_manager->init(context->get_device());
	device->set_context(*context);
	auto &em = Granite::EventManager::get_global();
	em.enqueue_latched<DeviceCreatedEvent>(device.get());

	surface = platform->create_surface(context->get_instance(), context->get_gpu());
	if (surface == VK_NULL_HANDLE)
		return false;

	unsigned width = platform->get_surface_width();
	unsigned height = platform->get_surface_height();
	aspect_ratio = platform->get_aspect_ratio();

	VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_EXTENSION_SYMBOL(context->get_instance(), vkDestroySurfaceKHR);
	VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_EXTENSION_SYMBOL(context->get_instance(), vkGetPhysicalDeviceSurfaceSupportKHR);
	VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_EXTENSION_SYMBOL(context->get_instance(),
	                                                     vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
	VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_EXTENSION_SYMBOL(context->get_instance(), vkGetPhysicalDeviceSurfaceFormatsKHR);
	VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_EXTENSION_SYMBOL(context->get_instance(),
	                                                     vkGetPhysicalDeviceSurfacePresentModesKHR);

	VULKAN_SYMBOL_WRAPPER_LOAD_DEVICE_EXTENSION_SYMBOL(context->get_device(), vkCreateSwapchainKHR);
	VULKAN_SYMBOL_WRAPPER_LOAD_DEVICE_EXTENSION_SYMBOL(context->get_device(), vkDestroySwapchainKHR);
	VULKAN_SYMBOL_WRAPPER_LOAD_DEVICE_EXTENSION_SYMBOL(context->get_device(), vkGetSwapchainImagesKHR);
	VULKAN_SYMBOL_WRAPPER_LOAD_DEVICE_EXTENSION_SYMBOL(context->get_device(), vkAcquireNextImageKHR);
	VULKAN_SYMBOL_WRAPPER_LOAD_DEVICE_EXTENSION_SYMBOL(context->get_device(), vkQueuePresentKHR);

	VkBool32 supported = VK_FALSE;
	vkGetPhysicalDeviceSurfaceSupportKHR(context->get_gpu(), context->get_graphics_queue_family(), surface, &supported);
	if (!supported)
		return false;

	if (!init_swapchain(width, height))
		return false;
	device->init_swapchain(swapchain_images, this->width, this->height, format);
	platform->get_frame_timer().reset();

	return true;
}

void WSI::init_surface_and_swapchain(VkSurfaceKHR new_surface)
{
	if (new_surface != VK_NULL_HANDLE)
	{
		VK_ASSERT(surface == VK_NULL_HANDLE);
		surface = new_surface;
	}

	width = platform->get_surface_width();
	height = platform->get_surface_height();
	update_framebuffer(width, height);
}

void WSI::deinit_surface_and_swapchain()
{
	device->wait_idle();

	auto acquire = device->set_acquire(VK_NULL_HANDLE);
	auto release = device->set_release(VK_NULL_HANDLE);
	if (acquire != VK_NULL_HANDLE)
		vkDestroySemaphore(device->get_device(), acquire, nullptr);
	if (release != VK_NULL_HANDLE)
		vkDestroySemaphore(device->get_device(), release, nullptr);

	if (swapchain != VK_NULL_HANDLE)
		vkDestroySwapchainKHR(context->get_device(), swapchain, nullptr);
	swapchain = VK_NULL_HANDLE;
	need_acquire = true;

	if (surface != VK_NULL_HANDLE)
		vkDestroySurfaceKHR(context->get_instance(), surface, nullptr);
	surface = VK_NULL_HANDLE;

	auto &em = Granite::EventManager::get_global();
	em.dequeue_all_latched(SwapchainParameterEvent::get_type_id());
}

void WSI::set_external_frame(unsigned index, Vulkan::Semaphore acquire_semaphore, double frame_time)
{
	external_frame_index = index;
	external_acquire = move(acquire_semaphore);
	frame_is_external = true;
	external_frame_time = frame_time;
}

bool WSI::begin_frame_external()
{
	// Need to handle this stuff from outside.
	if (!need_acquire)
		return false;

	auto &em = Granite::EventManager::get_global();
	auto frame_time = platform->get_frame_timer().frame(external_frame_time);
	auto elapsed_time = platform->get_frame_timer().get_elapsed();

	// Poll after acquire as well for optimal latency.
	platform->poll_input();

	swapchain_index = external_frame_index;
	em.dispatch_inline(Granite::FrameTickEvent{frame_time, elapsed_time});

	release_semaphore = semaphore_manager->request_cleared_semaphore();
	device->begin_frame(swapchain_index);
	em.dequeue_all_latched(SwapchainIndexEvent::get_type_id());
	em.enqueue_latched<SwapchainIndexEvent>(device.get(), swapchain_index);

	if (external_acquire)
		semaphore_manager->recycle(device->set_acquire(external_acquire->consume()));
	else
		semaphore_manager->recycle(device->set_acquire(VK_NULL_HANDLE));

	semaphore_manager->recycle(device->set_release(release_semaphore));
	external_release.reset();
	return true;
}

Vulkan::Semaphore WSI::get_external_release_semaphore()
{
	return external_release;
}

bool WSI::begin_frame()
{
	if (frame_is_external)
		return begin_frame_external();

	if (platform->should_resize())
	{
		update_framebuffer(platform->get_surface_width(), platform->get_surface_height());
		platform->acknowledge_resize();
	}

	if (!need_acquire)
		return true;

	external_release.reset();

	VkResult result;
	do
	{
		VkSemaphore acquire = semaphore_manager->request_cleared_semaphore();
		result = vkAcquireNextImageKHR(context->get_device(), swapchain, UINT64_MAX, acquire, VK_NULL_HANDLE,
		                               &swapchain_index);

		if (result == VK_SUCCESS)
		{
			auto &em = Granite::EventManager::get_global();
			auto frame_time = platform->get_frame_timer().frame();
			auto elapsed_time = platform->get_frame_timer().get_elapsed();

			// Poll after acquire as well for optimal latency.
			platform->poll_input();
			em.dispatch_inline(Granite::FrameTickEvent{frame_time, elapsed_time});

			release_semaphore = semaphore_manager->request_cleared_semaphore();
			device->begin_frame(swapchain_index);
			em.dequeue_all_latched(SwapchainIndexEvent::get_type_id());
			em.enqueue_latched<SwapchainIndexEvent>(device.get(), swapchain_index);
			semaphore_manager->recycle(device->set_acquire(acquire));
			semaphore_manager->recycle(device->set_release(release_semaphore));
		}
		else if (result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_ERROR_SURFACE_LOST_KHR)
		{
			VK_ASSERT(width != 0);
			VK_ASSERT(height != 0);
			vkDeviceWaitIdle(device->get_device());
			vkDestroySemaphore(device->get_device(), acquire, nullptr);

			auto old_acquire = device->set_acquire(VK_NULL_HANDLE);
			auto old_release = device->set_release(VK_NULL_HANDLE);
			if (old_acquire != VK_NULL_HANDLE)
				vkDestroySemaphore(device->get_device(), old_acquire, nullptr);
			if (old_release != VK_NULL_HANDLE)
				vkDestroySemaphore(device->get_device(), old_release, nullptr);

			if (!init_swapchain(width, height))
				return false;
			device->init_swapchain(swapchain_images, width, height, format);
		}
		else
		{
			semaphore_manager->recycle(acquire);
			return false;
		}
	} while (result != VK_SUCCESS);
	return true;
}

bool WSI::end_frame()
{
	device->flush_frame();

	if (!device->swapchain_touched())
	{
		device->wait_idle();
		return true;
	}

	need_acquire = true;

	// Take ownership of the release semaphore so that the external user can use it.
	if (frame_is_external)
	{
		external_release = Util::make_handle<SemaphoreHolder>(device.get(), device->set_release(VK_NULL_HANDLE), true);
		frame_is_external = false;
	}
	else
	{
		VkResult result = VK_SUCCESS;
		VkPresentInfoKHR info = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
		info.waitSemaphoreCount = 1;
		info.pWaitSemaphores = &release_semaphore;
		info.swapchainCount = 1;
		info.pSwapchains = &swapchain;
		info.pImageIndices = &swapchain_index;
		info.pResults = &result;

		VkResult overall = vkQueuePresentKHR(context->get_graphics_queue(), &info);
		if (overall != VK_SUCCESS || result != VK_SUCCESS)
		{
			LOGE("vkQueuePresentKHR failed.\n");
			return false;
		}
	}

	return true;
}

void WSI::update_framebuffer(unsigned width, unsigned height)
{
	vkDeviceWaitIdle(context->get_device());

	aspect_ratio = platform->get_aspect_ratio();
	init_swapchain(width, height);
	device->init_swapchain(swapchain_images, width, height, format);
}

bool WSI::init_external_swapchain(std::vector<Vulkan::ImageHandle> external_images)
{
	external_swapchain_images = move(external_images);

	this->width = external_swapchain_images.front()->get_width();
	this->height = external_swapchain_images.front()->get_height();
	this->format = external_swapchain_images.front()->get_format();

	LOGI("Created swapchain %u x %u (fmt: %u).\n", this->width, this->height, static_cast<unsigned>(this->format));

	auto &em = Granite::EventManager::get_global();
	em.dequeue_all_latched(SwapchainParameterEvent::get_type_id());
	em.enqueue_latched<SwapchainParameterEvent>(device.get(), this->width, this->height, aspect_ratio,
	                                            external_swapchain_images.size(), this->format);

	return true;
}

void WSI::deinit_external()
{
	auto &em = Granite::EventManager::get_global();
	if (context)
	{
		vkDeviceWaitIdle(context->get_device());
		semaphore_manager->recycle(device->set_acquire(VK_NULL_HANDLE));
		semaphore_manager->recycle(device->set_release(VK_NULL_HANDLE));
		em.dequeue_all_latched(SwapchainParameterEvent::get_type_id());
		if (swapchain != VK_NULL_HANDLE)
			vkDestroySwapchainKHR(context->get_device(), swapchain, nullptr);
	}

	if (surface != VK_NULL_HANDLE)
		vkDestroySurfaceKHR(context->get_instance(), surface, nullptr);

	em.dequeue_all_latched(DeviceCreatedEvent::get_type_id());
	external_release.reset();
	external_acquire.reset();
	external_swapchain_images.clear();
	semaphore_manager.reset(new SemaphoreManager);
	device.reset(new Device);
	context.reset();
}

bool WSI::init_swapchain(unsigned width, unsigned height)
{
	VkSurfaceCapabilitiesKHR surface_properties;
	auto gpu = context->get_gpu();
	V(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &surface_properties));

	uint32_t format_count;
	vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &format_count, nullptr);
	vector<VkSurfaceFormatKHR> formats(format_count);
	vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &format_count, formats.data());

	VkSurfaceFormatKHR format;
	if (format_count == 1 && formats[0].format == VK_FORMAT_UNDEFINED)
	{
		format = formats[0];
		format.format = VK_FORMAT_B8G8R8A8_UNORM;
	}
	else
	{
		if (format_count == 0)
		{
			LOGE("Surface has no formats.\n");
			return false;
		}

		bool found = false;
		for (unsigned i = 0; i < format_count; i++)
		{
			if (formats[i].format == VK_FORMAT_R8G8B8A8_SRGB ||
			    formats[i].format == VK_FORMAT_B8G8R8A8_SRGB ||
			    formats[i].format == VK_FORMAT_A8B8G8R8_SRGB_PACK32)
			{
				format = formats[i];
				found = true;
			}
		}

		if (!found)
			format = formats[0];
	}

	VkExtent2D swapchain_size;
	if (surface_properties.currentExtent.width == ~0u)
	{
		swapchain_size.width = width;
		swapchain_size.height = height;
	}
	else
	{
		swapchain_size.width = max(min(width, surface_properties.maxImageExtent.width), surface_properties.minImageExtent.width);
		swapchain_size.height = max(min(height, surface_properties.maxImageExtent.height), surface_properties.minImageExtent.height);
	}

	uint32_t num_present_modes;
	vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &num_present_modes, nullptr);
	vector<VkPresentModeKHR> present_modes(num_present_modes);
	vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &num_present_modes, present_modes.data());

	VkPresentModeKHR swapchain_present_mode = VK_PRESENT_MODE_FIFO_KHR;
#if 0
	for (uint32_t i = 0; i < num_present_modes; i++)
	{
		if (present_modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR || present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
		{
			swapchain_present_mode = present_modes[i];
			break;
		}
	}
#endif

	uint32_t desired_swapchain_images = surface_properties.minImageCount + 1;
	if ((surface_properties.maxImageCount > 0) && (desired_swapchain_images > surface_properties.maxImageCount))
		desired_swapchain_images = surface_properties.maxImageCount;

	VkSurfaceTransformFlagBitsKHR pre_transform;
	if (surface_properties.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
		pre_transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	else
		pre_transform = surface_properties.currentTransform;

	VkCompositeAlphaFlagBitsKHR composite_mode = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	if (surface_properties.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR)
		composite_mode = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
	if (surface_properties.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
		composite_mode = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	if (surface_properties.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR)
		composite_mode = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
	if (surface_properties.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR)
		composite_mode = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;

	VkSwapchainKHR old_swapchain = swapchain;

	VkSwapchainCreateInfoKHR info = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
	info.surface = surface;
	info.minImageCount = desired_swapchain_images;
	info.imageFormat = format.format;
	info.imageColorSpace = format.colorSpace;
	info.imageExtent.width = swapchain_size.width;
	info.imageExtent.height = swapchain_size.height;
	info.imageArrayLayers = 1;
	info.imageUsage =
	    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	info.preTransform = pre_transform;
	info.compositeAlpha = composite_mode;
	info.presentMode = swapchain_present_mode;
	info.clipped = true;
	info.oldSwapchain = old_swapchain;

	V(vkCreateSwapchainKHR(context->get_device(), &info, nullptr, &swapchain));

	if (old_swapchain != VK_NULL_HANDLE)
		vkDestroySwapchainKHR(context->get_device(), old_swapchain, nullptr);

	this->width = swapchain_size.width;
	this->height = swapchain_size.height;
	this->format = format.format;

	LOGI("Created swapchain %u x %u (fmt: %u).\n", this->width, this->height, static_cast<unsigned>(this->format));

	uint32_t image_count;
	V(vkGetSwapchainImagesKHR(context->get_device(), swapchain, &image_count, nullptr));
	swapchain_images.resize(image_count);
	V(vkGetSwapchainImagesKHR(context->get_device(), swapchain, &image_count, swapchain_images.data()));

	auto &em = Granite::EventManager::get_global();
	em.dequeue_all_latched(SwapchainParameterEvent::get_type_id());
	em.enqueue_latched<SwapchainParameterEvent>(device.get(), this->width, this->height, aspect_ratio, image_count, info.imageFormat);

	return true;
}

WSI::~WSI()
{
	deinit_external();
}
}
