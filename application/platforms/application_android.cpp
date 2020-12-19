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

#include "android_native_app_glue.h"
#include "logging.hpp"
#include "application.hpp"
#include "application_events.hpp"
#include "application_wsi.hpp"
#include "context.hpp"
#include "string_helpers.hpp"
#include <jni.h>
#include <android/sensor.h>

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

using namespace std;
using namespace Vulkan;

#define SENSOR_GAME_ROTATION_VECTOR 15

namespace Granite
{
uint32_t android_api_version;

void application_dummy()
{
}

struct GlobalState
{
	android_app *app;
	int32_t base_width;
	int32_t base_height;
	int display_rotation;
	int surface_orientation;
	bool has_window;
	bool active;
	bool content_rect_changed;
};

struct JNI
{
	JNIEnv *env;
	jclass granite;
	jmethodID finishFromThread;
	jmethodID getDisplayRotation;
	jmethodID getAudioNativeSampleRate;
	jmethodID getAudioNativeBlockFrames;
	jmethodID getCommandLineArgument;
	jmethodID getCurrentOrientation;
	jclass classLoaderClass;
	jobject classLoader;

	jmethodID getVendorId;
	jmethodID getProductId;
	jmethodID getDeviceName;
	jmethodID getDevice;
	jclass inputDeviceClass;

