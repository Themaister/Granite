#define VK_USE_PLATFORM_ANDROID_KHR
#include "android_native_app_glue.h"
#include "util.hpp"
#include "application.hpp"

using namespace std;

namespace Granite
{
static android_app *global_app;
bool global_has_window;
bool global_active;

struct ApplicationPlatformAndroid : ApplicationPlatform
{
	ApplicationPlatformAndroid(unsigned width, unsigned height)
		: width(width), height(height)
	{
		if (!Vulkan::Context::init_loader(nullptr))
			throw runtime_error("Failed to init Vulkan loader.");

		has_window = global_has_window;
		active = global_active;
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

unique_ptr<ApplicationPlatform> create_default_application_platform(unsigned width, unsigned height)
{
	auto *platform = new ApplicationPlatformAndroid(width, height);
	assert(!global_app->userData);
	global_app->userData = platform;
	return unique_ptr<ApplicationPlatform>(platform);
}

static VkSurfaceKHR create_surface_from_native_window(VkInstance instance, ANativeWindow *window)
{
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	PFN_vkCreateAndroidSurfaceKHR create_surface;
	VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_SYMBOL(instance, "vkCreateAndroidSurfaceKHR", create_surface);
	VkAndroidSurfaceCreateInfoKHR create_info = { VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR };
	create_info.window = window;
	if (create_surface(instance, &create_info, nullptr, &surface) != VK_SUCCESS)
		return VK_NULL_HANDLE;
	return surface;
}

static int32_t engine_handle_input(android_app *, AInputEvent *)
{
	return 0;
}

static void engine_handle_cmd_init(android_app *pApp, int32_t cmd)
{
	auto &has_window = *static_cast<bool *>(pApp->userData);
	switch (cmd)
	{
	case APP_CMD_RESUME:
		global_active = true;
		break;

	case APP_CMD_PAUSE:
		global_active = false;
		break;

	case APP_CMD_INIT_WINDOW:
		global_has_window = pApp->window != nullptr;
		break;
	}
}

static void engine_handle_cmd(android_app *pApp, int32_t cmd)
{
	auto &state = *static_cast<ApplicationPlatformAndroid *>(pApp->userData);

	switch (cmd)
	{
	case APP_CMD_RESUME:
	{
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
		state.active = false;
		state.get_frame_timer().enter_idle();
		state.wsi_idle = true;
		break;
	}

	case APP_CMD_INIT_WINDOW:
		if (pApp->window != nullptr)
		{
			if (state.wsi)
			{
				state.has_window = true;
				auto surface = create_surface_from_native_window(state.wsi->get_context().get_instance(), pApp->window);
				state.wsi->init_surface_and_swapchain(surface);
			}
			else
				state.pending_native_window_init = true;
		}
		break;

	case APP_CMD_TERM_WINDOW:
		state.has_window = false;
		if (state.wsi)
			state.wsi->deinit_surface_and_swapchain();
		else
			state.pending_native_window_term = true;
		break;
	}
}

VkSurfaceKHR ApplicationPlatformAndroid::create_surface(VkInstance instance, VkPhysicalDevice)
{
	return create_surface_from_native_window(instance, global_app->window);
}

void ApplicationPlatformAndroid::poll_input()
{
	auto &state = *static_cast<ApplicationPlatformAndroid *>(global_app->userData);
	int events;
	android_poll_source *source;
	state.wsi = nullptr;
	while (ALooper_pollAll(1, nullptr, &events, reinterpret_cast<void **>(&source)) >= 0)
	{
		if (source)
			source->process(global_app, source);

		if (global_app->destroyRequested)
			return;
	}
}

bool ApplicationPlatformAndroid::alive(Vulkan::WSI &wsi)
{
	auto &state = *static_cast<ApplicationPlatformAndroid *>(global_app->userData);
	int events;
	android_poll_source *source;
	state.wsi = &wsi;

	if (global_app->destroyRequested)
		return false;

	bool once = false;

	if (state.pending_native_window_term)
	{
		wsi.deinit_surface_and_swapchain();
		state.pending_native_window_term = false;
	}

	if (state.pending_native_window_init)
	{
		auto surface = create_surface_from_native_window(wsi.get_context().get_instance(), global_app->window);
		wsi.init_surface_and_swapchain(surface);
		state.pending_native_window_init = false;
	}

	while (!once || !state.active || !state.has_window)
	{
		while (ALooper_pollAll((state.has_window && state.active) ? 0 : -1,
							   nullptr, &events, reinterpret_cast<void **>(&source)) >= 0)
		{
			if (source)
				source->process(global_app, source);

			if (global_app->destroyRequested)
				return false;
		}

		once = true;
	}

	return true;
}
}

void android_main(android_app *app)
{
	app_dummy();
	Granite::global_app = app;

	LOGI("Starting Granite!\n");

	app->onAppCmd = Granite::engine_handle_cmd_init;
	app->onInputEvent = Granite::engine_handle_input;
	app->userData = nullptr;
	Granite::global_active = false;
	Granite::global_has_window = false;

	for (;;)
	{
		int events;
		android_poll_source *source;
		while (ALooper_pollAll(-1, nullptr, &events, reinterpret_cast<void **>(&source)) >= 0)
		{
			if (source)
				source->process(app, source);

			if (app->destroyRequested)
				return;

			if (Granite::global_has_window)
			{
				app->onAppCmd = Granite::engine_handle_cmd;

				try
				{
					int ret = Granite::application_main(0, nullptr);
					LOGI("Application returned %d.\n", ret);
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