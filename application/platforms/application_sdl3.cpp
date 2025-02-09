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

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <atomic>

#include "application.hpp"
#include "application_wsi.hpp"
#include "application_events.hpp"
#include "input.hpp"
#include "input_sdl.hpp"
#include "cli_parser.hpp"
#include "global_managers_init.hpp"
#include "timeline_trace_file.hpp"
#include "path_utils.hpp"
#include "thread_group.hpp"
#include "thread_id.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#ifdef __linux__
#include <dlfcn.h>
#endif

namespace Granite
{
static Key sdl_key_to_granite_key(SDL_Keycode key)
{
	if (key >= 'a' && key <= 'z')
		return Key(int(Granite::Key::A) + (key - 'a'));
	else if (key >= 'A' && key <= 'Z')
		return Key(int(Granite::Key::A) + (key - 'A'));

#define k(sdl, granite) case SDLK_##sdl: return Key::granite
	switch (key)
	{
	k(LCTRL, LeftCtrl);
	k(LALT, LeftAlt);
	k(LSHIFT, LeftShift);
	k(RETURN, Return);
	k(SPACE, Space);
	k(ESCAPE, Escape);
	k(LEFT, Left);
	k(RIGHT, Right);
	k(UP, Up);
	k(DOWN, Down);
	k(0, _0);
	k(1, _1);
	k(2, _2);
	k(3, _3);
	k(4, _4);
	k(5, _5);
	k(6, _6);
	k(7, _7);
	k(8, _8);
	k(9, _9);
	default:
		return Key::Unknown;
	}
#undef k
}

struct WSIPlatformSDL : GraniteWSIPlatform
{
public:
	struct Options
	{
		unsigned override_width = 0;
		unsigned override_height = 0;
		bool fullscreen = false;
#ifdef _WIN32
		bool threaded = true;
#else
		bool threaded = false;
#endif
	};

	explicit WSIPlatformSDL(const Options &options_)
		: options(options_)
	{
	}

	void run_gamepad_init()
	{
		Util::Timer tmp_timer;
		tmp_timer.start();

		if (!SDL_Init(SDL_INIT_GAMEPAD))
		{
			LOGE("Failed to init gamepad.\n");
			return;
		}

		LOGI("SDL_Init(GAMEPAD) took %.3f seconds async.\n", tmp_timer.end());

		LOGI("Pushing task to main thread.\n");
		push_task_to_main_thread([this]() {
			LOGI("Running task in main thread.\n");
			if (!pad.init(get_input_tracker(), [](std::function<void ()> func) { func(); }))
				LOGE("Failed to init gamepad tracker.\n");

			gamepad_init_async.store(true, std::memory_order_release);
		});
	}

	void kick_gamepad_init()
	{
		SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
		// Adding gamepad events will make main loop spin without waiting.
		SDL_SetHint(SDL_HINT_AUTO_UPDATE_JOYSTICKS, "0");

		// Enumerating gamepads can be extremely slow in some cases. Do this async.
		// Gamepad interface is very async friendly.

		gamepad_init_async = false;

		if (auto *tg = GRANITE_THREAD_GROUP())
		{
			gamepad_init_task = tg->create_task([this]() { run_gamepad_init(); });
			gamepad_init_task->set_desc("SDL init gamepad");
			gamepad_init_task->set_task_class(TaskClass::Background);
			gamepad_init_task->flush();
		}
		else
			run_gamepad_init();
	}

	bool init(const std::string &name, unsigned width_, unsigned height_)
	{
		request_tear_down.store(false);
		width = width_;
		height = height_;

		if (options.override_width)
			width = options.override_width;
		if (options.override_height)
			height = options.override_height;

#ifdef __linux__
		// RenderDoc doesn't support Wayland, and SDL3 uses Wayland by default.
		// Opt in to X11 to avoid having to manually remember to pass down SDL_VIDEO_DRIVER=x11.
		void *renderdoc_module = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD);
		if (renderdoc_module)
		{
			LOGI("RenderDoc is loaded, disabling Wayland.\n");
			setenv("SDL_VIDEO_DRIVER", "x11", 0);
		}
#endif

		Util::Timer tmp_timer;
		tmp_timer.start();
		if (!SDL_Init(SDL_INIT_EVENTS | SDL_INIT_VIDEO))
		{
			LOGE("Failed to init SDL.\n");
			return false;
		}
		LOGI("SDL_Init took %.3f seconds.\n", tmp_timer.end());

		kick_gamepad_init();

