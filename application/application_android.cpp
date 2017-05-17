#include "android_native_app_glue.h"
#include "util.hpp"
#include "application.hpp"

struct AppState
{
	Vulkan::WSI *wsi = nullptr;
	bool active = false;
	bool has_window = false;
};

static int32_t engine_handle_input(android_app *, AInputEvent *)
{
	return 0;
}

static void engine_handle_cmd(android_app *pApp, int32_t cmd)
{
	auto &state = *static_cast<AppState *>(pApp->userData);

	switch (cmd)
	{
	case APP_CMD_RESUME:
	{
		state.active = true;
		LOGI("APP_CMD_RESUME\n");
		break;
	}

	case APP_CMD_PAUSE:
	{
		state.active = false;
		LOGI("APP_CMD_PAUSE\n");
		break;
	}

	case APP_CMD_INIT_WINDOW:
		if (pApp->window != nullptr)
		{
			LOGI("APP_INIT_WINDOW\n");
			Vulkan::WSI::set_global_native_window(pApp->window);
			state.has_window = true;

			if (state.wsi)
				state.wsi->runtime_init_native_window(pApp->window);
		}
		break;

	case APP_CMD_TERM_WINDOW:
		LOGE("Terminating window.\n");
		state.has_window = false;

		if (state.wsi)
			state.wsi->runtime_term_native_window();
		break;
	}
}

static android_app *global_app;
static AppState global_state;

bool mainloop_step(Vulkan::WSI &wsi)
{
	int events;
	android_poll_source *source;
	global_state.wsi = &wsi;

	while (!global_state.active || !global_state.has_window)
	{
		bool running = global_state.has_window && global_state.active;
		while (ALooper_pollAll(running ? 1 : -1, nullptr, &events, reinterpret_cast<void **>(&source)) >= 0)
		{
			if (source)
				source->process(global_app, source);

			if (global_app->destroyRequested)
				return false;
		}
	}

	return true;
}

void android_main(android_app *app)
{
	app_dummy();
	global_app = app;

	LOGI("Starting android app!\n");

	app->userData = &global_state;
	app->onAppCmd = engine_handle_cmd;
	app->onInputEvent = engine_handle_input;

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

			if (global_state.has_window)
			{
				try
				{
					int ret = Granite::application_main(0, nullptr);
					LOGI("Application returned %d.\n", ret);
				}
				catch (const std::exception &e)
				{
					LOGE("Application threw exception: %s\n", e.what());
					exit(1);
				}
				return;
			}
		}
	}
}