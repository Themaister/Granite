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
static Vulkan::ImageViewHandle swapchain_unorm_view;
static Vulkan::ImageHandle swapchain_image;
static Vulkan::Semaphore acquire_semaphore;
static unsigned num_swapchain_images;
static retro_vulkan_image swapchain_image_info;
static bool can_dupe = false;
static retro_usec_t last_frame_time;
static std::string application_name;
static std::string application_internal_resolution;

static unsigned current_width;
static unsigned current_height;

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
		return current_width;
	}

	unsigned get_surface_height() override
	{
		return current_height;
	}

	bool alive(Vulkan::WSI &) override
	{
		return true;
	}

	void poll_input() override
	{
		input_poll_cb();

		auto &tracker = app->get_platform().get_input_tracker();
		const auto poll_key = [&](unsigned index, JoypadKey key, unsigned retro_key) {
			tracker.joypad_key_state(index, key,
			                         input_state_cb(index, RETRO_DEVICE_JOYPAD, 0, retro_key)
			                         ? JoypadKeyState::Pressed : JoypadKeyState::Released);
		};

		const auto poll_axis = [&](unsigned index, JoypadAxis axis, unsigned retro_index, unsigned retro_id) {
			tracker.joyaxis_state(index, axis,
			                      clamp(input_state_cb(index, RETRO_DEVICE_ANALOG,
			                                           retro_index, retro_id) * (1.0f / 0x7fff), -1.0f, 1.0f));
		};

		for (unsigned i = 0; i < 2; i++)
		{
			poll_key(i, JoypadKey::Left, RETRO_DEVICE_ID_JOYPAD_LEFT);
			poll_key(i, JoypadKey::Right, RETRO_DEVICE_ID_JOYPAD_RIGHT);
			poll_key(i, JoypadKey::Up, RETRO_DEVICE_ID_JOYPAD_UP);
			poll_key(i, JoypadKey::Down, RETRO_DEVICE_ID_JOYPAD_DOWN);
			poll_key(i, JoypadKey::Select, RETRO_DEVICE_ID_JOYPAD_SELECT);
			poll_key(i, JoypadKey::Start, RETRO_DEVICE_ID_JOYPAD_START);
			poll_key(i, JoypadKey::L1, RETRO_DEVICE_ID_JOYPAD_L);
			poll_key(i, JoypadKey::L2, RETRO_DEVICE_ID_JOYPAD_L2);
			poll_key(i, JoypadKey::L3, RETRO_DEVICE_ID_JOYPAD_L3);
			poll_key(i, JoypadKey::R1, RETRO_DEVICE_ID_JOYPAD_R);
			poll_key(i, JoypadKey::R2, RETRO_DEVICE_ID_JOYPAD_R2);
			poll_key(i, JoypadKey::R3, RETRO_DEVICE_ID_JOYPAD_R3);
			poll_key(i, JoypadKey::B, RETRO_DEVICE_ID_JOYPAD_B);
			poll_key(i, JoypadKey::A, RETRO_DEVICE_ID_JOYPAD_A);
			poll_key(i, JoypadKey::X, RETRO_DEVICE_ID_JOYPAD_X);
			poll_key(i, JoypadKey::Y, RETRO_DEVICE_ID_JOYPAD_Y);

			poll_axis(i, JoypadAxis::LeftX, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);
			poll_axis(i, JoypadAxis::LeftY, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y);
			poll_axis(i, JoypadAxis::RightX, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X);
			poll_axis(i, JoypadAxis::RightY, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y);
		}

		tracker.dispatch_current_state(app->get_platform().get_frame_timer().get_frame_time());
	}

	bool has_external_swapchain() override
	{
		return true;
	}
};

namespace Granite
{
retro_log_printf_t libretro_log;
// We will handle the platform stuff externally through the libretro implementation.
std::unique_ptr<ApplicationPlatform> create_default_application_platform(unsigned, unsigned)
{
	return std::unique_ptr<ApplicationPlatform>(new ApplicationPlatformLibretro);
}
}