		SDL_SetEventEnabled(SDL_EVENT_DROP_FILE, false);
		SDL_SetEventEnabled(SDL_EVENT_DROP_TEXT, false);

		if (!SDL_Vulkan_LoadLibrary(nullptr))
		{
			LOGE("Failed to load Vulkan library.\n");
			return false;
		}

		if (!Vulkan::Context::init_loader(
				reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr())))
		{
			LOGE("Failed to initialize Vulkan loader.\n");
			return false;
		}

		wake_event_type = SDL_RegisterEvents(1);

		application.name = name;
		if (application.name.empty())
			application.name = Path::basename(Path::get_executable_path());

		window = SDL_CreateWindow(application.name.empty() ? "SDL Window" : application.name.c_str(),
		                          int(width), int(height), SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN);
		if (!window)
		{
			LOGE("Failed to create SDL window.\n");
			return false;
		}

		if (options.fullscreen)
			toggle_fullscreen();

		application.info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		application.info.pEngineName = "Granite";
		application.info.pApplicationName = application.name.empty() ? "Granite" : application.name.c_str();
		application.info.apiVersion = VK_API_VERSION_1_1;

		return true;
	}

	const VkApplicationInfo *get_application_info() override
	{
		return &application.info;
	}

	void begin_drop_event() override
	{
		push_task_to_main_thread([]() {
			SDL_SetEventEnabled(SDL_EVENT_DROP_FILE, true);
		});
	}

	void show_message_box(const std::string &str, MessageType type) override
	{
		push_task_to_main_thread([this, str, type]() {
			const char *title = nullptr;
			Uint32 flags = 0;
			switch (type)
			{
			case MessageType::Error:
				flags = SDL_MESSAGEBOX_ERROR;
				title = "Error";
				break;

			case MessageType::Warning:
				flags = SDL_MESSAGEBOX_WARNING;
				title = "Warning";
				break;

			case MessageType::Info:
				flags = SDL_MESSAGEBOX_INFORMATION;
				title = "Info";
				break;
			}

			SDL_ShowSimpleMessageBox(flags, title, str.c_str(), window);
		});
	}

	uintptr_t get_native_window() override
	{
#ifdef _WIN32
		SDL_PropertiesID props = SDL_GetWindowProperties(window);
		SDL_LockProperties(props);
		auto hwnd = static_cast<HWND>(SDL_GetPointerProperty(props, "SDL.window.win32.hwnd", nullptr));
		SDL_UnlockProperties(props);
		return reinterpret_cast<uintptr_t>(hwnd);
#else
		return 0;
#endif
	}

	void toggle_fullscreen()
	{
		bool is_fullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;

		if (!is_fullscreen)
		{
			if (!SDL_SetWindowFullscreen(window, true))
			{
				LOGE("Failed to toggle fullscreen.\n");
			}
#ifdef _WIN32
			else
			{
				SDL_PropertiesID props = SDL_GetWindowProperties(window);
				SDL_LockProperties(props);
				auto hwnd = static_cast<HWND>(SDL_GetPointerProperty(props, "SDL.window.win32.hwnd", nullptr));
				SDL_UnlockProperties(props);

				push_task_to_async_thread([this, hwnd]() {
					set_hmonitor(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY));
				});
			}
#endif
		}
		else
		{
#ifdef _WIN32
			push_task_to_async_thread([this]() {
				set_hmonitor(nullptr);
			});
#endif
			SDL_SetWindowFullscreen(window, false);
		}
	}

	bool alive(Vulkan::WSI &) override
	{
		std::lock_guard<std::mutex> holder{get_input_tracker().get_lock()};
		flush_deferred_input_events();
		process_events_async_thread();
		process_events_async_thread_non_pollable();
		return !request_tear_down.load();
	}

	void poll_input() override
	{
		if (!options.threaded && !iterate_message_loop())
			request_tear_down = true;

		std::lock_guard<std::mutex> holder{get_input_tracker().get_lock()};
		flush_deferred_input_events();
		process_events_async_thread();

		if (gamepad_init_async.load(std::memory_order_acquire))
			pad.update(get_input_tracker());
		get_input_tracker().dispatch_current_state(get_frame_timer().get_frame_time());
	}

	void poll_input_async(Granite::InputTrackerHandler *override_handler) override
	{
		std::lock_guard<std::mutex> holder{get_input_tracker().get_lock()};
		begin_async_input_handling();
		{
			process_events_async_thread();
			if (gamepad_init_async.load(std::memory_order_acquire))
				pad.update(get_input_tracker());
		}
		end_async_input_handling();
		get_input_tracker().dispatch_current_state(0.0, override_handler);
	}

	std::vector<const char *> get_instance_extensions() override
	{
		uint32_t count;
		const char * const *ext = SDL_Vulkan_GetInstanceExtensions(&count);
		return { ext, ext + count };
	}

	VkSurfaceKHR create_surface(VkInstance instance, VkPhysicalDevice) override
	{
		VkSurfaceKHR surface = VK_NULL_HANDLE;
		if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface))
			return VK_NULL_HANDLE;

		int actual_width, actual_height;
		SDL_GetWindowSizeInPixels(window, &actual_width, &actual_height);
		width = unsigned(actual_width);
		height = unsigned(actual_height);
		return surface;
	}

	uint32_t get_surface_width() override
	{
		return width;
	}

	uint32_t get_surface_height() override
	{
		return height;
	}

	~WSIPlatformSDL()
	{
		if (gamepad_init_task)
			gamepad_init_task->wait();

		if (window)
			SDL_DestroyWindow(window);

		pad.close();
		SDL_Quit();
	}

	void block_until_wsi_forward_progress(Vulkan::WSI &wsi) override
	{
		if (options.threaded)
		{
			get_frame_timer().enter_idle();
			while (!resize && alive(wsi))
				process_events_async_thread_blocking();
			get_frame_timer().leave_idle();
		}
		else
		{
			WSIPlatform::block_until_wsi_forward_progress(wsi);
		}
	}

	void notify_resize(unsigned width_, unsigned height_)
	{
		LOGI("Resize: %u x %u\n", width_, height_);
		push_task_to_async_thread([=]() {
			resize = true;
			width = width_;
			height = height_;
		});
	}

	void notify_current_swapchain_dimensions(unsigned width_, unsigned height_) override
	{
		push_task_to_main_thread([=]() {
			WSIPlatform::notify_current_swapchain_dimensions(width_, height_);
		});
	}

	void set_window_title(const std::string &title) override
	{
		push_task_to_main_thread([=]() {
			if (window)
				SDL_SetWindowTitle(window, title.c_str());
		});
	}

	bool process_sdl_event(const SDL_Event &e)
	{
		if (e.type == wake_event_type)
		{
			LOGI("Processing events main thread.\n");
			process_events_main_thread();
			return true;
		}

		const auto dispatcher = [this](std::function<void ()> func) {
			push_task_to_async_thread(std::move(func));
		};

		if (pad.process_sdl_event(e, get_input_tracker(), dispatcher))
			return true;

		switch (e.type)
		{
		case SDL_EVENT_QUIT:
			return false;

		case SDL_EVENT_WINDOW_RESIZED:
			if (e.window.windowID == SDL_GetWindowID(window))
				notify_resize(e.window.data1, e.window.data2);
			break;

		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_BUTTON_UP:
			if (e.button.windowID == SDL_GetWindowID(window))
			{
				MouseButton btn;
				if (e.button.button == SDL_BUTTON_LEFT)
					btn = MouseButton::Left;
				else if (e.button.button == SDL_BUTTON_MIDDLE)
					btn = MouseButton::Middle;
				else if (e.button.button == SDL_BUTTON_RIGHT)
					btn = MouseButton::Right;
				else
					break;

				push_task_to_async_thread(
						[this, btn, x = e.button.x, y = e.button.y,
								pressed = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN]() {
							get_input_tracker().mouse_button_event(btn, x, y, pressed);
						});
			}
			break;

		case SDL_EVENT_WINDOW_MOUSE_ENTER:
			if (e.window.windowID == SDL_GetWindowID(window))
			{
				float x, y;
				SDL_GetMouseState(&x, &y);
				push_task_to_async_thread([this, x, y]() {
					get_input_tracker().mouse_enter(x, y);
				});
			}
			break;

		case SDL_EVENT_WINDOW_MOUSE_LEAVE:
			if (e.window.windowID == SDL_GetWindowID(window))
			{
				push_task_to_async_thread([this]() {
					get_input_tracker().mouse_leave();
				});
			}
			break;

		case SDL_EVENT_MOUSE_MOTION:
			if (e.motion.windowID == SDL_GetWindowID(window))
			{
				push_task_to_async_thread([this, x = e.motion.x, y = e.motion.y]() {
					get_input_tracker().mouse_move_event_absolute(x, y);
				});
			}
			break;

		case SDL_EVENT_KEY_DOWN:
		case SDL_EVENT_KEY_UP:
			if (e.key.windowID == SDL_GetWindowID(window))
			{
				KeyState state;
				if (e.key.repeat)
					state = KeyState::Repeat;
				else if (e.type == SDL_EVENT_KEY_DOWN)
					state = KeyState::Pressed;
				else
					state = KeyState::Released;

				if (state == KeyState::Pressed && e.key.key == SDLK_ESCAPE)
				{
					return false;
				}
				else if (state == KeyState::Pressed && e.key.key == SDLK_RETURN &&
				         (e.key.mod & SDL_KMOD_ALT) != 0)
				{
					toggle_fullscreen();
				}
				else if (state == KeyState::Pressed && tolower(e.key.key) == 'v' &&
				         (e.key.mod & SDL_KMOD_LCTRL) != 0)
				{
					push_non_pollable_task_to_async_thread([c = clipboard]() mutable {
						if (auto *manager = GRANITE_EVENT_MANAGER())
							manager->enqueue<Vulkan::ApplicationWindowTextDropEvent>(std::move(c));
					});
				}
				else
				{
					Key key = sdl_key_to_granite_key(e.key.key);
					push_task_to_async_thread([=]() {
						get_input_tracker().key_event(key, state);
					});
				}
			}
			break;

		case SDL_EVENT_DROP_FILE:
			if (e.drop.windowID == SDL_GetWindowID(window))
			{
				std::string str = e.drop.data;
				push_non_pollable_task_to_async_thread([s = std::move(str)]() mutable {
					if (auto *manager = GRANITE_EVENT_MANAGER())
						manager->enqueue<Vulkan::ApplicationWindowFileDropEvent>(std::move(s));
				});
			}
			break;

		case SDL_EVENT_DROP_COMPLETE:
			SDL_SetEventEnabled(SDL_EVENT_DROP_FILE, false);
			break;

		case SDL_EVENT_CLIPBOARD_UPDATE:
			if (SDL_HasClipboardText())
			{
				const char *text = SDL_GetClipboardText();
				if (text)
					clipboard = text;
				else
					clipboard.clear();
			}
			else
				clipboard.clear();
			break;

		default:
			break;
		}

		return true;
	}

	void run_message_loop()
	{
		SDL_Event e;
		while (async_loop_alive && SDL_WaitEvent(&e))
			if (!process_sdl_event(e))
				break;
	}

	bool iterate_message_loop()
	{
		SDL_Event e;
		while (SDL_PollEvent(&e))
			if (!process_sdl_event(e))
				return false;
		return true;
	}

	void run_loop(Application *app)
	{
		auto ctx = Global::create_thread_context();

		process_events_main_thread();

		if (options.threaded)
		{
			async_loop_alive = true;
			threaded_main_loop = std::thread(&WSIPlatformSDL::thread_main, this, app, std::move(ctx));

			run_message_loop();
			notify_close();

			if (threaded_main_loop.joinable())
				threaded_main_loop.join();
		}
		else
			thread_main(app, {});
	}

	static void dispatch_running_events()
	{
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
	}

	static void dispatch_stopped_events()
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

	void thread_main(Application *app, Global::GlobalManagersHandle ctx)
	{
		if (options.threaded)
		{
			// Set this up as an alternative main thread.
			ThreadGroup::set_async_main_thread();
			Global::set_thread_context(*ctx);
			Util::register_thread_index(0);
			ctx.reset();
		}

		{
			GRANITE_SCOPED_TIMELINE_EVENT("sdl-dispatch-running-events");
			dispatch_running_events();
		}

		{
			{
				GRANITE_SCOPED_TIMELINE_EVENT("sdl-start-audio-system");
				Granite::Global::start_audio_system();
			}

			while (app->poll())
				app->run_frame();
			Granite::Global::stop_audio_system();
		}
		dispatch_stopped_events();
		push_task_to_main_thread([this]() { async_loop_alive = false; });
	}

	void notify_close()
	{
		request_tear_down.store(true);
	}

