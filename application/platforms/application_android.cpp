/* Copyright (c) 2017-2018 Hans-Kristian Arntzen
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
#include "util.hpp"
#include "application.hpp"
#include "application_events.hpp"
#include "vulkan.hpp"
#include <jni.h>
#include <android/sensor.h>

#include "android.hpp"
#include "os.hpp"
#include "rapidjson_wrapper.hpp"

using namespace std;
using namespace Vulkan;

#define SENSOR_GAME_ROTATION_VECTOR 15

namespace Granite
{
void application_dummy()
{
}

struct GlobalState
{
	android_app *app;
	int32_t base_width;
	int32_t base_height;
	int orientation;
	bool has_window;
	bool active;
};

struct JNI
{
	jclass granite;
	jmethodID finishFromThread;
	jmethodID getDisplayRotation;
	jclass classLoaderClass;
	jobject classLoader;

	ASensorEventQueue *sensor_queue;
	const ASensor *rotation_sensor;
};
static GlobalState global_state;
static JNI jni;

namespace App
{
static void finishFromThread()
{
	JNIEnv *env = nullptr;
	global_state.app->activity->vm->AttachCurrentThread(&env, nullptr);
	env->CallVoidMethod(global_state.app->activity->clazz, jni.finishFromThread);
	global_state.app->activity->vm->DetachCurrentThread();
}
}

struct WSIPlatformAndroid : Vulkan::WSIPlatform
{
	WSIPlatformAndroid(unsigned width, unsigned height)
		: width(width), height(height)
	{
		if (!Vulkan::Context::init_loader(nullptr))
			throw runtime_error("Failed to init Vulkan loader.");

		has_window = global_state.has_window;
		active = global_state.active;
	}

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

	VkSurfaceKHR create_surface(VkInstance instance, VkPhysicalDevice) override;

	unsigned width, height;
	Application *app = nullptr;
	Vulkan::WSI *wsi = nullptr;
	bool active = false;
	bool has_window = true;
	bool wsi_idle = false;

	bool pending_native_window_init = false;
	bool pending_native_window_term = false;
};

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
				if (global_state.orientation == 1)
				{
					// Compensate for different display rotation.
					swap(q.x, q.y);
					q.x = -q.x;
				}
				else if (global_state.orientation == 2 || global_state.orientation == 3)
				{
					LOGE("Untested orientation %u!\n", global_state.orientation);
				}

				static const quat landscape(glm::one_over_root_two<float>(), glm::one_over_root_two<float>(), 0.0f, 0.0f);
				q = conjugate(normalize(q * landscape));
				state.get_input_tracker().orientation_event(q);
			}
		}
	}
}

static int32_t engine_handle_input(android_app *app, AInputEvent *event)
{
	auto &state = *static_cast<WSIPlatformAndroid *>(app->userData);
	bool handled = false;

	auto type = AInputEvent_getType(event);
	switch (type)
	{
	case AINPUT_EVENT_TYPE_MOTION:
	{
		auto pointer = AMotionEvent_getAction(event);
		auto action = pointer & AMOTION_EVENT_ACTION_MASK;

		switch (action)
		{
		case AMOTION_EVENT_ACTION_DOWN:
		case AMOTION_EVENT_ACTION_POINTER_DOWN:
		{
			auto index = (pointer & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
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
			for (size_t index = 0; index < count; index++)
			{
				auto x = AMotionEvent_getX(event, index) / global_state.base_width;
				auto y = AMotionEvent_getY(event, index) / global_state.base_height;
				int id = AMotionEvent_getPointerId(event, index);
				state.get_input_tracker().on_touch_move(id, x, y);
			}
			state.get_input_tracker().dispatch_touch_gesture();
			handled = true;
			break;
		}

		case AMOTION_EVENT_ACTION_UP:
		case AMOTION_EVENT_ACTION_POINTER_UP:
		{
			auto index = (pointer & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
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
		Granite::EventManager::get_global().dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
		Granite::EventManager::get_global().enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Running);
		global_state.active = true;
		break;
	}

	case APP_CMD_PAUSE:
	{
		LOGI("Lifecycle pause\n");
		disable_sensors();
		Granite::EventManager::get_global().dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
		Granite::EventManager::get_global().enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Paused);
		global_state.active = false;
		break;
	}

	case APP_CMD_START:
	{
		LOGI("Lifecycle start\n");
		Granite::EventManager::get_global().dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
		Granite::EventManager::get_global().enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Paused);
		break;
	}

	case APP_CMD_STOP:
	{
		LOGI("Lifecycle stop\n");
		Granite::EventManager::get_global().dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
		Granite::EventManager::get_global().enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Stopped);
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

		JNIEnv *env = nullptr;
		app->activity->vm->AttachCurrentThread(&env, nullptr);
		global_state.orientation = env->CallIntMethod(app->activity->clazz, jni.getDisplayRotation);
		app->activity->vm->DetachCurrentThread();
		break;
	}
	}
}

static void engine_handle_cmd(android_app *app, int32_t cmd)
{
	auto &state = *static_cast<WSIPlatformAndroid *>(app->userData);

	switch (cmd)
	{
	case APP_CMD_RESUME:
	{
		LOGI("Lifecycle resume\n");
		Granite::EventManager::get_global().dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
		Granite::EventManager::get_global().enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Running);
		enable_sensors();

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
		Granite::EventManager::get_global().dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
		Granite::EventManager::get_global().enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Paused);
		disable_sensors();

		state.active = false;
		state.get_frame_timer().enter_idle();
		state.wsi_idle = true;
		break;
	}

	case APP_CMD_START:
	{
		LOGI("Lifecycle start\n");
		Granite::EventManager::get_global().dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
		Granite::EventManager::get_global().enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Paused);
		break;
	}

	case APP_CMD_STOP:
	{
		LOGI("Lifecycle stop\n");
		Granite::EventManager::get_global().dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
		Granite::EventManager::get_global().enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Stopped);
		break;
	}

	case APP_CMD_INIT_WINDOW:
		if (app->window != nullptr)
		{
			state.has_window = true;
			global_state.base_width = ANativeWindow_getWidth(app->window);
			global_state.base_height = ANativeWindow_getHeight(app->window);
			LOGI("New window size %d x %d\n", global_state.base_width, global_state.base_height);

			if (state.wsi)
			{
				LOGI("Lifecycle init window\n");
				auto surface = create_surface_from_native_window(state.wsi->get_context().get_instance(), app->window);
				state.wsi->init_surface_and_swapchain(surface);
			}
			else
				state.pending_native_window_init = true;
		}
		break;

	case APP_CMD_TERM_WINDOW:
		state.has_window = false;
		if (state.wsi)
		{
			LOGI("Lifecycle deinit window\n");
			state.wsi->deinit_surface_and_swapchain();
		}
		else
			state.pending_native_window_term = true;
		break;
	}
}

VkSurfaceKHR WSIPlatformAndroid::create_surface(VkInstance instance, VkPhysicalDevice)
{
	return create_surface_from_native_window(instance, global_state.app->window);
}

void WSIPlatformAndroid::poll_input()
{
	auto &state = *static_cast<WSIPlatformAndroid *>(global_state.app->userData);
	int events;
	int ident;
	android_poll_source *source;
	state.wsi = nullptr;
	while ((ident = ALooper_pollAll(1, nullptr, &events, reinterpret_cast<void **>(&source))) >= 0)
	{
		if (source)
			source->process(global_state.app, source);

		if (ident == LOOPER_ID_USER)
			handle_sensors();

		if (global_state.app->destroyRequested)
			return;
	}

	state.get_input_tracker().dispatch_current_state(state.get_frame_timer().get_frame_time());
}

bool WSIPlatformAndroid::alive(Vulkan::WSI &wsi)
{
	auto &state = *static_cast<WSIPlatformAndroid *>(global_state.app->userData);
	int events;
	int ident;
	android_poll_source *source;
	state.wsi = &wsi;

	if (state.killed || global_state.app->destroyRequested)
		return false;

	bool once = false;

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

static void init_jni()
{
	JNIEnv *env = nullptr;
	auto *app = global_state.app;
	app->activity->vm->AttachCurrentThread(&env, nullptr);

	jclass clazz = env->GetObjectClass(app->activity->clazz);
	jmethodID getApplication = env->GetMethodID(clazz, "getApplication", "()Landroid/app/Application;");
	jobject application = env->CallObjectMethod(app->activity->clazz, getApplication);

	jclass applicationClass = env->GetObjectClass(application);
	jmethodID getApplicationContext = env->GetMethodID(applicationClass, "getApplicationContext", "()Landroid/content/Context;");
	jobject context = env->CallObjectMethod(application, getApplicationContext);

	jclass contextClass = env->GetObjectClass(context);
	jmethodID getClassLoader = env->GetMethodID(contextClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
	jni.classLoader = env->CallObjectMethod(context, getClassLoader);

	jni.classLoaderClass = env->GetObjectClass(jni.classLoader);
	jmethodID loadClass = env->GetMethodID(jni.classLoaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
	jstring str = env->NewStringUTF("net.themaister.granite.GraniteActivity");
	jni.granite = static_cast<jclass>(env->CallObjectMethod(jni.classLoader, loadClass, str));
	jni.finishFromThread = env->GetMethodID(jni.granite, "finishFromThread", "()V");
	jni.getDisplayRotation = env->GetMethodID(jni.granite, "getDisplayRotation", "()I");

	env->DeleteLocalRef(str);
	app->activity->vm->DetachCurrentThread();
}

static void init_sensors()
{
	auto *manager = ASensorManager_getInstance();
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

void android_main(android_app *app)
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated"
	app_dummy();
#pragma GCC diagnostic pop

	// Statics on Android might not be cleared out.
	global_state = {};
	jni = {};

	global_state.app = app;
	init_jni();

	LOGI("Starting Granite!\n");

#ifdef ANDROID_APK_FILESYSTEM

#ifndef ANDROID_BUILTIN_ASSET_PATH
#define ANDROID_BUILTIN_ASSET_PATH ""
#endif

#ifndef ANDROID_ASSET_PATH
#define ANDROID_ASSET_PATH ""
#endif

	Filesystem::get().register_protocol("builtin", make_unique<AssetManagerFilesystem>(ANDROID_BUILTIN_ASSET_PATH, app->activity->assetManager));
	Filesystem::get().register_protocol("assets", make_unique<AssetManagerFilesystem>(ANDROID_ASSET_PATH, app->activity->assetManager));
	Filesystem::get().register_protocol("cache", make_unique<OSFilesystem>(app->activity->internalDataPath));
#endif

	app->onAppCmd = engine_handle_cmd_init;
	app->onInputEvent = engine_handle_input;
	app->userData = nullptr;

	init_sensors();

	Granite::EventManager::get_global().enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Stopped);

	unsigned width = 1280;
	unsigned height = 720;
	string android_config;
	Filesystem::get().read_file_to_string("assets://android.json", android_config);
	if (!android_config.empty())
	{
		rapidjson::Document doc;
		doc.Parse(android_config);

		if (doc.HasMember("width"))
			width = doc["width"].GetUint();
		if (doc.HasMember("height"))
			height = doc["height"].GetUint();
	}

	LOGI("Using resolution: %u x %u\n", width, height);

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
				Granite::EventManager::get_global().dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
				return;
			}

			if (ident == LOOPER_ID_USER)
				handle_sensors();

			if (Granite::global_state.has_window)
			{
				app->onAppCmd = Granite::engine_handle_cmd;

				try
				{
					int ret;
					int argc = 1;
					char arg[] = "granite";
					char *argv[] = { arg, nullptr };

					auto app = unique_ptr<Granite::Application>(Granite::application_create(argc, argv));
					if (app)
					{
						auto platform = make_unique<Granite::WSIPlatformAndroid>(width, height);
						global_state.app->userData = platform.get();
						if (!app->init_wsi(move(platform)))
							ret = 1;
						else
						{
							while (app->poll())
								app->run_frame();
							ret = 0;
						}
					}
					else
						ret = 1;

					LOGI("Application returned %d.\n", ret);
					Granite::EventManager::get_global().dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
					App::finishFromThread();
					return;
				}
				catch (const std::exception &e)
				{
					LOGE("Application threw exception: %s\n", e.what());
					exit(1);
				}
			}
		}
	}
}
