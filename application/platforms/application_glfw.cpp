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

#include "application.hpp"
#include "application_wsi.hpp"
#include "application_events.hpp"
#include "vulkan_headers.hpp"
#include "GLFW/glfw3.h"
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

		auto *em = Global::event_manager();
		if (em)
		{
			em->dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
			em->enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Stopped);
			em->dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
			em->enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Paused);
			em->dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
			em->enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Running);
		}

#ifdef HAVE_LINUX_INPUT
		if (!input_manager.init(LINUX_INPUT_MANAGER_JOYPAD_BIT, &get_input_tracker()))
			LOGE("Failed to initialize input manager.\n");
#elif defined(HAVE_XINPUT_WINDOWS)
		if (!input_manager.init(&get_input_tracker()))
			LOGE("Failed to initialize input manager.\n");
#endif
		return true;
	}

	bool alive(Vulkan::WSI &) override
	{
		glfwPollEvents();
#if defined(HAVE_LINUX_INPUT) || defined(HAVE_XINPUT_WINDOWS)
		input_manager.poll();
#endif
		return !glfwWindowShouldClose(window);
	}

	void poll_input() override
	{
		glfwPollEvents();
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
		auto *em = Global::event_manager();
		if (em)
		{
			em->dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
			em->enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Paused);
			em->dequeue_all_latched(ApplicationLifecycleEvent::get_type_id());
			em->enqueue_latched<ApplicationLifecycleEvent>(ApplicationLifecycle::Stopped);
		}

		if (window)
			glfwDestroyWindow(window);
	}

	void notify_resize(unsigned width_, unsigned height_)
	{
		resize = true;
		width = width_;
		height = height_;
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
		if (window)
			glfwSetWindowTitle(window, title.c_str());
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

private:
	GLFWwindow *window = nullptr;
	unsigned width = 0;
	unsigned height = 0;
	CachedWindow cached_window;

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
		glfwSetWindowShouldClose(window, GLFW_TRUE);
	else if (action == GLFW_PRESS && key == GLFW_KEY_ENTER && mods == GLFW_MOD_ALT)
	{
#ifdef _WIN32
		glfw->set_hmonitor(nullptr);
#endif
		if (glfwGetWindowMonitor(window))
		{
			auto cached = glfw->get_cached_window();
			glfwSetWindowMonitor(window, nullptr, cached.x, cached.y, cached.width, cached.height, 0);
		}
		else
		{
			auto *primary = glfwGetPrimaryMonitor();
			if (primary)
			{
				auto *mode = glfwGetVideoMode(primary);
				WSIPlatformGLFW::CachedWindow win;
				glfwGetWindowPos(window, &win.x, &win.y);
				glfwGetWindowSize(window, &win.width, &win.height);
				glfw->set_cached_window(win);
				glfwSetWindowMonitor(window, primary, 0, 0, mode->width, mode->height, mode->refreshRate);
#ifdef _WIN32
				glfw->set_hmonitor(MonitorFromWindow(glfwGetWin32Window(window), MONITOR_DEFAULTTOPRIMARY));
#endif
			}
		}
	}
	else
		glfw->get_input_tracker().key_event(gkey, state);
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
	glfw->get_input_tracker().mouse_button_event(btn, x, y, action == GLFW_PRESS);
}

static void cursor_cb(GLFWwindow *window, double x, double y)
{
	auto *glfw = static_cast<WSIPlatformGLFW *>(glfwGetWindowUserPointer(window));
	glfw->get_input_tracker().mouse_move_event_absolute(x, y);
}

static void enter_cb(GLFWwindow *window, int entered)
{
	auto *glfw = static_cast<WSIPlatformGLFW *>(glfwGetWindowUserPointer(window));
	if (entered)
	{
		double x, y;
		glfwGetCursorPos(window, &x, &y);
		glfw->get_input_tracker().mouse_enter(x, y);
	}
	else
		glfw->get_input_tracker().mouse_leave();
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
		if (!platform->init(app->get_default_width(), app->get_default_height()))
			return 1;

		if (!app->init_wsi(move(platform)))
			return 1;

		Granite::Global::start_audio_system();
		while (app->poll())
			app->run_frame();
		Granite::Global::stop_audio_system();

		app.reset();
		Granite::Global::deinit();
		return 0;
	}
	else
		return 1;
}
}