#ifdef _WIN32
	void set_hmonitor(HMONITOR monitor)
	{
		current_hmonitor = monitor;
	}

	uintptr_t get_fullscreen_monitor() override
	{
		return reinterpret_cast<uintptr_t>(current_hmonitor);
	}
#endif

	template <typename Op>
	void push_task_to_async_thread(Op &&op)
	{
		push_task_to_list(task_list_async, std::forward<Op>(op));
	}

	template <typename Op>
	void push_non_pollable_task_to_async_thread(Op &&op)
	{
		push_non_pollable_task_to_list(task_list_async, std::forward<Op>(op));
	}

private:
	SDL_Window *window = nullptr;
	unsigned width = 0;
	unsigned height = 0;
	uint32_t wake_event_type = 0;
	Options options;
	std::string clipboard;
	TaskGroupHandle gamepad_init_task;
	std::atomic<bool> gamepad_init_async;

	struct
	{
		VkApplicationInfo info = {};
		std::string name;
	} application;

	std::thread threaded_main_loop;
	struct TaskList
	{
		std::mutex lock;
		std::condition_variable cond;
		std::vector<std::function<void ()>> list;
		std::vector<std::function<void ()>> non_pollable_list;
	} task_list_main, task_list_async;

	static void process_events_for_list(TaskList &list, bool blocking)
	{
		std::unique_lock<std::mutex> holder{list.lock};

		if (blocking)
			while (list.list.empty())
				list.cond.wait(holder, [&list]() { return !list.list.empty(); });

		for (auto &task : list.list)
			task();
		list.list.clear();
	}

	template <typename Op>
	void push_task_to_list(TaskList &list, Op &&op)
	{
		std::lock_guard<std::mutex> holder{list.lock};
		list.list.emplace_back(std::forward<Op>(op));
		list.cond.notify_one();
	}

	template <typename Op>
	void push_non_pollable_task_to_list(TaskList &list, Op &&op)
	{
		std::lock_guard<std::mutex> holder{list.lock};
		list.non_pollable_list.emplace_back(std::forward<Op>(op));
		list.cond.notify_one();
	}

	void process_events_main_thread()
	{
		process_events_for_list(task_list_main, false);
	}

	void process_events_main_thread_blocking()
	{
		process_events_for_list(task_list_main, true);
	}

	void process_events_async_thread()
	{
		process_events_for_list(task_list_async, false);
	}

	void process_events_async_thread_non_pollable()
	{
		std::unique_lock<std::mutex> holder{task_list_async.lock};
		for (auto &task : task_list_async.non_pollable_list)
			task();
		task_list_async.non_pollable_list.clear();
	}

	void process_events_async_thread_blocking()
	{
		process_events_for_list(task_list_async, true);
	}

	InputTrackerSDL pad;

	template <typename Op>
	void push_task_to_main_thread(Op &&op)
	{
		push_task_to_list(task_list_main, std::forward<Op>(op));
		SDL_Event wake_event = {};
		wake_event.type = wake_event_type;
		SDL_PushEvent(&wake_event);
	}

	std::atomic_bool request_tear_down;
	bool async_loop_alive = false;

