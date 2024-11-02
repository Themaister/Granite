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

#include "global_managers_init.hpp"
#include "game-activity/GameActivity.h"
#include "game-activity/native_app_glue/android_native_app_glue.h"
#include "paddleboat/paddleboat.h"
#include "logging.hpp"
#include "application.hpp"
#include "application_events.hpp"
#include "application_wsi.hpp"
#include "context.hpp"
#include "string_helpers.hpp"
#include <jni.h>
#include <android/sensor.h>
#include <android/window.h>
#if defined(HAVE_SWAPPY)
#include "swappy/swappyVk.h"
#endif

#include "android.hpp"
#include "os_filesystem.hpp"
#include "rapidjson_wrapper.hpp"
#include "muglm/muglm_impl.hpp"

#ifdef AUDIO_HAVE_OPENSL
#include "audio_opensl.hpp"
#endif
#ifdef AUDIO_HAVE_OBOE
#include "audio_oboe.hpp"
#endif

using namespace Vulkan;

#define SENSOR_GAME_ROTATION_VECTOR 15

namespace Granite
{
uint32_t android_api_version;

void application_dummy()
{
}

// Alternatively, make sure this is linked in.
// Implementation is here to trick a linker to always let main() in static library work.
void application_setup_default_filesystem(const char *default_asset_directory)
{
	auto *filesystem = GRANITE_FILESYSTEM();
	if (filesystem)
		Filesystem::setup_default_filesystem(filesystem, default_asset_directory);
}

struct GlobalState
{
	android_app *app;
	int32_t base_width;
	int32_t base_height;
	int display_rotation;
	bool has_window;
	bool active;
	bool content_rect_changed;
};

struct Config
{
	unsigned target_width = 0;
	unsigned target_height = 0;
	bool support_prerotate = true;
	bool support_gyro = false;
};

struct JNI
{
	JNIEnv *env;
	jclass granite;
	jmethodID getDisplayRotation;
	jmethodID getAudioNativeSampleRate;
	jmethodID getAudioNativeBlockFrames;
	jmethodID getCommandLineArgument;
	jclass classLoaderClass;
	jobject classLoader;

	ASensorEventQueue *sensor_queue;
	const ASensor *rotation_sensor;
};

static GlobalState global_state;
static Config global_config;
static JNI jni;

static void on_window_resized(android_app *app)
{
	if (app->window)
	{
		auto new_width = ANativeWindow_getWidth(app->window);
		auto new_height = ANativeWindow_getHeight(app->window);
		if (new_width != global_state.base_width || new_height != global_state.base_height)
		{
			global_state.base_width = new_width;
			global_state.base_height = new_height;
			global_state.content_rect_changed = true;
		}
	}
}

static void on_content_rect_changed(GameActivity *, const ARect *rect)
{
	global_state.base_width = rect->right - rect->left;
	global_state.base_height = rect->bottom - rect->top;
	global_state.content_rect_changed = true;
	LOGI("Got content rect: %d x %d\n", global_state.base_width, global_state.base_height);
}

namespace App
{
static std::string getCommandLine()
{
	std::string result;

	jstring key = jni.env->NewStringUTF("granite");
	jstring str = static_cast<jstring>(jni.env->CallObjectMethod(global_state.app->activity->javaGameActivity,
	                                                             jni.getCommandLineArgument,
	                                                             key));
	if (str)
	{
		const char *data = jni.env->GetStringUTFChars(str, nullptr);
		if (data)
		{
			result = data;
			jni.env->ReleaseStringUTFChars(str, data);
		}
		else
			LOGE("Failed to get JNI string data.\n");
	}
	else
		LOGE("Failed to get JNI string from getCommandLine().\n");

	return result;
}

#ifdef HAVE_GRANITE_AUDIO
static int getAudioNativeSampleRate()
{
	int ret = jni.env->CallIntMethod(global_state.app->activity->javaGameActivity, jni.getAudioNativeSampleRate);
	return ret;
}

static int getAudioNativeBlockFrames()
{
	int ret = jni.env->CallIntMethod(global_state.app->activity->javaGameActivity, jni.getAudioNativeBlockFrames);
	return ret;
}
#endif

static int getDisplayRotation()
{
	int ret = jni.env->CallIntMethod(global_state.app->activity->javaGameActivity, jni.getDisplayRotation);
	return ret;
}
}

struct WSIPlatformAndroid : Granite::GraniteWSIPlatform
{
	bool init(unsigned width_, unsigned height_)
	{
		width = width_;
		height = height_;
		VK_ASSERT(global_state.base_width && global_state.base_height);

		if (width == 0 && height != 0)
		{
			width = unsigned(std::round(float(height) * get_aspect_ratio()));
			LOGI("Adjusting width to %u pixels based on aspect ratio.\n", width);
		}

		if (width != 0 && height == 0)
		{
			height = unsigned(std::round(float(width) / get_aspect_ratio()));
			LOGI("Adjusting height to %u pixels based on aspect ratio.\n", height);
		}

		if (!Vulkan::Context::init_loader(nullptr))
		{
			LOGE("Failed to init Vulkan loader.\n");
			return false;
		}

		get_input_tracker().set_touch_resolution(width, height);
		has_window = global_state.has_window;
		active = global_state.active;

		return true;
	}

#if defined(HAVE_SWAPPY)
	VkDevice current_device = VK_NULL_HANDLE;
#endif