static retro_hw_render_callback hw_render;

RETRO_API void retro_init(void)
{
}

RETRO_API void retro_deinit(void)
{
}

static void setup_variables()
{
	application_name = app->get_name();
	application_internal_resolution = application_name + "_internal_resolution";

	static const retro_variable variables[] = {
			{ application_internal_resolution.c_str(), "Internal resolution; 1280x720|640x360|1280x1024|1920x1080" },
			{ nullptr, nullptr },
	};
	environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES, const_cast<retro_variable *>(variables));
}

static void query_variables()
{
	retro_variable var = { application_internal_resolution.c_str(), nullptr };
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
	{
		unsigned new_width, new_height;
		if (sscanf(var.value, "%ux%u", &new_width, &new_height) == 2)
		{
			current_width = new_width;
			current_height = new_height;
		}
	}
}

RETRO_API void retro_set_environment(retro_environment_t cb)
{
	environ_cb = cb;
	bool support = true;
	environ_cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &support);

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

RETRO_API void retro_get_system_av_info(struct retro_system_av_info *info)
{
	info->timing.fps = 60.0;
	info->timing.sample_rate = 44100.0;
	info->geometry.aspect_ratio = float(current_width) / current_height;
	info->geometry.base_height = current_width;
	info->geometry.base_width = current_height;
	info->geometry.max_width = current_width;
	info->geometry.max_height = current_height;
}

RETRO_API void retro_set_controller_port_device(unsigned, unsigned)
{
}

RETRO_API void retro_reset(void)
{
}

static void check_variables()
{
	bool updated = false;
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
	{
		unsigned old_width = current_width;
		unsigned old_height = current_height;
		query_variables();
		if (old_width != current_width || old_height != current_height)
		{
			retro_system_av_info av_info;
			retro_get_system_av_info(&av_info);
			if (!environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &av_info))
			{
				current_width = old_width;
				current_height = old_height;
			}
		}
	}
}