	ASensorEventQueue *sensor_queue;
	const ASensor *rotation_sensor;
};
static GlobalState global_state;
static JNI jni;

static void on_content_rect_changed(ANativeActivity *, const ARect *rect)
{
	global_state.base_width = rect->right - rect->left;
	global_state.base_height = rect->bottom - rect->top;
	global_state.content_rect_changed = true;
	LOGI("Got content rect: %d x %d\n", global_state.base_width, global_state.base_height);
}

namespace App
{
static void finishFromThread()
{
	jni.env->CallVoidMethod(global_state.app->activity->clazz, jni.finishFromThread);
}

static string getCommandLine()
{
	string result;

	jstring key = jni.env->NewStringUTF("granite");
	jstring str = static_cast<jstring>(jni.env->CallObjectMethod(global_state.app->activity->clazz,
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
	int ret = jni.env->CallIntMethod(global_state.app->activity->clazz, jni.getAudioNativeSampleRate);
	return ret;
}

static int getAudioNativeBlockFrames()
{
	int ret = jni.env->CallIntMethod(global_state.app->activity->clazz, jni.getAudioNativeBlockFrames);
	return ret;
}
#endif

static int getCurrentOrientation()
{
	int ret = jni.env->CallIntMethod(global_state.app->activity->clazz, jni.getCurrentOrientation);
	return ret;
}

static int getDisplayRotation()
{
	int ret = jni.env->CallIntMethod(global_state.app->activity->clazz, jni.getDisplayRotation);
	return ret;
}
}

struct WSIPlatformAndroid : Granite::GraniteWSIPlatform
{
	bool init(unsigned width_, unsigned height_)
	{
		width = width_;
		height = height_;
		if (!Vulkan::Context::init_loader(nullptr))
		{
			LOGE("Failed to init Vulkan loader.\n");
			return false;
		}

		get_input_tracker().set_touch_resolution(width, height);
		has_window = global_state.has_window;
		active = global_state.active;

		for (auto &id : gamepad_ids)
			id = -1;

		return true;
	}

	void event_swapchain_created(Device *device_, unsigned width_, unsigned height_, float aspect_, size_t count_, VkFormat format_, VkSurfaceTransformFlagBitsKHR transform_) override
	{
		Granite::GraniteWSIPlatform::event_swapchain_created(device_, width_, height_, aspect_, count_, format_, transform_);

		if (transform_ & (VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR |
		                  VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR |
		                  VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_90_BIT_KHR |
		                  VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_270_BIT_KHR))
		{
			swap(width_, height_);
		}
		get_input_tracker().set_touch_resolution(width_, height_);
	}

	void update_orientation();
	bool alive(Vulkan::WSI &wsi) override;
	void poll_input() override;

	vector<const char *> get_instance_extensions() override
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

	struct GamepadInfo
	{
		string name;
		int vid = 0;
		int pid = 0;
		void init_remap_table(JoypadRemapper &remapper);
	};

	void query_gamepad_info(unsigned index, int32_t id)
	{
		auto &info = gamepad_info[index];

		jobject device = jni.env->CallStaticObjectMethod(jni.inputDeviceClass, jni.getDevice, id);
		if (device)
		{
			jstring name = static_cast<jstring>(jni.env->CallObjectMethod(device, jni.getDeviceName));
			if (name)
			{
				const char *str = jni.env->GetStringUTFChars(name, nullptr);
				if (str)
				{
					info.name = str;
					jni.env->ReleaseStringUTFChars(name, str);
				}
			}
			info.vid = jni.env->CallIntMethod(device, jni.getVendorId);
			info.pid = jni.env->CallIntMethod(device, jni.getProductId);
		}

		LOGI("Found gamepad: %s (VID: 0x%x, PID: 0x%x)\n",
		     info.name.c_str(), info.vid, info.pid);

		info.init_remap_table(get_input_tracker().get_joypad_remapper(index));
	}

	unsigned register_gamepad_id(int32_t id)
	{
		for (unsigned i = 0; i < InputTracker::Joypads; i++)
		{
			if (gamepad_ids[i] == -1)
			{
				get_input_tracker().enable_joypad(i);
				gamepad_ids[i] = id;
				query_gamepad_info(i, id);
				return i;
			}
			else if (gamepad_ids[i] == id)
				return i;
		}

		// Fallback to gamepad 0.
		return 0;
	}

	VkSurfaceKHR create_surface(VkInstance instance, VkPhysicalDevice) override;

	unsigned width, height;
	Application *app = nullptr;
	Vulkan::WSI *app_wsi = nullptr;
	bool active = false;
	bool has_window = true;
	bool wsi_idle = false;

	bool pending_native_window_init = false;
	bool pending_native_window_term = false;
	bool pending_config_change = false;
	int32_t gamepad_ids[InputTracker::Joypads];
	GamepadInfo gamepad_info[InputTracker::Joypads];
};

void WSIPlatformAndroid::GamepadInfo::init_remap_table(JoypadRemapper &remapper)
{
	remapper.reset();

	// TODO: Make this data-driven.
	if (vid == 0x54c && pid == 0x9cc)
	{
		name = "PlayStation 4 Controller - Wireless";
		LOGI("Autodetected joypad: %s\n", name.c_str());

		remapper.register_button(AKEYCODE_BUTTON_A, JoypadKey::West, JoypadAxis::Unknown);
		remapper.register_button(AKEYCODE_BUTTON_B, JoypadKey::South, JoypadAxis::Unknown);
		remapper.register_button(AKEYCODE_BUTTON_C, JoypadKey::East, JoypadAxis::Unknown);
		remapper.register_button(AKEYCODE_BUTTON_X, JoypadKey::North, JoypadAxis::Unknown);
		remapper.register_button(AKEYCODE_BUTTON_Y, JoypadKey::LeftShoulder, JoypadAxis::Unknown);
		remapper.register_button(AKEYCODE_BUTTON_START, JoypadKey::RightThumb, JoypadAxis::Unknown);
		remapper.register_button(AKEYCODE_BUTTON_Z, JoypadKey::RightShoulder, JoypadAxis::Unknown);
		remapper.register_button(AKEYCODE_BUTTON_SELECT, JoypadKey::LeftThumb, JoypadAxis::Unknown);
		remapper.register_button(AKEYCODE_BUTTON_R2, JoypadKey::Start, JoypadAxis::Unknown);
		remapper.register_button(AKEYCODE_BUTTON_L2, JoypadKey::Select, JoypadAxis::Unknown);
		remapper.register_button(AKEYCODE_BUTTON_L1, JoypadKey::Unknown, JoypadAxis::LeftTrigger);
		remapper.register_button(AKEYCODE_BUTTON_R1, JoypadKey::Unknown, JoypadAxis::RightTrigger);

		remapper.register_axis(AMOTION_EVENT_AXIS_X, JoypadAxis::LeftX, 1.0f, JoypadKey::Unknown,
		                       JoypadKey::Unknown);
		remapper.register_axis(AMOTION_EVENT_AXIS_Y, JoypadAxis::LeftY, 1.0f, JoypadKey::Unknown,
		                       JoypadKey::Unknown);
		remapper.register_axis(AMOTION_EVENT_AXIS_Z, JoypadAxis::RightX, 1.0f, JoypadKey::Unknown,
		                       JoypadKey::Unknown);
		remapper.register_axis(AMOTION_EVENT_AXIS_RZ, JoypadAxis::RightY, 1.0f, JoypadKey::Unknown,
		                       JoypadKey::Unknown);
		remapper.register_axis(AMOTION_EVENT_AXIS_HAT_X, JoypadAxis::Unknown, 1.0f, JoypadKey::Left,
		                       JoypadKey::Right);
		remapper.register_axis(AMOTION_EVENT_AXIS_HAT_Y, JoypadAxis::Unknown, 1.0f, JoypadKey::Up,
		                       JoypadKey::Down);
	}
}

static VkSurfaceKHR create_surface_from_native_window(VkInstance instance, ANativeWindow *window)
{
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	VkAndroidSurfaceCreateInfoKHR create_info = { VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR };
	create_info.window = window;
	if (vkCreateAndroidSurfaceKHR(instance, &create_info, nullptr, &surface) != VK_SUCCESS)
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
					swap(q.x, q.y);
					q.x = -q.x;
				}
				else if (global_state.display_rotation == 2)
				{
					// Doesn't seem to be possible to trigger this?
					LOGE("Untested orientation %u!\n", global_state.display_rotation);
				}
				else if (global_state.display_rotation == 3)
				{
					swap(q.x, q.y);
					q.y = -q.y;
				}

				static const quat landscape(muglm::one_over_root_two<float>(), muglm::one_over_root_two<float>(), 0.0f, 0.0f);
				q = conjugate(normalize(q * landscape));
				state.get_input_tracker().orientation_event(q);
			}
		}
	}
}