	void event_swapchain_created(Device *device_, VkSwapchainKHR swapchain, unsigned width_, unsigned height_, float aspect_, size_t count_,
	                             VkFormat format_, VkColorSpaceKHR color_space_, VkSurfaceTransformFlagBitsKHR transform_) override
	{
#if defined(HAVE_SWAPPY)
		current_device = device_->get_device();

		uint64_t refresh = 0;
		if (SwappyVk_initAndGetRefreshCycleDuration(jni.env, global_state.app->activity->javaGameActivity,
		                                            device_->get_physical_device(), device_->get_device(),
		                                            swapchain, &refresh))
		{
			LOGI("Swappy reported refresh duration of %.3f ms.\n", double(refresh) * 1e-6);
		}
		else
			LOGW("Failed to initialize swappy refresh rate.\n");

		SwappyVk_setWindow(current_device, swapchain, global_state.app->window);
#endif

		Granite::GraniteWSIPlatform::event_swapchain_created(device_, swapchain, width_, height_,
		                                                     aspect_, count_, format_,
		                                                     color_space_, transform_);

		if (transform_ & (VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR |
		                  VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR |
		                  VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_90_BIT_KHR |
		                  VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_270_BIT_KHR))
		{
			std::swap(width_, height_);
		}
		get_input_tracker().set_touch_resolution(width_, height_);
	}

	void destroy_swapchain_resources(VkSwapchainKHR swapchain) override
	{
		(void)swapchain;
#if defined(HAVE_SWAPPY)
		if (current_device && swapchain)
		{
			SwappyVk_destroySwapchain(current_device, swapchain);
			current_device = VK_NULL_HANDLE;
		}
#endif
	}

	void update_orientation();
	bool alive(Vulkan::WSI &wsi) override;
	void poll_input() override;
	void poll_input_async(Granite::InputTrackerHandler *override_handler) override;

	void request_teardown();
	void gamepad_update();

	std::vector<const char *> get_instance_extensions() override
	{
		return { "VK_KHR_surface", "VK_KHR_android_surface" };
	}

	uint32_t get_surface_width() override
	{
		return width;
	}

	uint32_t get_surface_height() override
	{
		return height;
	}

	float get_aspect_ratio() override
	{
		return float(global_state.base_width) / global_state.base_height;
	}

	VkSurfaceKHR create_surface(VkInstance instance, VkPhysicalDevice) override;

	unsigned width = 0, height = 0;
	Application *app = nullptr;
	Vulkan::WSI *app_wsi = nullptr;
	uint64_t active_axes = 0;
	bool active = false;
	bool has_window = true;
	bool wsi_idle = false;
	bool requesting_teardown = false;