#ifdef _WIN32
	HMONITOR current_hmonitor = nullptr;
#endif
};
}

namespace Granite
{
int application_main(
		bool (*query_application_interface)(ApplicationQuery, void *, size_t),
		Application *(*create_application)(int, char **), int argc, char *argv[])
{
	ApplicationQueryDefaultManagerFlags flags{Global::MANAGER_FEATURE_DEFAULT_BITS};
	query_application_interface(ApplicationQuery::DefaultManagerFlags, &flags, sizeof(flags));
	Global::init(flags.manager_feature_flags);

	WSIPlatformSDL::Options options;
	int exit_code;

	Util::CLICallbacks cbs;
	cbs.add("--fullscreen", [&](Util::CLIParser &) { options.fullscreen = true; });
	cbs.add("--width", [&](Util::CLIParser &parser) { options.override_width = parser.next_uint(); });
	cbs.add("--height", [&](Util::CLIParser &parser) { options.override_height = parser.next_uint(); });
	cbs.add("--thread-main-loop", [&](Util::CLIParser &) { options.threaded = true; });
	cbs.add("--no-thread-main-loop", [&](Util::CLIParser &) { options.threaded = false; });
	cbs.error_handler = [&]() { LOGE("Failed to parse CLI arguments for SDL.\n"); };
	if (!Util::parse_cli_filtered(std::move(cbs), argc, argv, exit_code))
		return exit_code;

	auto app = std::unique_ptr<Application>(create_application(argc, argv));
	int ret;

	if (app)
	{
		auto platform = std::make_unique<WSIPlatformSDL>(options);
		auto *platform_handle = platform.get();

		if (!platform->init(app->get_name(), app->get_default_width(), app->get_default_height()))
			return 1;

		if (!app->init_platform(std::move(platform)) || !app->init_wsi())
			return 1;

		platform_handle->run_loop(app.get());

		app.reset();
		ret = EXIT_SUCCESS;
	}
	else
	{
		ret = EXIT_FAILURE;
	}

	Global::deinit();
	return ret;
}
}
