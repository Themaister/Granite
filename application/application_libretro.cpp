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

#include "application.hpp"
#include "vulkan_symbol_wrapper.h"
#include "vulkan.hpp"
#include "libretro/libretro.h"
#include "libretro/libretro_vulkan.h"

using namespace Granite;

static Application *app;
static retro_environment_t environ_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static retro_hw_render_context_negotiation_interface_vulkan vulkan_negotiation;
static std::unique_ptr<Vulkan::Context> vulkan_context;
static retro_hw_render_interface_vulkan *vulkan_interface;

namespace Granite
{
retro_log_printf_t libretro_log;
// We will handle the platform stuff externally through the libretro implementation.
std::unique_ptr<ApplicationPlatform> create_default_application_platform(unsigned, unsigned)
{
	return {};
}
}

struct ApplicationPlatformLibretro : Granite::ApplicationPlatform
{
	VkSurfaceKHR create_surface(VkInstance, VkPhysicalDevice) override
	{
		return VK_NULL_HANDLE;
	}

	std::vector<const char *> get_instance_extensions() override
	{
		return {};
	}

	unsigned get_surface_width() override
	{
		return app->get_width();
	}

	unsigned get_surface_height() override
	{
		return app->get_height();
	}

	bool alive(Vulkan::WSI &) override
	{
		return true;
	}

	void poll_input() override
	{
		input_poll_cb();
	}
};
static ApplicationPlatformLibretro platform_libretro;

static retro_hw_render_callback hw_render;

RETRO_API void retro_init(void)
{
}

RETRO_API void retro_deinit(void)
{
}

RETRO_API void retro_set_environment(retro_environment_t cb)
{
	environ_cb = cb;
	environ_cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, nullptr);

	retro_log_callback log_interface;
	if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log_interface))
		Granite::libretro_log = log_interface.log;
}

RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb)
{
	video_cb = cb;
}

RETRO_API void retro_set_audio_sample(retro_audio_sample_t)
{
}

RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
	audio_cb = cb;
}

RETRO_API void retro_set_input_poll(retro_input_poll_t cb)
{
	input_poll_cb = cb;
}

RETRO_API void retro_set_input_state(retro_input_state_t cb)
{
	input_state_cb = cb;
}

RETRO_API unsigned retro_api_version(void)
{
	return RETRO_API_VERSION;
}

RETRO_API void retro_get_system_info(struct retro_system_info *info)
{
	info->block_extract = false;
	info->library_name = "Granite";
	info->library_version = "0.0";
	info->need_fullpath = false;
	info->valid_extensions = nullptr;
}

RETRO_API void retro_reset(void)
{
}

RETRO_API void retro_run(void)
{
	if (app)
	{
		app->poll();
		app->run_frame();
	}
	else
	{
		input_poll_cb();
		environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, nullptr);
	}
}

RETRO_API size_t retro_serialize_size(void)
{
	return 0;
}

RETRO_API bool retro_serialize(void *, size_t)
{
	return false;
}

RETRO_API bool retro_unserialize(const void *, size_t)
{
	return false;
}

RETRO_API void retro_cheat_reset(void)
{
}

RETRO_API void retro_cheat_set(unsigned, bool, const char *)
{
}

static void context_destroy(void)
{
	if (app)
		app->get_wsi().deinit_external();
}

static void context_reset(void)
{
	if (!environ_cb(RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE, &vulkan_interface))
	{
		Granite::libretro_log(RETRO_LOG_ERROR, "Didn't get Vulkan HW interface.");
		delete app;
		app = nullptr;
		return;
	}

	if (vulkan_interface->interface_type != RETRO_HW_RENDER_INTERFACE_VULKAN)
	{
		Granite::libretro_log(RETRO_LOG_ERROR, "Didn't get Vulkan HW interface.");
		delete app;
		app = nullptr;
		return;
	}

	if (vulkan_interface->interface_version != RETRO_HW_RENDER_INTERFACE_VULKAN_VERSION)
	{
		Granite::libretro_log(RETRO_LOG_ERROR, "Didn't get expected Vulkan HW interface version.");
		delete app;
		app = nullptr;
		return;
	}

	unsigned num_images = 0;
	auto sync_mask = vulkan_interface->get_sync_index_mask(vulkan_interface->handle);
	for (unsigned i = 0; i < 32; i++)
	{
		if (sync_mask & (1u << i))
			num_images = i + 1;
	}

	app->get_wsi().init_external(&platform_libretro, std::move(vulkan_context), num_images, app->get_width(), app->get_height());
}