static int32_t engine_handle_input(android_app *app, AInputEvent *event)
{
	if (!app->userData)
		return 0;

	auto &state = *static_cast<WSIPlatformAndroid *>(app->userData);
	bool handled = false;

	auto type = AInputEvent_getType(event);
	auto source = AInputEvent_getSource(event);
	auto device_id = AInputEvent_getDeviceId(event);
	//auto source_class = source & AINPUT_SOURCE_CLASS_MASK;
	source &= AINPUT_SOURCE_ANY;

	switch (type)
	{
	case AINPUT_EVENT_TYPE_KEY:
	{
		if (source & AINPUT_SOURCE_GAMEPAD)
		{
			auto action = AKeyEvent_getAction(event);
			auto code = AKeyEvent_getKeyCode(event);

			bool pressed = action == AKEY_EVENT_ACTION_DOWN;
			bool released = action == AKEY_EVENT_ACTION_UP;

			if (pressed || released)
			{
				unsigned joypad_index = state.register_gamepad_id(device_id);
				auto &tracker = state.get_input_tracker();
				tracker.joypad_key_state_raw(joypad_index, code, pressed);
			}

			handled = true;
		}
		break;
	}

	case AINPUT_EVENT_TYPE_MOTION:
	{
		auto pointer = AMotionEvent_getAction(event);
		auto action = pointer & AMOTION_EVENT_ACTION_MASK;
		auto index = (pointer & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;

		if (source & AINPUT_SOURCE_JOYSTICK)
		{
			if (action == AMOTION_EVENT_ACTION_MOVE)
			{
				unsigned joypad_index = state.register_gamepad_id(device_id);
				auto &tracker = state.get_input_tracker();

				static const int axes[] = {
					AMOTION_EVENT_AXIS_X,
					AMOTION_EVENT_AXIS_Y,
					AMOTION_EVENT_AXIS_Z,
					AMOTION_EVENT_AXIS_RZ,
					AMOTION_EVENT_AXIS_HAT_X,
					AMOTION_EVENT_AXIS_HAT_Y,
					AMOTION_EVENT_AXIS_LTRIGGER,
					AMOTION_EVENT_AXIS_RTRIGGER,
					AMOTION_EVENT_AXIS_GAS,
					AMOTION_EVENT_AXIS_BRAKE,
				};

				for (int ax : axes)
					tracker.joyaxis_state_raw(joypad_index, ax, AMotionEvent_getAxisValue(event, ax, index));

				handled = true;
			}
		}
		else if (source & AINPUT_SOURCE_TOUCHSCREEN)
		{
			switch (action)
			{
			case AMOTION_EVENT_ACTION_DOWN:
			case AMOTION_EVENT_ACTION_POINTER_DOWN:
			{
				auto x = AMotionEvent_getX(event, index) / global_state.base_width;
				auto y = AMotionEvent_getY(event, index) / global_state.base_height;
				int id = AMotionEvent_getPointerId(event, index);
				state.get_input_tracker().on_touch_down(id, x, y);
				handled = true;
				break;
			}

			case AMOTION_EVENT_ACTION_MOVE:
			{
				size_t count = AMotionEvent_getPointerCount(event);
				for (size_t i = 0; i < count; i++)
				{
					auto x = AMotionEvent_getX(event, i) / global_state.base_width;
					auto y = AMotionEvent_getY(event, i) / global_state.base_height;
					int id = AMotionEvent_getPointerId(event, i);
					state.get_input_tracker().on_touch_move(id, x, y);
				}
				state.get_input_tracker().dispatch_touch_gesture();
				handled = true;
				break;
			}

			case AMOTION_EVENT_ACTION_UP:
			case AMOTION_EVENT_ACTION_POINTER_UP:
			{
				auto x = AMotionEvent_getX(event, index) / global_state.base_width;
				auto y = AMotionEvent_getY(event, index) / global_state.base_height;
				int id = AMotionEvent_getPointerId(event, index);
				state.get_input_tracker().on_touch_up(id, x, y);
				handled = true;
				break;
			}

			default:
				break;
			}
		}
		break;
	}

	default:
		break;
	}

	return handled ? 1 : 0;
}

static void engine_handle_cmd_init(android_app *app, int32_t cmd)
{
	switch (cmd)
	{
	case APP_CMD_RESUME:
	{
		LOGI("Lifecycle resume\n");
		enable_sensors();
		Granite::Global::event_manager()->dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
		Granite::Global::event_manager()->enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Running);
		global_state.active = true;
		Granite::Global::start_audio_system();
		break;
	}

	case APP_CMD_PAUSE:
	{
		LOGI("Lifecycle pause\n");
		disable_sensors();
		Granite::Global::event_manager()->dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
		Granite::Global::event_manager()->enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Paused);
		global_state.active = false;
		Granite::Global::stop_audio_system();
		break;
	}

	case APP_CMD_START:
	{
		LOGI("Lifecycle start\n");
		Granite::Global::event_manager()->dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
		Granite::Global::event_manager()->enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Paused);
		break;
	}

	case APP_CMD_STOP:
	{
		LOGI("Lifecycle stop\n");
		Granite::Global::event_manager()->dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
		Granite::Global::event_manager()->enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Stopped);
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
		}

		global_state.display_rotation = jni.env->CallIntMethod(app->activity->clazz, jni.getDisplayRotation);
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
		Granite::Global::event_manager()->dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
		Granite::Global::event_manager()->enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Running);
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
		Granite::Global::event_manager()->dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
		Granite::Global::event_manager()->enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Paused);
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
		Granite::Global::event_manager()->dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
		Granite::Global::event_manager()->enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Paused);
		break;
	}

	case APP_CMD_STOP:
	{
		LOGI("Lifecycle stop\n");
		Granite::Global::event_manager()->dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
		Granite::Global::event_manager()->enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Stopped);
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
				state.app_wsi->init_surface_and_swapchain(surface);
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
	// Apparently, AConfiguration_getOrientation is broken in latest Android versions.
	// Gotta use JNI. Sigh ........
	global_state.surface_orientation = App::getCurrentOrientation();
	global_state.display_rotation = App::getDisplayRotation();
	LOGI("Got new orientation: %d\n", global_state.surface_orientation);
	LOGI("Got new rotation: %d\n", global_state.display_rotation);
	LOGI("Got new resolution: %d x %d\n", global_state.base_width, global_state.base_height);
	pending_config_change = true;
}

