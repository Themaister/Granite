/* Copyright (c) 2017-2023 Hans-Kristian Arntzen
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

#include "application.hpp"
#include "application_wsi.hpp"
#include "application_events.hpp"
#include "input.hpp"
#include "cli_parser.hpp"
#include "global_managers_init.hpp"
#include "timeline_trace_file.hpp"
#include "path_utils.hpp"
#include "thread_group.hpp"
#include "thread_id.hpp"

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

static JoypadKey sdl_gamepad_button_to_granite(SDL_GamepadButton button)
{
	switch (button)
	{
	case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
		return JoypadKey::Down;
	case SDL_GAMEPAD_BUTTON_DPAD_UP:
		return JoypadKey::Up;
	case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
		return JoypadKey::Left;
	case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
		return JoypadKey::Right;
	case SDL_GAMEPAD_BUTTON_GUIDE:
		return JoypadKey::Mode;
	case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
		return JoypadKey::LeftShoulder;
	case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
		return JoypadKey::RightShoulder;
	case SDL_GAMEPAD_BUTTON_WEST:
		return JoypadKey::West;
	case SDL_GAMEPAD_BUTTON_EAST:
		return JoypadKey::East;
	case SDL_GAMEPAD_BUTTON_NORTH:
		return JoypadKey::North;
	case SDL_GAMEPAD_BUTTON_SOUTH:
		return JoypadKey::South;
	case SDL_GAMEPAD_BUTTON_START:
		return JoypadKey::Start;
	case SDL_GAMEPAD_BUTTON_BACK:
		return JoypadKey::Select;
	case SDL_GAMEPAD_BUTTON_LEFT_STICK:
		return JoypadKey::LeftThumb;
	case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
		return JoypadKey::RightThumb;
	default:
		return JoypadKey::Unknown;
	}
}

static JoypadAxis sdl_gamepad_axis_to_granite(SDL_GamepadAxis axis)
{
	switch (axis)
	{
	case SDL_GAMEPAD_AXIS_LEFTX:
		return JoypadAxis::LeftX;
	case SDL_GAMEPAD_AXIS_LEFTY:
		return JoypadAxis::LeftY;
	case SDL_GAMEPAD_AXIS_RIGHTX:
		return JoypadAxis::RightX;
	case SDL_GAMEPAD_AXIS_RIGHTY:
		return JoypadAxis::RightY;
	case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
		return JoypadAxis::LeftTrigger;
	case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
		return JoypadAxis::RightTrigger;
	default:
		return JoypadAxis::Unknown;
	}
}

struct WSIPlatformSDL : GraniteWSIPlatform
{
public:
	struct Options
	{
		unsigned override_width = 0;
		unsigned override_height = 0;
		bool fullscreen = false;
	};

	explicit WSIPlatformSDL(const Options &options_)
		: options(options_)
	{
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

		if (SDL_Init(SDL_INIT_EVENTS | SDL_INIT_GAMEPAD | SDL_INIT_VIDEO) < 0)
		{
			LOGE("Failed to init SDL.\n");
			return false;
		}

		if (SDL_Vulkan_LoadLibrary(nullptr) < 0)
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

		// Open existing gamepads.
		int num_pads = 0;
		SDL_JoystickID *ids = SDL_GetGamepads(&num_pads);
		for (int i = 0; i < num_pads; i++)
			add_gamepad(ids[i]);
		if (ids)
			SDL_free(ids);
		SDL_SetGamepadEventsEnabled(SDL_TRUE);

		return true;
	}

	const VkApplicationInfo *get_application_info() override
	{
		return &application.info;
	}

	void toggle_fullscreen()
	{
		bool is_fullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;

		if (!is_fullscreen)
		{
			if (SDL_SetWindowFullscreen(window, SDL_TRUE) < 0)
			{
				LOGE("Failed to toggle fullscreen.\n");
			}
#ifdef _WIN32
			else
			{
				SDL_PropertiesID props = SDL_GetWindowProperties(window);
				SDL_LockProperties(props);
				auto hwnd = static_cast<HWND>(SDL_GetProperty(props, "SDL.window.win32.hwnd", nullptr));
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
			SDL_SetWindowFullscreen(window, SDL_FALSE);
		}
	}

	bool alive(Vulkan::WSI &) override
	{
		process_events_async_thread();
		return !request_tear_down.load();
	}

	void poll_input() override
	{
		process_events_async_thread();
		get_input_tracker().dispatch_current_state(get_frame_timer().get_frame_time());
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
		SDL_GetWindowSize(window, &actual_width, &actual_height);
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
		if (window)
			SDL_DestroyWindow(window);

		for (auto *pad : pads)
			if (pad)
				SDL_CloseGamepad(pad);

		SDL_Quit();
	}

	void block_until_wsi_forward_progress(Vulkan::WSI &wsi) override
	{
		get_frame_timer().enter_idle();
		while (!resize && alive(wsi))
			process_events_async_thread_blocking();
		get_frame_timer().leave_idle();
	}

	void notify_resize(unsigned width_, unsigned height_)
	{
		uint64_t current_resize_timestamp = swapchain_dimension_update_timestamp;

		push_task_to_async_thread([=]() {
			resize = true;
			width = width_;
			height = height_;
		});

		// Give the async thread a chance to catch up with main thread so it can create a new swapchain before
		// we invalidate the swapchain again.
		// There is a gap when querying swapchain dimensions and when we create the swapchain.
		// On most platforms, the query must match the swapchain,
		// so if we keep processing OS events, things will get out of sync.
		// Need to observe that the async thread updates the swapchain dimensions at least once.
		while (current_resize_timestamp == swapchain_dimension_update_timestamp && async_loop_alive)
			process_events_main_thread_blocking();
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

	int run_main_loop()
	{
		bool alive = true;
		SDL_Event e;

		while (alive && SDL_WaitEvent(&e))
		{
			if (e.type == wake_event_type)
			{
				process_events_main_thread();
				continue;
			}

			switch (e.type)
			{
			case SDL_EVENT_QUIT:
				alive = false;
				break;

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

					if (state == KeyState::Pressed && e.key.keysym.sym == SDLK_ESCAPE)
					{
						alive = false;
					}
					else if (state == KeyState::Pressed && e.key.keysym.sym == SDLK_RETURN &&
					         (e.key.keysym.mod & SDL_KMOD_ALT) != 0)
					{
						toggle_fullscreen();
					}
					else
					{
						Key key = sdl_key_to_granite_key(e.key.keysym.sym);
						push_task_to_async_thread([=]() {
							get_input_tracker().key_event(key, state);
						});
					}
				}
				break;

			case SDL_EVENT_GAMEPAD_ADDED:
			{
				add_gamepad(e.gdevice.which);
				break;
			}

			case SDL_EVENT_GAMEPAD_REMOVED:
			{
				remove_gamepad(e.gdevice.which);
				break;
			}

			case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
			case SDL_EVENT_GAMEPAD_BUTTON_UP:
			{
				int player = SDL_GetJoystickInstancePlayerIndex(e.gbutton.which);
				if (player < 0 || player >= int(InputTracker::Joypads) || !pads[player])
					break;

				JoypadKey key = sdl_gamepad_button_to_granite(SDL_GamepadButton(e.gbutton.button));
				if (key == JoypadKey::Unknown)
					break;

				auto state = e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ?
				             JoypadKeyState::Pressed : JoypadKeyState::Released;

				LOGI("Joykey: %s -> %s\n", joypad_key_to_tag(key),
				     state == JoypadKeyState::Pressed ? "Pressed" : "Released");

				push_task_to_async_thread([=]() {
					get_input_tracker().joypad_key_state(player, key, state);
				});
				break;
			}

			case SDL_EVENT_GAMEPAD_AXIS_MOTION:
			{
				int player = SDL_GetJoystickInstancePlayerIndex(e.gaxis.which);
				if (player < 0 || player >= int(InputTracker::Joypads) || !pads[player])
					break;

				JoypadAxis axis = sdl_gamepad_axis_to_granite(SDL_GamepadAxis(e.gaxis.axis));
				bool is_trigger = axis == JoypadAxis::LeftTrigger || axis == JoypadAxis::RightTrigger;

				float value;
				if (is_trigger)
				{
					value = float(e.gaxis.value) / float(SDL_JOYSTICK_AXIS_MAX);
				}
				else
				{
					value = (float(e.gaxis.value) - SDL_JOYSTICK_AXIS_MIN) /
					        float(SDL_JOYSTICK_AXIS_MAX - SDL_JOYSTICK_AXIS_MIN);
					value = 2.0f * value - 1.0f;
				}

				if (value < -0.5f || value > 0.5f)
					LOGI("Joyaxis: %s -> %.3f\n", joypad_axis_to_tag(axis), value);

				push_task_to_async_thread([=]() {
					get_input_tracker().joyaxis_state(player, axis, value);
				});
				break;
			}

			default:
				break;
			}
		}

		return 0;
	}

	int run_async_loop(Application *app)
	{
		auto ctx = Global::create_thread_context();
		async_loop_alive = true;
		threaded_main_loop = std::thread(&WSIPlatformSDL::thread_main, this, app, std::move(ctx));

		int ret = run_main_loop();
		notify_close();

		if (threaded_main_loop.joinable())
			threaded_main_loop.join();

		return ret;
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
		// Set this up as an alternative main thread.
		ThreadGroup::set_async_main_thread();
		Global::set_thread_context(*ctx);
		Util::register_thread_index(0);
		ctx.reset();

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
		SDL_Event quit_event = {};
		quit_event.type = SDL_EVENT_QUIT;
		SDL_PushEvent(&quit_event);
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

private:
	SDL_Window *window = nullptr;
	unsigned width = 0;
	unsigned height = 0;
	uint32_t wake_event_type = 0;
	Options options;

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

	void process_events_async_thread_blocking()
	{
		process_events_for_list(task_list_async, true);
	}

	void add_gamepad(SDL_JoystickID id)
	{
		int player_index = SDL_GetJoystickInstancePlayerIndex(id);
		if (player_index >= 0 && player_index < int(InputTracker::Joypads) && !pads[player_index])
		{
			uint32_t vid = SDL_GetGamepadInstanceVendor(id);
			uint32_t pid = SDL_GetGamepadInstanceProduct(id);
			const char *name = SDL_GetGamepadInstanceName(id);
			LOGI("Plugging in controller: \"%s\" (%u/%u).\n", name, vid, pid);
			const char *mapping = SDL_GetGamepadInstanceMapping(id);
			LOGI(" Using mapping: \"%s\"\n", mapping);
			pads[player_index] = SDL_OpenGamepad(id);
			push_task_to_async_thread([=]() {
				get_input_tracker().enable_joypad(player_index, vid, pid);
			});
		}
	}

	void remove_gamepad(SDL_JoystickID id)
	{
		int player_index = SDL_GetJoystickInstancePlayerIndex(id);
		if (player_index >= 0 && player_index < int(InputTracker::Joypads) && pads[player_index])
		{
			uint32_t vid = SDL_GetGamepadInstanceVendor(id);
			uint32_t pid = SDL_GetGamepadInstanceProduct(id);
			const char *name = SDL_GetGamepadInstanceName(id);
			LOGI("Unplugging controller: \"%s\" (%u/%u).\n", name, vid, pid);
			SDL_CloseGamepad(pads[player_index]);
			pads[player_index] = nullptr;
			push_task_to_async_thread([=]() {
				get_input_tracker().disable_joypad(player_index, vid, pid);
			});
		}
	}

	SDL_Gamepad *pads[InputTracker::Joypads] = {};

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
	cbs.error_handler = [&]() { LOGE("Failed to parse CLI arguments for SDL.\n"); };
	if (!Util::parse_cli_filtered(std::move(cbs), argc, argv, exit_code))
		return exit_code;

	auto app = std::unique_ptr<Application>(create_application(argc, argv));

	if (app)
	{
		auto platform = std::make_unique<WSIPlatformSDL>(options);
		auto *platform_handle = platform.get();

		if (!platform->init(app->get_name(), app->get_default_width(), app->get_default_height()))
			return 1;

		if (!app->init_platform(std::move(platform)) || !app->init_wsi())
			return 1;

		int ret = platform_handle->run_async_loop(app.get());

		app.reset();
		Global::deinit();
		return ret;
	}
	else
		return 1;
}
}