	bool pending_native_window_init = false;
	bool pending_native_window_term = false;
	bool pending_config_change = false;
	bool has_mouse_input = false;
};

static VkSurfaceKHR create_surface_from_native_window(VkInstance instance, ANativeWindow *window)
{
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	VkAndroidSurfaceCreateInfoKHR create_info = { VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR };
	create_info.window = window;

	auto gpa = Vulkan::Context::get_instance_proc_addr();
#define SYM(x) PFN_##x p_##x; do { PFN_vkVoidFunction vf = gpa(instance, #x); memcpy(&p_##x, &vf, sizeof(vf)); } while(0)
	SYM(vkCreateAndroidSurfaceKHR);
	if (p_vkCreateAndroidSurfaceKHR(instance, &create_info, nullptr, &surface) != VK_SUCCESS)
		return VK_NULL_HANDLE;
	return surface;
}

static void enable_sensors()
{
	if (!jni.rotation_sensor || !jni.sensor_queue)
		return;

	int min_delay = ASensor_getMinDelay(jni.rotation_sensor);
	ASensorEventQueue_enableSensor(jni.sensor_queue, jni.rotation_sensor);
	if (ASensorEventQueue_setEventRate(jni.sensor_queue, jni.rotation_sensor, std::max(8000, min_delay)) < 0)
		LOGE("Failed to set event rate.\n");
}

static void disable_sensors()
{
	if (!jni.rotation_sensor || !jni.sensor_queue)
		return;

	ASensorEventQueue_disableSensor(jni.sensor_queue, jni.rotation_sensor);
}

static void handle_sensors()
{
	if (!global_state.app->userData)
		return;

	auto &state = *static_cast<WSIPlatformAndroid *>(global_state.app->userData);

	if (!ASensorEventQueue_hasEvents(jni.sensor_queue))
		return;

	ASensorEvent events[64];
	for (;;)
	{
		int count = ASensorEventQueue_getEvents(jni.sensor_queue, events, 64);
		if (count <= 0)
			return;

		for (int i = 0; i < count; i++)
		{
			auto &event = events[i];
			if (event.type == SENSOR_GAME_ROTATION_VECTOR)
			{
				quat q(event.data[3], -event.data[0], -event.data[1], -event.data[2]);

				// Compensate for different display rotation.
				if (global_state.display_rotation == 1)
				{
					std::swap(q.x, q.y);
					q.x = -q.x;
				}
				else if (global_state.display_rotation == 2)
				{
					// Doesn't seem to be possible to trigger this?
					LOGE("Untested orientation %u!\n", global_state.display_rotation);
				}
				else if (global_state.display_rotation == 3)
				{
					std::swap(q.x, q.y);
					q.y = -q.y;
				}

				static const quat landscape(muglm::one_over_root_two<float>(), muglm::one_over_root_two<float>(), 0.0f, 0.0f);
				q = conjugate(normalize(q * landscape));
				state.get_input_tracker().orientation_event(q);
			}
		}
	}
}

static void engine_handle_input(WSIPlatformAndroid &state)
{
	auto *input_buffer = android_app_swap_input_buffers(global_state.app);

	if (!input_buffer)
		return;

	for (uint32_t i = 0; i < input_buffer->keyEventsCount; i++)
	{
		auto &event = input_buffer->keyEvents[i];

		auto action = event.action;
		auto code = event.keyCode;

		if (Paddleboat_isInitialized())
			if (Paddleboat_processGameActivityKeyInputEvent(&event, sizeof(event)))
				continue;

		if (event.source == AINPUT_SOURCE_KEYBOARD)
		{
			if (action == AKEY_EVENT_ACTION_DOWN && code == AKEYCODE_BACK)
			{
				LOGI("Requesting teardown.\n");
				state.requesting_teardown = true;
			}
		}
	}

	if (input_buffer->keyEventsCount)
		android_app_clear_key_events(input_buffer);

	for (uint32_t i = 0; i < input_buffer->motionEventsCount; i++)
	{
		auto &event = input_buffer->motionEvents[i];

		auto action = event.action & AMOTION_EVENT_ACTION_MASK;
		auto index = (event.action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
		auto source = event.source;

		// Paddleboat eats mouse events, and we want to handle them raw.
		if (source == AINPUT_SOURCE_MOUSE)
		{
			// TODO: Does Android have concept of focus?
			if (!state.has_mouse_input)
			{
				state.get_input_tracker().mouse_enter(0.0, 0.0);
				state.has_mouse_input = true;
			}

			switch (action)
			{
			case AMOTION_EVENT_ACTION_MOVE:
			case AMOTION_EVENT_ACTION_HOVER_MOVE:
			{
				auto x = GameActivityPointerAxes_getX(&event.pointers[index]);
				auto y = GameActivityPointerAxes_getY(&event.pointers[index]);
				x /= float(global_state.base_width);
				y /= float(global_state.base_height);
				state.get_input_tracker().mouse_move_event_absolute_normalized(x, y);
				break;
			}

			case AMOTION_EVENT_ACTION_DOWN:
			case AMOTION_EVENT_ACTION_POINTER_DOWN:
			{
				auto x = GameActivityPointerAxes_getX(&event.pointers[index]);
				auto y = GameActivityPointerAxes_getY(&event.pointers[index]);
				x /= float(global_state.base_width);
				y /= float(global_state.base_height);
				if (event.buttonState & AMOTION_EVENT_BUTTON_PRIMARY)
					state.get_input_tracker().mouse_button_event_normalized(MouseButton::Left, x, y, true);
				if (event.buttonState & AMOTION_EVENT_BUTTON_SECONDARY)
					state.get_input_tracker().mouse_button_event_normalized(MouseButton::Right, x, y, true);
				break;
			}

			case AMOTION_EVENT_ACTION_UP:
			case AMOTION_EVENT_ACTION_POINTER_UP:
			{
				if (!(event.buttonState & AMOTION_EVENT_BUTTON_PRIMARY))
					state.get_input_tracker().mouse_button_event(MouseButton::Left, false);
				if (!(event.buttonState & AMOTION_EVENT_BUTTON_SECONDARY))
					state.get_input_tracker().mouse_button_event(MouseButton::Right, false);
				break;
			}

			default:
				break;
			}

			continue;
		}

		if (Paddleboat_isInitialized())
			if (Paddleboat_processGameActivityMotionInputEvent(&event, sizeof(event)))
				continue;

		if (source == AINPUT_SOURCE_TOUCHSCREEN)
		{
			switch (action)
			{
				case AMOTION_EVENT_ACTION_DOWN:
				case AMOTION_EVENT_ACTION_POINTER_DOWN:
				{
					auto x = GameActivityPointerAxes_getX(&event.pointers[index]);
					auto y = GameActivityPointerAxes_getY(&event.pointers[index]);
					x /= float(global_state.base_width);
					y /= float(global_state.base_height);
					int id = event.pointers[index].id;
					state.get_input_tracker().on_touch_down(id, x, y);
					break;
				}

				case AMOTION_EVENT_ACTION_MOVE:
				{
					size_t count = event.pointerCount;
					for (size_t p = 0; p < count; p++)
					{
						// Divide by base_width / base_height?
						auto x = GameActivityPointerAxes_getX(&event.pointers[p]);
						auto y = GameActivityPointerAxes_getY(&event.pointers[p]);
						x /= float(global_state.base_width);
						y /= float(global_state.base_height);
						int id = event.pointers[p].id;
						state.get_input_tracker().on_touch_move(id, x, y);
					}
					state.get_input_tracker().dispatch_touch_gesture();
					break;
				}

				case AMOTION_EVENT_ACTION_UP:
				case AMOTION_EVENT_ACTION_POINTER_UP:
				{
					auto x = GameActivityPointerAxes_getX(&event.pointers[index]);
					auto y = GameActivityPointerAxes_getY(&event.pointers[index]);
					x /= float(global_state.base_width);
					y /= float(global_state.base_height);
					int id = event.pointers[index].id;
					state.get_input_tracker().on_touch_up(id, x, y);
					break;
				}

				default:
					break;
			}
		}
	}

	if (input_buffer->motionEventsCount)
		android_app_clear_motion_events(input_buffer);
}

static void engine_handle_cmd_init(android_app *app, int32_t cmd)
{
	switch (cmd)
	{
	case APP_CMD_RESUME:
	{
		LOGI("Lifecycle resume\n");
		enable_sensors();
		GRANITE_EVENT_MANAGER()->dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
		GRANITE_EVENT_MANAGER()->enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Running);
		global_state.active = true;
		Granite::Global::start_audio_system();
		break;
	}

	case APP_CMD_PAUSE:
	{
		LOGI("Lifecycle pause\n");
		disable_sensors();
		GRANITE_EVENT_MANAGER()->dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
		GRANITE_EVENT_MANAGER()->enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Paused);
		global_state.active = false;
		Granite::Global::stop_audio_system();
		break;
	}

	case APP_CMD_START:
	{
		LOGI("Lifecycle start\n");
		GRANITE_EVENT_MANAGER()->dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
		GRANITE_EVENT_MANAGER()->enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Paused);
		if (jni.env && Paddleboat_isInitialized())
			Paddleboat_onStart(jni.env);
		break;
	}