void WSIPlatformAndroid::poll_input()
{
	int events;
	int ident;
	android_poll_source *source;
	app_wsi = nullptr;
	while ((ident = ALooper_pollAll(1, nullptr, &events, reinterpret_cast<void **>(&source))) >= 0)
	{
		if (source)
			source->process(global_state.app, source);

		if (ident == LOOPER_ID_USER)
			handle_sensors();

		if (global_state.app->destroyRequested)
			return;
	}
	get_input_tracker().dispatch_current_state(get_frame_timer().get_frame_time());
}

bool WSIPlatformAndroid::alive(Vulkan::WSI &wsi)
{
	auto &state = *static_cast<WSIPlatformAndroid *>(global_state.app->userData);
	int events;
	int ident;
	android_poll_source *source;
	state.app_wsi = &wsi;

	if (global_state.app->destroyRequested)
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
			wsi.init_surface_and_swapchain(surface);
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
	if (jni.env && global_state.app)
	{
		global_state.app->activity->vm->DetachCurrentThread();
		jni.env = nullptr;
	}
}

static void init_jni()
{
	auto *app = global_state.app;
	app->activity->vm->AttachCurrentThread(&jni.env, nullptr);

	jclass clazz = jni.env->GetObjectClass(app->activity->clazz);
	jmethodID getApplication = jni.env->GetMethodID(clazz, "getApplication", "()Landroid/app/Application;");
	jobject application = jni.env->CallObjectMethod(app->activity->clazz, getApplication);

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

	jni.inputDeviceClass = jni.env->FindClass("android/view/InputDevice");

	jni.finishFromThread = jni.env->GetMethodID(jni.granite, "finishFromThread", "()V");
	jni.getDisplayRotation = jni.env->GetMethodID(jni.granite, "getDisplayRotation", "()I");
	jni.getAudioNativeSampleRate = jni.env->GetMethodID(jni.granite, "getAudioNativeSampleRate", "()I");
	jni.getAudioNativeBlockFrames = jni.env->GetMethodID(jni.granite, "getAudioNativeBlockFrames", "()I");
	jni.getCurrentOrientation = jni.env->GetMethodID(jni.granite, "getCurrentOrientation", "()I");
	jni.getCommandLineArgument = jni.env->GetMethodID(jni.granite, "getCommandLineArgument", "(Ljava/lang/String;)Ljava/lang/String;");

	if (jni.inputDeviceClass)
	{
		jni.getDevice = jni.env->GetStaticMethodID(jni.inputDeviceClass, "getDevice",
		                                           "(I)Landroid/view/InputDevice;");
		jni.getDeviceName = jni.env->GetMethodID(jni.inputDeviceClass, "getName",
		                                         "()Ljava/lang/String;");
		jni.getVendorId = jni.env->GetMethodID(jni.inputDeviceClass, "getVendorId",
		                                       "()I");
		jni.getProductId = jni.env->GetMethodID(jni.inputDeviceClass, "getProductId",
		                                        "()I");
	}

#ifdef HAVE_GRANITE_AUDIO
	int sample_rate = App::getAudioNativeSampleRate();
	int block_frames = App::getAudioNativeBlockFrames();
#ifdef AUDIO_HAVE_OPENSL
	Granite::Audio::set_opensl_low_latency_parameters(sample_rate, block_frames);
#endif
#ifdef AUDIO_HAVE_OBOE
	Granite::Audio::set_oboe_low_latency_parameters(sample_rate, block_frames);
#endif
#endif
}