RETRO_API void retro_run(void)
{
	if (!app)
	{
		// The application is dead, force a shutdown.
		input_poll_cb();
		environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, nullptr);
		return;
	}

	check_variables();

	auto sync_index = vulkan_interface->get_sync_index(vulkan_interface->handle);
	auto &wsi = app->get_wsi();

	// Check if we need to reinitialize the swapchain.
	unsigned num_images = 0;
	auto sync_mask = vulkan_interface->get_sync_index_mask(vulkan_interface->handle);
	for (unsigned i = 0; i < 32; i++)
	{
		if (sync_mask & (1u << i))
			num_images = i + 1;
	}

	if (num_images != num_swapchain_images)
	{
		num_swapchain_images = num_images;
		std::vector<Vulkan::ImageHandle> images;
		for (unsigned i = 0; i < num_images; i++)
			images.push_back(swapchain_image);

		acquire_semaphore.reset();
		wsi.reinit_external_swapchain(std::move(images));
	}

	// Setup the external frame.
	vulkan_interface->wait_sync_index(vulkan_interface->handle);
	wsi.set_external_frame(sync_index, acquire_semaphore, last_frame_time * 1e-6);
	acquire_semaphore.reset();

	// Run frame.
	app->poll();
	app->run_frame();

	// Present to libretro frontend.
	auto signal_semaphore = wsi.get_device().request_semaphore();
	vulkan_interface->set_signal_semaphore(vulkan_interface->handle,
	                                       signal_semaphore->get_semaphore());
	signal_semaphore->signal_external();

	acquire_semaphore = wsi.get_external_release_semaphore();
	if (acquire_semaphore->get_semaphore() != VK_NULL_HANDLE)
	{
		vulkan_interface->set_image(vulkan_interface->handle,
		                            &swapchain_image_info,
		                            1, &acquire_semaphore->get_semaphore(),
		                            VK_QUEUE_FAMILY_IGNORED);

		video_cb(RETRO_HW_FRAME_BUFFER_VALID, current_width, current_height, 0);
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
			                   VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT,
			                   VK_ACCESS_TRANSFER_WRITE_BIT);
			swapchain_image->set_layout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
			cmd->clear_image(*swapchain_image, {});
			cmd->image_barrier(*swapchain_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			                   VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			                   VK_ACCESS_SHADER_READ_BIT);
			swapchain_image->set_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			video_cb(RETRO_HW_FRAME_BUFFER_VALID, current_width, current_height, 0);
			can_dupe = true;
		}
		else
		{
			video_cb(nullptr, current_width, current_height, 0);
		}
	}

	acquire_semaphore = signal_semaphore;
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
	swapchain_unorm_view.reset();
	swapchain_image.reset();
	acquire_semaphore.reset();

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

	app->get_wsi().get_device().set_queue_lock([&]() {
		                                           vulkan_interface->lock_queue(vulkan_interface->handle);
	                                           },
	                                           [&]() {
		                                           vulkan_interface->unlock_queue(vulkan_interface->handle);
	                                           });

	unsigned num_images = 0;
	auto sync_mask = vulkan_interface->get_sync_index_mask(vulkan_interface->handle);
	for (unsigned i = 0; i < 32; i++)
	{
		if (sync_mask & (1u << i))
			num_images = i + 1;
	}
	num_swapchain_images = num_images;

	Vulkan::ImageCreateInfo info = Vulkan::ImageCreateInfo::render_target(current_width, current_height,
	                                                                      VK_FORMAT_R8G8B8A8_SRGB);
	info.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	info.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
	info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	app->get_wsi().get_device().set_context(*vulkan_context);
	swapchain_image = app->get_wsi().get_device().create_image(info, nullptr);
	swapchain_image->set_swapchain_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	can_dupe = false;

	Vulkan::ImageViewCreateInfo view_info;
	view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
	view_info.image = swapchain_image.get();
	swapchain_unorm_view = app->get_wsi().get_device().create_image_view(view_info);

	std::vector<Vulkan::ImageHandle> images;
	for (unsigned i = 0; i < num_images; i++)
		images.push_back(swapchain_image);

	if (!app->get_wsi().init_external(std::move(vulkan_context), std::move(images)))
	{
		Granite::libretro_log(RETRO_LOG_ERROR, "Failed to create external swapchain.");
		delete app;
		app = nullptr;
		return;
	}

	// Setup the swapchain image info for the frontend.
	swapchain_image_info.image_view = swapchain_unorm_view->get_view();
	swapchain_image_info.image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	swapchain_image_info.create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	swapchain_image_info.create_info.image = swapchain_unorm_view->get_image().get_image();
	swapchain_image_info.create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
	swapchain_image_info.create_info.components.r = VK_COMPONENT_SWIZZLE_R;
	swapchain_image_info.create_info.components.g = VK_COMPONENT_SWIZZLE_G;
	swapchain_image_info.create_info.components.b = VK_COMPONENT_SWIZZLE_B;
	swapchain_image_info.create_info.components.a = VK_COMPONENT_SWIZZLE_A;
	swapchain_image_info.create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	swapchain_image_info.create_info.subresourceRange.levelCount = 1;
	swapchain_image_info.create_info.subresourceRange.layerCount = 1;
	swapchain_image_info.create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
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
			application_name.c_str(),
			::app->get_version(),
			"Granite",
			0,
			VK_MAKE_VERSION(1, 0, 59)
	};
	return &app;
}

static void frame_time_callback(retro_usec_t usecs)
{
	last_frame_time = usecs;
}

RETRO_API bool retro_load_game(const struct retro_game_info *)
{
	app = Granite::application_create(0, nullptr);
	current_width = app->get_default_width();
	current_height = app->get_default_height();
	setup_variables();
	query_variables();

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

	retro_frame_time_callback frame_cb = {};
	frame_cb.callback = frame_time_callback;
	frame_cb.reference = (1000000 + 30) / 60;
	last_frame_time = frame_cb.reference;
	environ_cb(RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK, &frame_cb);

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