static bool create_device(
		struct retro_vulkan_context *context,
		VkInstance instance,
		VkPhysicalDevice gpu,
		VkSurfaceKHR surface,
		PFN_vkGetInstanceProcAddr get_instance_proc_addr,
		const char **required_device_extensions,
		unsigned num_required_device_extensions,
		const char **required_device_layers,
		unsigned num_required_device_layers,
		const VkPhysicalDeviceFeatures *required_features)
{
	if (!Vulkan::Context::init_loader(get_instance_proc_addr))
		return false;

	vulkan_context.reset(new Vulkan::Context(instance, gpu, surface, required_device_extensions, num_required_device_extensions,
	                                         required_device_layers, num_required_device_layers,
	                                         required_features));

	vulkan_context->release_device();
	context->gpu = vulkan_context->get_gpu();
	context->device = vulkan_context->get_device();
	context->presentation_queue = vulkan_context->get_graphics_queue();
	context->presentation_queue_family_index = vulkan_context->get_graphics_queue_family();
	context->queue = vulkan_context->get_graphics_queue();
	context->queue_family_index = vulkan_context->get_graphics_queue_family();
	return true;
}

static const VkApplicationInfo *get_application_info(void)
{
	static const VkApplicationInfo app = {
			VK_STRUCTURE_TYPE_APPLICATION_INFO,
			nullptr,
			"Granite",
			0,
			"Granite",
			0,
			VK_MAKE_VERSION(1, 0, 59)
	};
	return &app;
}

RETRO_API bool retro_load_game(const struct retro_game_info *)
{
	app = Granite::application_create(0, nullptr);

	hw_render.context_destroy = context_destroy;
	hw_render.context_reset = context_reset;
	hw_render.context_type = RETRO_HW_CONTEXT_VULKAN;
	hw_render.version_major = 1;
	hw_render.version_minor = 0;
	if (!environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw_render))
	{
		Granite::libretro_log(RETRO_LOG_ERROR, "SET_HW_RENDER failed, this core cannot run.\n");
		return false;
	}

	vulkan_negotiation.interface_type = RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN;
	vulkan_negotiation.interface_version = RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN_VERSION;
	vulkan_negotiation.create_device = create_device;
	vulkan_negotiation.destroy_device = nullptr;
	vulkan_negotiation.get_application_info = get_application_info;

	if (!environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE, &vulkan_negotiation))
	{
		Granite::libretro_log(RETRO_LOG_ERROR, "SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE failed, this core cannot run.\n");
		return false;
	}

	EventManager::get_global().dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
	EventManager::get_global().enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Stopped);
	EventManager::get_global().dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
	EventManager::get_global().enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Paused);
	EventManager::get_global().dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
	EventManager::get_global().enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Running);

	return true;
}

RETRO_API bool retro_load_game_special(unsigned, const struct retro_game_info *, size_t)
{
	return false;
}

RETRO_API void retro_unload_game(void)
{
	EventManager::get_global().dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
	EventManager::get_global().enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Paused);
	EventManager::get_global().dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
	EventManager::get_global().enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Stopped);

	delete app;
	app = nullptr;
}

RETRO_API unsigned retro_get_region(void)
{
	return RETRO_REGION_NTSC;
}

RETRO_API void *retro_get_memory_data(unsigned)
{
	return nullptr;
}

RETRO_API size_t retro_get_memory_size(unsigned)
{
	return 0;
}