	case APP_CMD_STOP:
	{
		LOGI("Lifecycle stop\n");
		GRANITE_EVENT_MANAGER()->dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
		GRANITE_EVENT_MANAGER()->enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Stopped);
		if (jni.env && Paddleboat_isInitialized())
			Paddleboat_onStop(jni.env);
		break;
	}

	case APP_CMD_INIT_WINDOW:
	{
		global_state.has_window = app->window != nullptr;
		if (app->window)
		{
			LOGI("Init window\n");
			global_state.base_width = ANativeWindow_getWidth(app->window);
			global_state.base_height = ANativeWindow_getHeight(app->window);
			global_state.content_rect_changed = true;
		}

		global_state.display_rotation = jni.env->CallIntMethod(app->activity->javaGameActivity, jni.getDisplayRotation);
		break;
	}

	case APP_CMD_WINDOW_RESIZED:
	{
		on_window_resized(app);
		break;
	}

	case APP_CMD_CONTENT_RECT_CHANGED:
	{
		on_content_rect_changed(app->activity, &app->contentRect);
		break;
	}

	default:
		break;
	}
}

static void engine_handle_cmd(android_app *app, int32_t cmd)
{
	if (!app->userData)
		return;
	auto &state = *static_cast<WSIPlatformAndroid *>(app->userData);

	switch (cmd)
	{
	case APP_CMD_RESUME:
	{
		LOGI("Lifecycle resume\n");
		GRANITE_EVENT_MANAGER()->dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
		GRANITE_EVENT_MANAGER()->enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Running);
		enable_sensors();
		Granite::Global::start_audio_system();

		state.active = true;
		if (state.wsi_idle)
		{
			state.get_frame_timer().leave_idle();
			state.wsi_idle = false;
		}
		break;
	}

	case APP_CMD_PAUSE:
	{
		LOGI("Lifecycle pause\n");
		GRANITE_EVENT_MANAGER()->dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
		GRANITE_EVENT_MANAGER()->enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Paused);
		disable_sensors();
		Granite::Global::stop_audio_system();

		state.active = false;
		state.get_frame_timer().enter_idle();
		state.wsi_idle = true;
		break;
	}

	case APP_CMD_START:
	{
		LOGI("Lifecycle start\n");
		GRANITE_EVENT_MANAGER()->dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
		GRANITE_EVENT_MANAGER()->enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Paused);
		if (jni.env && Paddleboat_isInitialized())
			Paddleboat_onStart(jni.env);
		break;
	}

	case APP_CMD_STOP:
	{
		LOGI("Lifecycle stop\n");
		GRANITE_EVENT_MANAGER()->dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
		GRANITE_EVENT_MANAGER()->enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Stopped);
		if (jni.env && Paddleboat_isInitialized())
			Paddleboat_onStop(jni.env);
		break;
	}

	case APP_CMD_INIT_WINDOW:
		if (app->window != nullptr)
		{
			state.has_window = true;
			LOGI("New window size %d x %d\n", global_state.base_width, global_state.base_height);
			global_state.base_width = ANativeWindow_getWidth(app->window);
			global_state.base_height = ANativeWindow_getHeight(app->window);
			global_state.content_rect_changed = false;

			if (state.app_wsi)
			{
				LOGI("Lifecycle init window.\n");
				auto surface = create_surface_from_native_window(state.app_wsi->get_context().get_instance(), app->window);
				state.app_wsi->reinit_surface_and_swapchain(surface);
			}
			else
			{
				LOGI("Pending init window.\n");
				state.pending_native_window_init = true;
			}
		}
		break;

	case APP_CMD_TERM_WINDOW:
		state.has_window = false;
		if (state.app_wsi)
		{
			LOGI("Lifecycle deinit window.\n");
			state.app_wsi->deinit_surface_and_swapchain();
		}
		else
		{
			LOGI("Pending deinit window.\n");
			state.pending_native_window_term = true;
		}
		break;

	case APP_CMD_WINDOW_RESIZED:
	{
		on_window_resized(app);
		break;
	}

	case APP_CMD_CONTENT_RECT_CHANGED:
	{
		on_content_rect_changed(app->activity, &app->contentRect);
		break;
	}

	default:
		break;
	}
}

