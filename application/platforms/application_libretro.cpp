/* Copyright (c) 2017-2020 Hans-Kristian Arntzen
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
#include "application_wsi.hpp"
#include "muglm/muglm_impl.hpp"

using namespace Granite;

static Application *app;
static retro_environment_t environ_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;

static retro_usec_t last_frame_time;
static std::string application_name;
static std::string application_internal_resolution;

static unsigned current_width;
static unsigned current_height;

struct WSIPlatformLibretro : Granite::GraniteWSIPlatform
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

		auto &tracker = get_input_tracker();
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

		const auto poll_axis_button = [&](unsigned index, JoypadAxis axis, unsigned retro_key) {
			tracker.joyaxis_state(index, axis,
			                      input_state_cb(index, RETRO_DEVICE_JOYPAD, 0, retro_key) ? 1.0f : 0.0f);
		};

		tracker.enable_joypad(0);
		tracker.enable_joypad(1);
		for (unsigned i = 0; i < 2; i++)
		{
			poll_key(i, JoypadKey::Left, RETRO_DEVICE_ID_JOYPAD_LEFT);
			poll_key(i, JoypadKey::Right, RETRO_DEVICE_ID_JOYPAD_RIGHT);
			poll_key(i, JoypadKey::Up, RETRO_DEVICE_ID_JOYPAD_UP);
			poll_key(i, JoypadKey::Down, RETRO_DEVICE_ID_JOYPAD_DOWN);
			poll_key(i, JoypadKey::Select, RETRO_DEVICE_ID_JOYPAD_SELECT);
			poll_key(i, JoypadKey::Start, RETRO_DEVICE_ID_JOYPAD_START);
			poll_key(i, JoypadKey::LeftShoulder, RETRO_DEVICE_ID_JOYPAD_L);
			poll_key(i, JoypadKey::LeftThumb, RETRO_DEVICE_ID_JOYPAD_L3);
			poll_key(i, JoypadKey::RightShoulder, RETRO_DEVICE_ID_JOYPAD_R);
			poll_key(i, JoypadKey::RightThumb, RETRO_DEVICE_ID_JOYPAD_R3);
			poll_key(i, JoypadKey::South, RETRO_DEVICE_ID_JOYPAD_B);
			poll_key(i, JoypadKey::East, RETRO_DEVICE_ID_JOYPAD_A);
			poll_key(i, JoypadKey::North, RETRO_DEVICE_ID_JOYPAD_X);
			poll_key(i, JoypadKey::West, RETRO_DEVICE_ID_JOYPAD_Y);

			poll_axis(i, JoypadAxis::LeftX, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);
			poll_axis(i, JoypadAxis::LeftY, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y);
			poll_axis(i, JoypadAxis::RightX, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X);
			poll_axis(i, JoypadAxis::RightY, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y);

			poll_axis_button(i, JoypadAxis::LeftTrigger, RETRO_DEVICE_ID_JOYPAD_L2);
			poll_axis_button(i, JoypadAxis::RightTrigger, RETRO_DEVICE_ID_JOYPAD_R2);
		}

		tracker.dispatch_current_state(app->get_platform().get_frame_timer().get_frame_time());
	}

	bool has_external_swapchain() override
	{
		return true;
	}
};

static retro_hw_render_callback hw_render;

RETRO_API void retro_init(void)
{
	Global::init(Global::MANAGER_FEATURE_ALL_BITS & ~Global::MANAGER_FEATURE_AUDIO_BIT);
}

RETRO_API void retro_deinit(void)
{
	Global::deinit();
}

static void setup_variables()
{
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
	info->library_name = "Sample Scene Viewer";
	info->library_version = "0.0";
	info->need_fullpath = true;
	info->valid_extensions = "gltf|glb|scene";
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
			libretro_set_swapchain_size(current_width, current_height);
			if (!environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &av_info))
			{
				current_width = old_width;
				current_height = old_height;
				libretro_set_swapchain_size(current_width, current_height);
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

	// Begin frame.
	libretro_begin_frame(app->get_wsi(), last_frame_time);

	// Run frame.
	app->poll();
	app->run_frame();

	// Present.
	libretro_end_frame(video_cb, app->get_wsi());
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
	libretro_context_destroy(app ? &app->get_wsi() : nullptr);
}

static void context_reset(void)
{
	retro_hw_render_interface_vulkan *vulkan;
	if (!environ_cb(RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE, &vulkan))
	{
		Granite::libretro_log(RETRO_LOG_ERROR, "Didn't get Vulkan HW interface.");
		delete app;
		app = nullptr;
		return;
	}

	if (!libretro_context_reset(vulkan, app->get_wsi()))
	{
		Granite::libretro_log(RETRO_LOG_ERROR, "Failed to reset Vulkan context.");
		delete app;
		app = nullptr;
		return;
	}
}

static void frame_time_callback(retro_usec_t usecs)
{
	last_frame_time = usecs;
}

RETRO_API bool retro_load_game(const struct retro_game_info *info)
{
	char *argv[] = {
		const_cast<char *>("libretro-granite"),
		const_cast<char *>(info->path),
		nullptr,
	};

	app = Granite::application_create(2, argv);
	if (!app)
	{
		Granite::libretro_log(RETRO_LOG_ERROR, "Failed to load scene: %s\n", info->path);
		return false;
	}

	current_width = app->get_default_width();
	current_height = app->get_default_height();

	if (!app->init_wsi(std::make_unique<WSIPlatformLibretro>()))
	{
		Granite::libretro_log(RETRO_LOG_ERROR, "Failed to init platform.");
		delete app;
		return false;
	}

	application_name = app->get_name();
	libretro_set_application_info(application_name.c_str(), app->get_version());

	setup_variables();
	query_variables();
	libretro_set_swapchain_size(current_width, current_height);

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

	if (!libretro_load_game(environ_cb))
	{
		Granite::libretro_log(RETRO_LOG_ERROR, "Failed to set up Vulkan application, this core cannot run.\n");
		return false;
	}

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
	libretro_unload_game();
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

namespace Granite
{
void application_dummy()
{
}
}
