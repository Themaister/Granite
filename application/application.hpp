/* Copyright (c) 2017 Hans-Kristian Arntzen
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

#pragma once

#include <window.hpp>
#include "wsi.hpp"
#include "render_context.hpp"
#include "scene_loader.hpp"
#include "animation_system.hpp"
#include "renderer.hpp"
#include "input.hpp"
#include "timer.hpp"
#include "event.hpp"
#include "font.hpp"
#include "ui_manager.hpp"
#include "render_graph.hpp"
#include "mesh_util.hpp"

namespace Granite
{
enum class ApplicationLifecycle
{
	Running,
	Paused,
	Stopped,
	Dead
};

class ApplicationLifecycleEvent : public Event
{
public:
	static constexpr EventType type_id = GRANITE_EVENT_TYPE_HASH(ApplicationLifecycleEvent);
	ApplicationLifecycleEvent(ApplicationLifecycle lifecycle)
		: lifecycle(lifecycle)
	{
	}

	ApplicationLifecycle get_lifecycle() const
	{
		return lifecycle;
	}

private:
	ApplicationLifecycle lifecycle;
};

class FrameTickEvent : public Granite::Event
{
public:
	static constexpr Granite::EventType type_id = GRANITE_EVENT_TYPE_HASH(FrameTickEvent);

	FrameTickEvent(double frame_time, double elapsed_time)
		: Granite::Event(type_id), frame_time(frame_time), elapsed_time(elapsed_time)
	{
	}

	double get_frame_time() const
	{
		return frame_time;
	}

	double get_elapsed_time() const
	{
		return elapsed_time;
	}

private:
	double frame_time;
	double elapsed_time;
};

class ApplicationPlatform
{
public:
	virtual ~ApplicationPlatform() = default;

	virtual VkSurfaceKHR create_surface(VkInstance instance, VkPhysicalDevice gpu) = 0;
	virtual std::vector<const char *> get_instance_extensions() = 0;
	virtual std::vector<const char *> get_device_extensions()
	{
		return { "VK_KHR_swapchain" };
	}

	virtual VkFormat get_preferred_format()
	{
		return VK_FORMAT_B8G8R8A8_SRGB;
	}

	bool should_resize()
	{
		return resize;
	}

	void acknowledge_resize()
	{
		resize = false;
	}

	virtual uint32_t get_surface_width() = 0;
	virtual uint32_t get_surface_height() = 0;

	virtual float get_aspect_ratio()
	{
		return float(get_surface_width()) / float(get_surface_height());
	}

	virtual bool alive(Vulkan::WSI &wsi) = 0;
	virtual void poll_input() = 0;

	Util::FrameTimer &get_frame_timer()
	{
		return timer;
	}

	InputTracker &get_input_tracker()
	{
		return tracker;
	}

	void kill()
	{
		killed = true;
	}

protected:
	bool resize = false;
	bool killed = false;

private:
	Util::FrameTimer timer;
	InputTracker tracker;
};

class Application
{
public:
	Application(unsigned width, unsigned height);
	virtual ~Application() = default;
	virtual void render_frame(double frame_time, double elapsed_time) = 0;

	Vulkan::WSI &get_wsi()
	{
		return wsi;
	}

	ApplicationPlatform &get_platform()
	{
		return *platform;
	}

	int run();

private:
	std::unique_ptr<ApplicationPlatform> platform;
	Vulkan::WSI wsi;
};

class SceneViewerApplication : public Application, public EventHandler
{
public:
	SceneViewerApplication(const std::string &path, unsigned width, unsigned height);
	void render_frame(double frame_time, double elapsed_time) override;

private:
	RenderContext context;
	RenderContext depth_context;
	Renderer renderer;
	Renderer depth_renderer;
	LightingParameters lighting;
	FPSCamera cam;
	VisibilityList visible;
	VisibilityList depth_visible;
	SceneLoader scene_loader;
	std::unique_ptr<AnimationSystem> animation_system;
	UI::Window *window;

	void on_device_created(const Event &e);
	void on_device_destroyed(const Event &e);
	void on_swapchain_changed(const Event &e);
	void on_swapchain_destroyed(const Event &e);
	RenderGraph graph;

	Vulkan::Texture *reflection = nullptr;
	Vulkan::Texture *irradiance = nullptr;

	bool need_shadow_map_update = true;
	void update_shadow_map();
	std::string skydome_reflection;
	std::string skydome_irradiance;
	AABB scene_aabb;

	void lighting_pass(Vulkan::CommandBuffer &cmd, bool reflection_pass);
	void render_shadow_map_near(Vulkan::CommandBuffer &cmd);
	void render_shadow_map_far(Vulkan::CommandBuffer &cmd);
	void render_main_pass(Vulkan::CommandBuffer &cmd, const mat4 &proj, const mat4 &view);
	TexturePlane plane_reflection;

	enum class MainPassType
	{
		Main,
		Reflection,
		Refraction
	};
	void add_main_pass(Vulkan::Device &device, const std::string &tag, MainPassType type);

	enum class DepthPassType
	{
		Main,
		Near
	};
	void add_shadow_pass(Vulkan::Device &device, const std::string &tag, DepthPassType type);
};

extern int application_main(int argc, char *argv[]);
std::unique_ptr<ApplicationPlatform> create_default_application_platform(unsigned width, unsigned height);
}