VkSurfaceKHR WSIPlatformAndroid::create_surface(VkInstance instance, VkPhysicalDevice)
{
	return create_surface_from_native_window(instance, global_state.app->window);
}

void WSIPlatformAndroid::update_orientation()
{
	global_state.display_rotation = App::getDisplayRotation();
	LOGI("Got new rotation: %d\n", global_state.display_rotation);
	LOGI("Got new resolution: %d x %d\n", global_state.base_width, global_state.base_height);
	pending_config_change = true;
}

void WSIPlatformAndroid::request_teardown()
{
	requesting_teardown = true;
}

void WSIPlatformAndroid::gamepad_update()
{
	if (jni.env && Paddleboat_isInitialized())
		Paddleboat_update(jni.env);

	// Need to explicitly enables axes we care about.
	const uint64_t new_active_axes = Paddleboat_getActiveAxisMask();
	uint64_t new_axes = new_active_axes ^ active_axes;

	if (new_axes != 0)
	{
		active_axes = new_active_axes;
		int32_t axis_index = 0;

		while (new_axes != 0)
		{
			if ((new_axes & 1) != 0)
			{
				LOGI("Enable Axis: %d", axis_index);
				GameActivityPointerAxes_enableAxis(axis_index);
			}
			axis_index++;
			new_axes >>= 1;
		}
	}

	auto &tracker = get_input_tracker();
	for (int i = 0; i < PADDLEBOAT_MAX_CONTROLLERS; i++)
	{
		if (Paddleboat_getControllerStatus(i) != PADDLEBOAT_CONTROLLER_ACTIVE)
			continue;

		Paddleboat_Controller_Info info = {};
		Paddleboat_getControllerInfo(i, &info);
		bool known_layout = false;

		switch (info.controllerFlags & PADDLEBOAT_CONTROLLER_LAYOUT_MASK)
		{
		case PADDLEBOAT_CONTROLLER_LAYOUT_SHAPES:
		case PADDLEBOAT_CONTROLLER_LAYOUT_STANDARD:
			known_layout = true;
			break;

		default:
			break;
		}

		if (!known_layout)
			continue;

		Paddleboat_Controller_Data data = {};
		Paddleboat_getControllerData(i, &data);

		struct Mapping
		{
			JoypadKey key;
			uint32_t mask;
		};
		static const Mapping map[] = {
			{ JoypadKey::Left, PADDLEBOAT_BUTTON_DPAD_LEFT },
			{ JoypadKey::Right, PADDLEBOAT_BUTTON_DPAD_RIGHT },
			{ JoypadKey::Up, PADDLEBOAT_BUTTON_DPAD_UP },
			{ JoypadKey::Down, PADDLEBOAT_BUTTON_DPAD_DOWN },
			{ JoypadKey::West, PADDLEBOAT_BUTTON_X },
			{ JoypadKey::East, PADDLEBOAT_BUTTON_B },
			{ JoypadKey::North, PADDLEBOAT_BUTTON_Y },
			{ JoypadKey::South, PADDLEBOAT_BUTTON_A },
			{ JoypadKey::Start, PADDLEBOAT_BUTTON_START },
			{ JoypadKey::Select, PADDLEBOAT_BUTTON_SELECT },
			{ JoypadKey::LeftShoulder, PADDLEBOAT_BUTTON_L1 },
			{ JoypadKey::RightShoulder, PADDLEBOAT_BUTTON_R1 },
			{ JoypadKey::LeftThumb, PADDLEBOAT_BUTTON_L3 },
			{ JoypadKey::RightThumb, PADDLEBOAT_BUTTON_R3 },
		};

		for (auto &m : map)
		{
			tracker.joypad_key_state(i, m.key,
									 (data.buttonsDown & m.mask) != 0 ?
									 JoypadKeyState::Pressed : JoypadKeyState::Released);
		}

		tracker.joyaxis_state(i, JoypadAxis::LeftX, data.leftStick.stickX);
		tracker.joyaxis_state(i, JoypadAxis::LeftY, data.leftStick.stickY);
		tracker.joyaxis_state(i, JoypadAxis::RightX, data.rightStick.stickX);
		tracker.joyaxis_state(i, JoypadAxis::RightY, data.rightStick.stickY);
		tracker.joyaxis_state(i, JoypadAxis::LeftTrigger, data.triggerL2);
		tracker.joyaxis_state(i, JoypadAxis::RightTrigger, data.triggerR2);
	}
}

