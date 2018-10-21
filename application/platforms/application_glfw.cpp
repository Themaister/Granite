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

#include "application.hpp"
#include "application_events.hpp"
#include "application_wsi.hpp"
#include "vulkan.hpp"
#include "GLFW/glfw3.h"
#ifdef HAVE_LINUX_INPUT
#include "input_linux.hpp"
#elif defined(HAVE_XINPUT_WINDOWS)
#include "xinput_windows.hpp"
#endif

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
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
	WSIPlatformGLFW(unsigned width, unsigned height)
		: width(width), height(height)
	{
		if (!glfwInit())
			throw runtime_error("Failed to initialize GLFW.");

		if (!Context::init_loader(GetInstanceProcAddr))
			throw runtime_error("Failed to initialize Vulkan loader.");

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

	void notify_resize(unsigned width, unsigned height)
	{
		resize = true;
		this->width = width;
		this->height = height;
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

	void set_window_title(const string &title)
	{
		if (window)
			glfwSetWindowTitle(window, title.c_str());
	}

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

void application_dummy()
{
}
}

#ifdef _WIN32
int CALLBACK WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
#else
int main(int argc, char *argv[])
#endif
{
#ifdef _WIN32
	int argc;
	wchar_t **wide_argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	vector<char *> argv_buffer(argc + 1);
	char **argv = nullptr;

	if (wide_argv)
	{
		argv = argv_buffer.data();
		for (int i = 0; i < argc; i++)
		{
			auto length = wcslen(wide_argv[i]);
			argv_buffer[i] = new char[length + 1];
			size_t num_converted;
			wcstombs_s(&num_converted, argv_buffer[i], length + 1, wide_argv[i], length + 1);
		}
	}
#endif

	Granite::Global::init();

	auto app = unique_ptr<Granite::Application>(Granite::application_create(argc, argv));

#ifdef _WIN32
	for (auto &arg : argv_buffer)
		delete[] arg;
#endif

	if (app)
	{
		if (!app->init_wsi(make_unique<Granite::WSIPlatformGLFW>(1280, 720)))
			return 1;

		Granite::Global::start_audio_system();
		while (app->poll())
			app->run_frame();
		Granite::Global::stop_audio_system();
		return 0;
	}
	else
		return 1;
}
