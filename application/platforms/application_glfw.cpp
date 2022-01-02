/* Copyright (c) 2017-2022 Hans-Kristian Arntzen
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
#include "application_wsi.hpp"
#include "application_events.hpp"
#include "vulkan_headers.hpp"
#include "global_managers_init.hpp"
#include "thread_group.hpp"
#include "thread_id.hpp"
#include "GLFW/glfw3.h"
#include <thread>
#include <condition_variable>
#include <mutex>
#include <functional>
#ifdef HAVE_LINUX_INPUT
#include "input_linux.hpp"
#elif defined(HAVE_XINPUT_WINDOWS)
#include "xinput_windows.hpp"
#endif

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include "GLFW/glfw3native.h"
#endif

using namespace std;
using namespace Vulkan;

namespace Granite
{
static void fb_size_cb(GLFWwindow *window, int width, int height);
static void key_cb(GLFWwindow *window, int key, int, int action, int);
static void button_cb(GLFWwindow *window, int button, int action, int);
static void cursor_cb(GLFWwindow *window, double x, double y);
static void enter_cb(GLFWwindow *window, int entered);
static void close_cb(GLFWwindow *window);

// glfwGetProcAddr uses different calling convention on Windows.
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetInstanceProcAddr(VkInstance instance, const char *name)
{
	return reinterpret_cast<PFN_vkVoidFunction>(glfwGetInstanceProcAddress(instance, name));
}

struct WSIPlatformGLFW : GraniteWSIPlatform
{
public:
	bool init(unsigned width_, unsigned height_)
	{
		request_tear_down.store(false);
		width = width_;
		height = height_;

		if (!glfwInit())
		{
			LOGE("Failed to initialize GLFW.\n");
			return false;
		}

		if (!Context::init_loader(GetInstanceProcAddr))
		{
			LOGE("Failed to initialize Vulkan loader.\n");
			return false;
		}

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		window = glfwCreateWindow(width, height, "GLFW Window", nullptr, nullptr);

		glfwSetWindowUserPointer(window, this);
		glfwSetFramebufferSizeCallback(window, fb_size_cb);
		glfwSetKeyCallback(window, key_cb);
		glfwSetMouseButtonCallback(window, button_cb);
		glfwSetCursorPosCallback(window, cursor_cb);
		glfwSetCursorEnterCallback(window, enter_cb);
		glfwSetWindowCloseCallback(window, close_cb);

		return true;
	}

	bool alive(Vulkan::WSI &) override
	{
		process_events_async_thread();
#if defined(HAVE_LINUX_INPUT) || defined(HAVE_XINPUT_WINDOWS)
		input_manager.poll();
#endif
		return !request_tear_down.load();
	}

	void poll_input() override
	{
		process_events_async_thread();
		get_input_tracker().dispatch_current_state(get_frame_timer().get_frame_time());
	}

	vector<const char *> get_instance_extensions() override
	{
		uint32_t count;
		const char **ext = glfwGetRequiredInstanceExtensions(&count);
		return { ext, ext + count };
	}

	VkSurfaceKHR create_surface(VkInstance instance, VkPhysicalDevice) override
	{
		VkSurfaceKHR surface = VK_NULL_HANDLE;
		if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS)
			return VK_NULL_HANDLE;

		int actual_width, actual_height;
		glfwGetFramebufferSize(window, &actual_width, &actual_height);
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

	~WSIPlatformGLFW()
	{
		if (window)
			glfwDestroyWindow(window);
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

	struct CachedWindow
	{
		int x = 0, y = 0, width = 0, height = 0;
	};

	CachedWindow get_cached_window() const
	{
		return cached_window;
	}

	void set_cached_window(const CachedWindow &win)
	{
		cached_window = win;
	}

	void set_window_title(const string &title) override
	{
		push_task_to_main_thread([=]() {
			if (window)
				glfwSetWindowTitle(window, title.c_str());
		});
	}

	int run_main_loop()
	{
		while (!glfwWindowShouldClose(window))
		{
			glfwWaitEvents();
			process_events_main_thread();
			if (!async_loop_alive)
				glfwSetWindowShouldClose(window, GLFW_TRUE);
		}

		return 0;
	}

	int run_async_loop(Application *app)
	{
		auto ctx = Global::create_thread_context();
		async_loop_alive = true;
		threaded_main_loop = std::thread(&WSIPlatformGLFW::thread_main, this, app, std::move(ctx));

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

	void init_input_managers()
	{
#ifdef HAVE_LINUX_INPUT
		if (!input_manager.init(LINUX_INPUT_MANAGER_JOYPAD_BIT, &get_input_tracker()))
			LOGE("Failed to initialize input manager.\n");
#elif defined(HAVE_XINPUT_WINDOWS)
		if (!input_manager.init(&get_input_tracker()))
			LOGE("Failed to initialize input manager.\n");
#endif
	}

	void thread_main(Application *app, Global::GlobalManagersHandle ctx)
	{
		// Set this up as an alternative main thread.
		ThreadGroup::set_async_main_thread_name();
		Global::set_thread_context(*ctx);
		Util::register_thread_index(0);
		ctx.reset();

		dispatch_running_events();
		init_input_managers();
		{
			Granite::Global::start_audio_system();
			while (app->poll())
				app->run_frame();
			Granite::Global::stop_audio_system();
		}
		dispatch_stopped_events();
		push_task_to_main_thread([this]() { async_loop_alive = false; });
	}

	void notify_close()
	{
		glfwSetWindowShouldClose(window, GLFW_TRUE);
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

private:
	GLFWwindow *window = nullptr;
	unsigned width = 0;
	unsigned height = 0;
	CachedWindow cached_window;

	std::thread threaded_main_loop;
	struct TaskList
	{
		std::mutex lock;
		std::condition_variable cond;
		std::vector<std::function<void ()>> list;
	} task_list_main, task_list_async;

	void process_events_for_list(TaskList &list, bool blocking)
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

	template <typename Op>
	void push_task_to_main_thread(Op &&op)
	{
		push_task_to_list(task_list_main, std::forward<Op>(op));
		glfwPostEmptyEvent();
	}

	std::atomic_bool request_tear_down;
	bool async_loop_alive = false;

#ifdef HAVE_LINUX_INPUT
	LinuxInputManager input_manager;
#elif defined(HAVE_XINPUT_WINDOWS)
	XInputManager input_manager;
#endif

#ifdef _WIN32
	HMONITOR current_hmonitor = nullptr;
#endif
};

static void fb_size_cb(GLFWwindow *window, int width, int height)
{
	auto *glfw = static_cast<WSIPlatformGLFW *>(glfwGetWindowUserPointer(window));
	VK_ASSERT(width != 0 && height != 0);
	glfw->notify_resize(width, height);
}

static Key glfw_key_to_granite(int key)
{
#define k(glfw, granite) case GLFW_KEY_##glfw: return Key::granite
	switch (key)
	{
	k(A, A);
	k(B, B);
	k(C, C);
	k(D, D);
	k(E, E);
	k(F, F);
	k(G, G);
	k(H, H);
	k(I, I);
	k(J, J);
	k(K, K);
	k(L, L);
	k(M, M);
	k(N, N);
	k(O, O);
	k(P, P);
	k(Q, Q);
	k(R, R);
	k(S, S);
	k(T, T);
	k(U, U);
	k(V, V);
	k(W, W);
	k(X, X);
	k(Y, Y);
	k(Z, Z);
	k(LEFT_CONTROL, LeftCtrl);
	k(LEFT_ALT, LeftAlt);
	k(LEFT_SHIFT, LeftShift);
	k(ENTER, Return);
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

static void key_cb(GLFWwindow *window, int key, int, int action, int mods)
{
	KeyState state;
	switch (action)
	{
	case GLFW_PRESS:
		state = KeyState::Pressed;
		break;

	default:
	case GLFW_RELEASE:
		state = KeyState::Released;
		break;

	case GLFW_REPEAT:
		state = KeyState::Repeat;
		break;
	}

	auto gkey = glfw_key_to_granite(key);
	auto *glfw = static_cast<WSIPlatformGLFW *>(glfwGetWindowUserPointer(window));

	if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE)
	{
		glfw->notify_close();
	}
	else if (action == GLFW_PRESS && key == GLFW_KEY_ENTER && mods == GLFW_MOD_ALT)
	{
#ifdef _WIN32
		glfw->push_task_to_async_thread([=]() {
			glfw->set_hmonitor(nullptr);
		});
#endif
		if (glfwGetWindowMonitor(window))
		{
			// Fullscreen -> windowed
			auto cached = glfw->get_cached_window();
			glfwSetWindowMonitor(window, nullptr, cached.x, cached.y, cached.width, cached.height, 0);
		}
		else
		{
			// Windowed -> fullscreen
			auto *primary = glfwGetPrimaryMonitor();
			if (primary)
			{
				auto *mode = glfwGetVideoMode(primary);
				WSIPlatformGLFW::CachedWindow win;
				glfwGetWindowPos(window, &win.x, &win.y);
				glfwGetWindowSize(window, &win.width, &win.height);
				glfw->set_cached_window(win);
#ifdef _WIN32
				glfw->push_task_to_async_thread([=]() {
					glfw->set_hmonitor(MonitorFromWindow(glfwGetWin32Window(window), MONITOR_DEFAULTTOPRIMARY));
				});
#endif
				glfwSetWindowMonitor(window, primary, 0, 0, mode->width, mode->height, mode->refreshRate);
			}
		}
	}
	else
	{
		glfw->push_task_to_async_thread([=]() {
			glfw->get_input_tracker().key_event(gkey, state);
		});
	}
}

static void button_cb(GLFWwindow *window, int button, int action, int)
{
	auto *glfw = static_cast<WSIPlatformGLFW *>(glfwGetWindowUserPointer(window));

	MouseButton btn;
	switch (button)
	{
	default:
	case GLFW_MOUSE_BUTTON_LEFT:
		btn = MouseButton::Left;
		break;
	case GLFW_MOUSE_BUTTON_RIGHT:
		btn = MouseButton::Right;
		break;
	case GLFW_MOUSE_BUTTON_MIDDLE:
		btn = MouseButton::Middle;
		break;
	}

	double x, y;
	glfwGetCursorPos(window, &x, &y);

	glfw->push_task_to_async_thread([=]() {
		glfw->get_input_tracker().mouse_button_event(btn, x, y, action == GLFW_PRESS);
	});
}

static void cursor_cb(GLFWwindow *window, double x, double y)
{
	auto *glfw = static_cast<WSIPlatformGLFW *>(glfwGetWindowUserPointer(window));
	glfw->push_task_to_async_thread([=]() {
		glfw->get_input_tracker().mouse_move_event_absolute(x, y);
	});
}

static void enter_cb(GLFWwindow *window, int entered)
{
	auto *glfw = static_cast<WSIPlatformGLFW *>(glfwGetWindowUserPointer(window));
	if (entered)
	{
		double x, y;
		glfwGetCursorPos(window, &x, &y);
		glfw->push_task_to_async_thread([=]() {
			glfw->get_input_tracker().mouse_enter(x, y);
		});
	}
	else
	{
		glfw->push_task_to_async_thread([=]() {
			glfw->get_input_tracker().mouse_leave();
		});
	}
}

static void close_cb(GLFWwindow *window)
{
	auto *glfw = static_cast<WSIPlatformGLFW *>(glfwGetWindowUserPointer(window));
	glfw->notify_close();
}
}

namespace Granite
{
int application_main(Application *(*create_application)(int, char **), int argc, char *argv[])
{
	Granite::Global::init();
	auto app = unique_ptr<Granite::Application>(create_application(argc, argv));

	if (app)
	{
		auto platform = make_unique<Granite::WSIPlatformGLFW>();
		auto *platform_handle = platform.get();

		if (!platform->init(app->get_default_width(), app->get_default_height()))
			return 1;

		if (!app->init_wsi(move(platform)))
			return 1;

		int ret = platform_handle->run_async_loop(app.get());

		app.reset();
		Granite::Global::deinit();
		return ret;
	}
	else
		return 1;
}
}