void WSIPlatformAndroid::poll_input()
{
	std::lock_guard<std::mutex> holder{get_input_tracker().get_lock()};
	int events;
	int ident;
	android_poll_source *source;
	app_wsi = nullptr;

	while ((ident = ALooper_pollAll(0, nullptr, &events, reinterpret_cast<void **>(&source))) >= 0)
	{
		if (source)
			source->process(global_state.app, source);

		if (ident == LOOPER_ID_USER)
			handle_sensors();

		if (global_state.app->destroyRequested)
			return;
	}

	gamepad_update();
	engine_handle_input(*this);
	get_input_tracker().dispatch_current_state(get_frame_timer().get_frame_time());
}

void WSIPlatformAndroid::poll_input_async(Granite::InputTrackerHandler *override_handler)
{
	// Not really used on Android, so implement it in the trivial way.
	std::lock_guard<std::mutex> holder{get_input_tracker().get_lock()};
	get_input_tracker().dispatch_current_state(0.0, override_handler);
}

bool WSIPlatformAndroid::alive(Vulkan::WSI &wsi)
{
	auto &state = *static_cast<WSIPlatformAndroid *>(global_state.app->userData);
	int events;
	int ident;
	android_poll_source *source;
	state.app_wsi = &wsi;

	if (global_state.app->destroyRequested || requesting_teardown)
		return false;

	bool once = false;

	if (global_state.content_rect_changed)
	{
		update_orientation();
		global_state.content_rect_changed = false;
	}

	if (state.pending_config_change)
	{
		state.pending_native_window_term = true;
		state.pending_native_window_init = true;
		state.pending_config_change = false;
	}

	const auto flush_pending = [&]() {
		if (state.pending_native_window_term)
		{
			LOGI("Pending native window term\n");
			wsi.deinit_surface_and_swapchain();
			state.pending_native_window_term = false;
		}

		if (state.pending_native_window_init)
		{
			LOGI("Pending native window init\n");
			auto surface = create_surface_from_native_window(wsi.get_context().get_instance(), global_state.app->window);
			wsi.reinit_surface_and_swapchain(surface);
			state.pending_native_window_init = false;
		}
	};

	flush_pending();

	while (!once || !state.active || !state.has_window)
	{
		while ((ident = ALooper_pollAll((state.has_window && state.active) ? 0 : -1,
										nullptr, &events, reinterpret_cast<void **>(&source))) >= 0)
		{
			if (source)
				source->process(global_state.app, source);

			if (ident == LOOPER_ID_USER)
				handle_sensors();

			if (global_state.app->destroyRequested)
				return false;
		}

		once = true;
	}

	flush_pending();

	return true;
}

static void deinit_jni()
{
	if (jni.env && Paddleboat_isInitialized())
		Paddleboat_destroy(jni.env);

	if (jni.env && global_state.app)
	{
		global_state.app->activity->vm->DetachCurrentThread();
		jni.env = nullptr;
	}
}

void paddleboat_controller_status_cb(
	const int32_t controllerIndex,
	const Paddleboat_ControllerStatus controllerStatus, void *)
{
	if (controllerStatus == PADDLEBOAT_CONTROLLER_JUST_CONNECTED)
	{
		char name[1024];
		*name = '\0';
		Paddleboat_getControllerName(controllerIndex, sizeof(name), name);
		LOGI("Controller #%u (%s) connected.\n", controllerIndex, name);
		auto *platform = static_cast<WSIPlatformAndroid *>(global_state.app->userData);
		if (platform)
			platform->get_input_tracker().enable_joypad(controllerIndex, 0, 0 /* todo */);

	}
	else if (controllerStatus == PADDLEBOAT_CONTROLLER_JUST_DISCONNECTED)
	{
		LOGI("Controller #%u disconnected.\n", controllerIndex);
		auto *platform = static_cast<WSIPlatformAndroid *>(global_state.app->userData);
		if (platform)
			platform->get_input_tracker().disable_joypad(controllerIndex, 0, 0 /* todo */);
	}
}