static void init_sensors()
{
#if __ANDROID_API__ >= 26
	auto *manager = ASensorManager_getInstanceForPackage("net.themaister.GraniteActivity");
#else
	auto *manager = ASensorManager_getInstance();
#endif
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
	// If we requested to be torn down with App::finishFromThread(),
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

void android_main(android_app *app)
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated"
	app_dummy();
#pragma GCC diagnostic pop

	// Native glue does not implement this.
	app->activity->callbacks->onContentRectChanged = on_content_rect_changed;

	android_api_version = uint32_t(app->activity->sdkVersion);

	// Statics on Android might not be cleared out.
	global_state = {};
	jni = {};

	global_state.app = app;

	init_jni();
	Global::init();

	LOGI("Starting Granite!\n");

#ifdef ANDROID_APK_FILESYSTEM

#ifndef ANDROID_BUILTIN_ASSET_PATH
#define ANDROID_BUILTIN_ASSET_PATH ""
#endif

#ifndef ANDROID_ASSET_PATH
#define ANDROID_ASSET_PATH ""
#endif

	AssetManagerFilesystem::global_asset_manager = app->activity->assetManager;
	Global::filesystem()->register_protocol("builtin", make_unique<AssetManagerFilesystem>(ANDROID_BUILTIN_ASSET_PATH));
	Global::filesystem()->register_protocol("assets", make_unique<AssetManagerFilesystem>(ANDROID_ASSET_PATH));
	Global::filesystem()->register_protocol("cache", make_unique<OSFilesystem>(app->activity->internalDataPath));
#endif

	app->onAppCmd = engine_handle_cmd_init;
	app->onInputEvent = engine_handle_input;
	app->userData = nullptr;

	init_sensors();

	Granite::Global::event_manager()->enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Stopped);

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
				Granite::Global::event_manager()->dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
				Global::deinit();
				deinit_jni();
				return;
			}

			if (ident == LOOPER_ID_USER)
				handle_sensors();

			if (Granite::global_state.has_window && Granite::global_state.content_rect_changed)
			{
				Granite::global_state.content_rect_changed = false;
				global_state.surface_orientation = AConfiguration_getOrientation(app->config);
				LOGI("Get initial orientation: %d\n", global_state.surface_orientation);
				app->onAppCmd = Granite::engine_handle_cmd;

				try
				{
					vector<const char *> argv;
					argv.push_back("granite");

					string cli_arguments = App::getCommandLine();
					vector<string> arguments;
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

					auto app_handle = unique_ptr<Granite::Application>(
							Granite::application_create(int(argv.size()) - 1,
							                            const_cast<char **>(argv.data())));

					int ret;
					if (app_handle)
					{
						// TODO: Configurable.
						app_handle->get_wsi().set_support_prerotate(true);

						unsigned width = 0;
						unsigned height = 0;
						{
							string android_config;
							Global::filesystem()->read_file_to_string("assets://android.json", android_config);
							if (!android_config.empty())
							{
								rapidjson::Document doc;
								doc.Parse(android_config);

								if (doc.HasMember("width"))
									width = doc["width"].GetUint();
								if (doc.HasMember("height"))
									height = doc["height"].GetUint();
							}
						}
						LOGI("Using resolution: %u x %u\n", width, height);

						auto platform = make_unique<Granite::WSIPlatformAndroid>();
						if (platform->init(width, height))
						{
							global_state.app->userData = platform.get();
							if (!app_handle->init_wsi(move(platform)))
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
					Granite::Global::event_manager()->dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
					App::finishFromThread();

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
