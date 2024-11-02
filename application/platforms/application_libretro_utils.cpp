/* Copyright (c) 2017-2024 Hans-Kristian Arntzen
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

#include "application_libretro_utils.hpp"
#include "application.hpp"
#include "application_events.hpp"
#include "thread_group.hpp"
#include "asset_manager.hpp"
#include "context.hpp"

namespace Granite
{
retro_log_printf_t libretro_log;
static retro_hw_render_interface_vulkan *vulkan_interface;
static retro_hw_render_context_negotiation_interface_vulkan vulkan_negotiation;
static Vulkan::ContextHandle vulkan_context;
static Vulkan::ImageViewHandle swapchain_unorm_view;
static Vulkan::ImageHandle swapchain_image;
static retro_vulkan_image swapchain_image_info;
static bool can_dupe = false;
static std::string application_name;
static unsigned application_version;

static unsigned swapchain_width;
static unsigned swapchain_height;
static unsigned swapchain_frame_index;
static Vulkan::Semaphore acquire_semaphore;

static VkApplicationInfo vulkan_app = {
	VK_STRUCTURE_TYPE_APPLICATION_INFO,
	nullptr,
	nullptr, 0,
	"Granite",
	0,
	VK_API_VERSION_1_1,
};

void libretro_set_swapchain_size(unsigned width, unsigned height)
{
	swapchain_width = width;
	swapchain_height = height;
}

void libretro_set_application_info(const char *name, unsigned version)
{
	application_name = name;
	application_version = version;
	vulkan_app.pApplicationName = application_name.c_str();
	vulkan_app.applicationVersion = application_version;
}

bool libretro_create_device(
		struct retro_vulkan_context *context,
		VkInstance instance,
		VkPhysicalDevice gpu,
		VkSurfaceKHR surface,
		PFN_vkGetInstanceProcAddr get_instance_proc_addr,
		const char **required_device_extensions,
		unsigned num_required_device_extensions,
		const char **, unsigned, // Deprecated.
		const VkPhysicalDeviceFeatures *required_features)
{
	if (!Vulkan::Context::init_loader(get_instance_proc_addr))
		return false;

	vulkan_context = Util::make_handle<Vulkan::Context>();
	Vulkan::Context::SystemHandles system_handles;
	system_handles.filesystem = GRANITE_FILESYSTEM();
	system_handles.thread_group = GRANITE_THREAD_GROUP();
	system_handles.asset_manager = GRANITE_ASSET_MANAGER();
	system_handles.timeline_trace_file = system_handles.thread_group->get_timeline_trace_file();
	vulkan_context->set_system_handles(system_handles);
	vulkan_context->set_num_thread_indices(GRANITE_THREAD_GROUP()->get_num_threads() + 1);
	if (!vulkan_context->init_device_from_instance(instance, gpu, surface, required_device_extensions, num_required_device_extensions,
	                                               required_features))
	{
		return false;
	}

	vulkan_context->release_device();
	context->gpu = vulkan_context->get_gpu();
	context->device = vulkan_context->get_device();
	context->presentation_queue = vulkan_context->get_queue_info().queues[Vulkan::QUEUE_INDEX_GRAPHICS];
	context->presentation_queue_family_index = vulkan_context->get_queue_info().family_indices[Vulkan::QUEUE_INDEX_GRAPHICS];
	context->queue = vulkan_context->get_queue_info().queues[Vulkan::QUEUE_INDEX_GRAPHICS];
	context->queue_family_index = vulkan_context->get_queue_info().family_indices[Vulkan::QUEUE_INDEX_GRAPHICS];
	return true;
}

static VkInstance libretro_create_instance(
		PFN_vkGetInstanceProcAddr get_instance_proc_addr,
		const VkApplicationInfo *app,
		retro_vulkan_create_instance_wrapper_t create_instance_wrapper,
		void *opaque)
{
	if (!Vulkan::Context::init_loader(get_instance_proc_addr))
		return VK_NULL_HANDLE;

	vulkan_context = Util::make_handle<Vulkan::Context>();
	Vulkan::Context::SystemHandles system_handles;
	system_handles.filesystem = GRANITE_FILESYSTEM();
	system_handles.thread_group = GRANITE_THREAD_GROUP();
	system_handles.asset_manager = GRANITE_ASSET_MANAGER();
	system_handles.timeline_trace_file = system_handles.thread_group->get_timeline_trace_file();

	vulkan_context->set_application_info(app);
	vulkan_context->set_system_handles(system_handles);
	vulkan_context->set_num_thread_indices(GRANITE_THREAD_GROUP()->get_num_threads() + 1);

	struct Factory final : Vulkan::InstanceFactory
	{
		VkInstance create_instance(const VkInstanceCreateInfo *info) override
		{
			return wrapper(opaque, info);
		}

		retro_vulkan_create_instance_wrapper_t wrapper = nullptr;
		void *opaque = nullptr;
	} factory;

	factory.wrapper = create_instance_wrapper;
	factory.opaque = opaque;
	vulkan_context->set_instance_factory(&factory);

	if (!vulkan_context->init_instance(nullptr, 0))
	{
		vulkan_context.reset();
		return VK_NULL_HANDLE;
	}

	vulkan_context->release_instance();
	return vulkan_context->get_instance();
}

static bool libretro_create_device2(
		struct retro_vulkan_context *context,
		VkInstance instance,
		VkPhysicalDevice gpu,
		VkSurfaceKHR surface,
		PFN_vkGetInstanceProcAddr get_instance_proc_addr,
		retro_vulkan_create_device_wrapper_t create_device_wrapper,
		void *opaque)
{
	// We are guaranteed that create_instance has been called here.
	if (!vulkan_context)
		return false;

	// Sanity check inputs.
	if (vulkan_context->get_instance() != instance)
		return false;
	if (Vulkan::Context::get_instance_proc_addr() != get_instance_proc_addr)
		return false;

	struct Factory final : Vulkan::DeviceFactory
	{
		VkDevice create_device(VkPhysicalDevice gpu, const VkDeviceCreateInfo *info) override
		{
			return wrapper(gpu, opaque, info);
		}

		retro_vulkan_create_device_wrapper_t wrapper = nullptr;
		void *opaque = nullptr;
	} factory;

	factory.wrapper = create_device_wrapper;
	factory.opaque = opaque;
	vulkan_context->set_device_factory(&factory);

	if (!vulkan_context->init_device(gpu, surface, nullptr, 0))
		return false;

	vulkan_context->release_device();
	context->gpu = vulkan_context->get_gpu();
	context->device = vulkan_context->get_device();
	context->presentation_queue = vulkan_context->get_queue_info().queues[Vulkan::QUEUE_INDEX_GRAPHICS];
	context->presentation_queue_family_index = vulkan_context->get_queue_info().family_indices[Vulkan::QUEUE_INDEX_GRAPHICS];
	context->queue = vulkan_context->get_queue_info().queues[Vulkan::QUEUE_INDEX_GRAPHICS];
	context->queue_family_index = vulkan_context->get_queue_info().family_indices[Vulkan::QUEUE_INDEX_GRAPHICS];
	return true;
}

void libretro_begin_frame(Vulkan::WSI &wsi, retro_usec_t frame_time)
{
	// Setup the external frame.
	vulkan_interface->wait_sync_index(vulkan_interface->handle);
	wsi.set_external_frame(swapchain_frame_index, std::move(acquire_semaphore), double(frame_time) * 1e-6);
	acquire_semaphore = {};

	swapchain_frame_index ^= 1;
}

void libretro_end_frame(retro_video_refresh_t video_cb, Vulkan::WSI &wsi)
{
	// Present to libretro frontend.
	auto signal_semaphore = wsi.get_device().request_semaphore(VK_SEMAPHORE_TYPE_BINARY);
	vulkan_interface->set_signal_semaphore(vulkan_interface->handle,
	                                       signal_semaphore->get_semaphore());
	signal_semaphore->signal_external();

	acquire_semaphore = wsi.consume_external_release_semaphore();
	if (acquire_semaphore && acquire_semaphore->get_semaphore() != VK_NULL_HANDLE)
	{
		vulkan_interface->set_image(vulkan_interface->handle,
		                            &swapchain_image_info,
		                            1, &acquire_semaphore->get_semaphore(),
		                            VK_QUEUE_FAMILY_IGNORED);

		// Lets us recycle the semaphore.
		acquire_semaphore->wait_external();

		video_cb(RETRO_HW_FRAME_BUFFER_VALID, swapchain_width, swapchain_height, 0);
		can_dupe = true;
	}
	else
	{
		vulkan_interface->set_image(vulkan_interface->handle,
		                            &swapchain_image_info,
		                            0, nullptr,
		                            VK_QUEUE_FAMILY_IGNORED);

		if (!can_dupe)
		{
			// Need something to show ... Just clear the image to black and present that.
			// This should only happen if we don't render to swapchain the very first frame,
			// so performance doesn't really matter.
			auto &device = wsi.get_device();
			auto cmd = device.request_command_buffer();
			cmd->image_barrier(*swapchain_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			                   VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, VK_PIPELINE_STAGE_2_CLEAR_BIT,
			                   VK_ACCESS_TRANSFER_WRITE_BIT);
			cmd->clear_image(*swapchain_image, {});
			cmd->image_barrier(*swapchain_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			                   VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
			                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
			device.submit(cmd);
			video_cb(RETRO_HW_FRAME_BUFFER_VALID, swapchain_width, swapchain_height, 0);
			can_dupe = true;
		}
		else
		{
			video_cb(nullptr, swapchain_width, swapchain_height, 0);
		}
	}

	// Mark video_cb has having done work in our frame context.
	wsi.get_device().submit_external(Vulkan::CommandBuffer::Type::Generic);

	acquire_semaphore = signal_semaphore;
}

bool libretro_context_reset(retro_hw_render_interface_vulkan *vulkan, Granite::Application &app)
{
	vulkan_interface = vulkan;
	if (vulkan->interface_type != RETRO_HW_RENDER_INTERFACE_VULKAN)
		return false;

	if (vulkan->interface_version != RETRO_HW_RENDER_INTERFACE_VULKAN_VERSION)
		return false;

	if (!app.init_wsi(std::move(vulkan_context)))
		return false;

	auto &device = app.get_wsi().get_device();
	device.set_queue_lock([vulkan]() {
		                      vulkan->lock_queue(vulkan->handle);
	                      },
	                      [vulkan]() {
		                      vulkan->unlock_queue(vulkan->handle);
	                      });

	const unsigned num_swapchain_images = 2;

	Vulkan::ImageCreateInfo info = Vulkan::ImageCreateInfo::render_target(swapchain_width, swapchain_height,
	                                                                      VK_FORMAT_R8G8B8A8_SRGB);
	info.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	info.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
	info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;

	swapchain_image = device.create_image(info, nullptr);
	swapchain_image->set_swapchain_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	can_dupe = false;

	Vulkan::ImageViewCreateInfo view_info;
	view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
	view_info.image = swapchain_image.get();
	swapchain_unorm_view = device.create_image_view(view_info);

	std::vector<Vulkan::ImageHandle> images;
	for (unsigned i = 0; i < num_swapchain_images; i++)
		images.push_back(swapchain_image);

	device.init_frame_contexts(2);
	if (!app.get_wsi().init_external_swapchain(std::move(images)))
		return false;

	// Setup the swapchain image info for the frontend.
	swapchain_image_info.image_view = swapchain_unorm_view->get_view();
	swapchain_image_info.image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	swapchain_image_info.create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	swapchain_image_info.create_info.image = swapchain_unorm_view->get_image().get_image();
	swapchain_image_info.create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
	swapchain_image_info.create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
	swapchain_image_info.create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
	swapchain_image_info.create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
	swapchain_image_info.create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
	swapchain_image_info.create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	swapchain_image_info.create_info.subresourceRange.levelCount = 1;
	swapchain_image_info.create_info.subresourceRange.layerCount = 1;
	swapchain_image_info.create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	swapchain_frame_index = 0;
	return true;
}

void libretro_context_destroy(Granite::Application *app)
{
	swapchain_unorm_view.reset();
	swapchain_image.reset();
	acquire_semaphore.reset();

	if (app)
		app->teardown_wsi();
}

static const VkApplicationInfo *get_application_info(void)
{
	return &vulkan_app;
}

bool libretro_load_game(retro_environment_t environ_cb)
{
	vulkan_negotiation.interface_type = RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN;
	if (!environ_cb(RETRO_ENVIRONMENT_GET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_SUPPORT, &vulkan_negotiation))
	{
		Granite::libretro_log(RETRO_LOG_WARN, "GET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_SUPPORT failed, assuming v1 only.\n");
		vulkan_negotiation.interface_version = 1;
	}
	else if (vulkan_negotiation.interface_version == 0)
	{
		Granite::libretro_log(RETRO_LOG_ERROR, "Vulkan is not supported, this core cannot run.\n");
	}
	else
	{
		Granite::libretro_log(RETRO_LOG_INFO, "GET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_SUPPORT passed, exposing v2.\n");
		vulkan_negotiation.interface_version = 2;
	}

	vulkan_negotiation.create_device = Granite::libretro_create_device;
	vulkan_negotiation.create_device2 = Granite::libretro_create_device2;
	vulkan_negotiation.create_instance = Granite::libretro_create_instance;
	vulkan_negotiation.destroy_device = nullptr;
	vulkan_negotiation.get_application_info = get_application_info;

	if (!environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE, &vulkan_negotiation))
	{
		Granite::libretro_log(RETRO_LOG_ERROR, "SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE failed, this core cannot run.\n");
		return false;
	}

	auto *em = GRANITE_EVENT_MANAGER();
	if (em)
	{
		em->dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
		em->enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Stopped);
		em->dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
		em->enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Paused);
		em->dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
		em->enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Running);
	}
	return true;
}

void libretro_unload_game()
{
	auto *em = GRANITE_EVENT_MANAGER();
	if (em)
	{
		em->dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
		em->enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Paused);
		em->dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
		em->enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Stopped);
	}
}
}