static void init_jni()
{
	auto *app = global_state.app;
	app->activity->vm->AttachCurrentThread(&jni.env, nullptr);

	if (Paddleboat_init(jni.env, app->activity->javaGameActivity) != PADDLEBOAT_NO_ERROR)
		LOGE("Failed to initialize Paddleboat.\n");
	else if (!Paddleboat_isInitialized())
		LOGE("Paddleboat is not initialized.\n");
	else
		Paddleboat_setControllerStatusCallback(paddleboat_controller_status_cb, nullptr);

	jclass clazz = jni.env->GetObjectClass(app->activity->javaGameActivity);
	jmethodID getApplication = jni.env->GetMethodID(clazz, "getApplication", "()Landroid/app/Application;");
	jobject application = jni.env->CallObjectMethod(app->activity->javaGameActivity, getApplication);

	jclass applicationClass = jni.env->GetObjectClass(application);
	jmethodID getApplicationContext = jni.env->GetMethodID(applicationClass, "getApplicationContext", "()Landroid/content/Context;");
	jobject context = jni.env->CallObjectMethod(application, getApplicationContext);

	jclass contextClass = jni.env->GetObjectClass(context);
	jmethodID getClassLoader = jni.env->GetMethodID(contextClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
	jni.classLoader = jni.env->CallObjectMethod(context, getClassLoader);

	jni.classLoaderClass = jni.env->GetObjectClass(jni.classLoader);
	jmethodID loadClass = jni.env->GetMethodID(jni.classLoaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");

	jstring granite_str = jni.env->NewStringUTF("net.themaister.granite.GraniteActivity");
	jni.granite = static_cast<jclass>(jni.env->CallObjectMethod(jni.classLoader, loadClass, granite_str));

	jni.getDisplayRotation = jni.env->GetMethodID(jni.granite, "getDisplayRotation", "()I");
	jni.getAudioNativeSampleRate = jni.env->GetMethodID(jni.granite, "getAudioNativeSampleRate", "()I");
	jni.getAudioNativeBlockFrames = jni.env->GetMethodID(jni.granite, "getAudioNativeBlockFrames", "()I");
	jni.getCommandLineArgument = jni.env->GetMethodID(jni.granite, "getCommandLineArgument", "(Ljava/lang/String;)Ljava/lang/String;");

#ifdef HAVE_GRANITE_AUDIO
	int sample_rate = App::getAudioNativeSampleRate();
	int block_frames = App::getAudioNativeBlockFrames();
#ifdef AUDIO_HAVE_OBOE
	Granite::Audio::set_oboe_low_latency_parameters(sample_rate, block_frames);
#endif
#endif

	GameActivity_setWindowFlags(app->activity,
	                            AWINDOW_FLAG_KEEP_SCREEN_ON | AWINDOW_FLAG_TURN_SCREEN_ON |
	                            AWINDOW_FLAG_FULLSCREEN |
	                            AWINDOW_FLAG_SHOW_WHEN_LOCKED,
	                            0);
}

static void init_sensors()
{
	auto *manager = ASensorManager_getInstanceForPackage("net.themaister.GraniteActivity");
	if (!manager)
		return;

	jni.rotation_sensor = ASensorManager_getDefaultSensor(manager, SENSOR_GAME_ROTATION_VECTOR);
	if (!jni.rotation_sensor)
		return;

	LOGI("Game Sensor name: %s\n", ASensor_getName(jni.rotation_sensor));

	jni.sensor_queue = ASensorManager_createEventQueue(manager, ALooper_forThread(), LOOPER_ID_USER, nullptr, nullptr);
	if (!jni.sensor_queue)
		return;
}
}

using namespace Granite;

static void wait_for_complete_teardown(android_app *app)
{
	// If we requested to be torn down with GameActivity_finish(),
	// at least make sure we observe and pump through all takedown events,
	// or we get a deadlock.
	while (!app->destroyRequested)
	{
		android_poll_source *source = nullptr;
		int events = 0;

		if (ALooper_pollAll(-1, nullptr, &events, reinterpret_cast<void **>(&source)) >= 0)
		{
			if (source)
				source->process(app, source);
		}
	}

	assert(app->activityState == APP_CMD_STOP);
}

static bool key_event_filter(const GameActivityKeyEvent *event)
{
	switch (event->source)
	{
	case AINPUT_SOURCE_GAMEPAD:
		return true;

	case AINPUT_SOURCE_KEYBOARD:
	{
		// System level keycodes that we don't care about
		// should be handled by system.
		auto code = event->keyCode;
		bool handled = code != AKEYCODE_VOLUME_DOWN &&
		               code != AKEYCODE_VOLUME_UP;
		return handled;
	}

	default:
		return false;
	}
}

static bool motion_event_filter(const GameActivityMotionEvent *event)
{
	switch (event->source)
	{
	case AINPUT_SOURCE_TOUCHSCREEN:
	case AINPUT_SOURCE_JOYSTICK:
	case AINPUT_SOURCE_MOUSE:
		return true;

	default:
		return false;
	}
}

static void parse_config()
{
	std::string android_config;
	GRANITE_FILESYSTEM()->read_file_to_string("assets://android.json", android_config);

	if (!android_config.empty())
	{
		rapidjson::Document doc;
		doc.Parse(android_config);

		if (doc.HasMember("width"))
			global_config.target_width = doc["width"].GetUint();
		if (doc.HasMember("height"))
			global_config.target_height = doc["height"].GetUint();
		if (doc.HasMember("supportPrerotate"))
			global_config.support_prerotate = doc["supportPrerotate"].GetBool();
		if (doc.HasMember("enableGyro"))
			global_config.support_gyro = doc["enableGyro"].GetBool();
	}
}

void android_main(android_app *app)
{
	// Statics on Android might not be cleared out.
	global_state = {};
	global_config = {};
	jni = {};

	global_state.app = app;

	init_jni();

	ApplicationQueryDefaultManagerFlags flags{Global::MANAGER_FEATURE_DEFAULT_BITS};
	query_application_interface(ApplicationQuery::DefaultManagerFlags, &flags, sizeof(flags));
	Global::init(flags.manager_feature_flags);

	LOGI("Starting Granite!\n");

#ifdef ANDROID_APK_FILESYSTEM

#ifndef ANDROID_BUILTIN_ASSET_PATH
#define ANDROID_BUILTIN_ASSET_PATH ""
#endif

#ifndef ANDROID_ASSET_PATH
#define ANDROID_ASSET_PATH ""
#endif

#ifndef ANDROID_FSR2_ASSET_PATH
#define ANDROID_FSR2_ASSET_PATH ""
#endif

	AssetManagerFilesystem::global_asset_manager = app->activity->assetManager;
	GRANITE_FILESYSTEM()->register_protocol("builtin", std::make_unique<AssetManagerFilesystem>(ANDROID_BUILTIN_ASSET_PATH));
	GRANITE_FILESYSTEM()->register_protocol("assets", std::make_unique<AssetManagerFilesystem>(ANDROID_ASSET_PATH));
	GRANITE_FILESYSTEM()->register_protocol("fsr2", std::make_unique<AssetManagerFilesystem>(ANDROID_FSR2_ASSET_PATH));
	GRANITE_FILESYSTEM()->register_protocol("cache", std::make_unique<OSFilesystem>(app->activity->internalDataPath));
	GRANITE_FILESYSTEM()->register_protocol("external", std::make_unique<OSFilesystem>(app->activity->externalDataPath));
#endif

	android_app_set_key_event_filter(app, key_event_filter);
	android_app_set_motion_event_filter(app, motion_event_filter);
	app->onAppCmd = engine_handle_cmd_init;
	app->userData = nullptr;

	parse_config();

	if (global_config.support_gyro)
		init_sensors();

	GRANITE_EVENT_MANAGER()->enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Stopped);

	for (;;)
	{
		int events;
		int ident;
		android_poll_source *source;
		while ((ident = ALooper_pollAll(-1, nullptr, &events, reinterpret_cast<void **>(&source))) >= 0)
		{
			if (source)
				source->process(app, source);

			if (app->destroyRequested)
			{
				GRANITE_EVENT_MANAGER()->dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
				Global::deinit();
				deinit_jni();
				return;
			}

			if (ident == LOOPER_ID_USER)
				handle_sensors();

			if (Granite::global_state.has_window && Granite::global_state.content_rect_changed)
			{
				Granite::global_state.content_rect_changed = false;
				app->onAppCmd = Granite::engine_handle_cmd;

				try
				{
					std::vector<const char *> argv;
					argv.push_back("granite");

					std::string cli_arguments = App::getCommandLine();
					std::vector<std::string> arguments;
					LOGI("Intent arguments: %s\n", cli_arguments.c_str());
					if (!cli_arguments.empty())
					{
						arguments = Util::split_no_empty(cli_arguments, " ");
						for (auto &arg : arguments)
						{
							LOGI("Command line argument: %s\n", arg.c_str());
							argv.push_back(arg.c_str());
						}
					}
					argv.push_back(nullptr);

					auto app_handle = std::unique_ptr<Granite::Application>(
							Granite::application_create(int(argv.size()) - 1,
							                            const_cast<char **>(argv.data())));

					int ret;
					if (app_handle)
					{
						LOGI("Using resolution: %u x %u\n", global_config.target_width, global_config.target_height);
						app_handle->get_wsi().set_support_prerotate(global_config.support_prerotate);

						auto platform = std::make_unique<Granite::WSIPlatformAndroid>();
						if (platform->init(global_config.target_width, global_config.target_height))
						{
							global_state.app->userData = platform.get();
							if (!app_handle->init_platform(std::move(platform)) || !app_handle->init_wsi())
								ret = 1;
							else
							{
								while (app_handle->poll())
									app_handle->run_frame();
								ret = 0;
							}
						}
						else
							ret = 1;
					}
					else
					{
						global_state.app->userData = nullptr;
						ret = 1;
					}

					LOGI("Application returned %d.\n", ret);
					GRANITE_EVENT_MANAGER()->dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
					GameActivity_finish(global_state.app->activity);

					wait_for_complete_teardown(global_state.app);

					app_handle.reset();
					Global::deinit();
					deinit_jni();
					return;
				}
				catch (const std::exception &e)
				{
					LOGE("Application threw exception: %s\n", e.what());
					deinit_jni();
					exit(1);
				}
			}
		}
	}
}
