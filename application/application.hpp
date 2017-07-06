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
	struct GBufferImpl : RenderPassImplementation
	{
		GBufferImpl(SceneViewerApplication *app)
			: app(app)
		{
		}

		bool get_clear_color(unsigned index, VkClearColorValue *value) override;
		bool get_clear_depth_stencil(VkClearDepthStencilValue *value) override;
		void build_render_pass(RenderPass &pass, Vulkan::CommandBuffer &cmd) override;
		SceneViewerApplication *app;
	};
	GBufferImpl gbuffer_impl;

	struct LightingImpl : RenderPassImplementation
	{
		LightingImpl(SceneViewerApplication *app)
			: app(app)
		{
		}

		void build_render_pass(RenderPass &pass, Vulkan::CommandBuffer &cmd) override;
		SceneViewerApplication *app;
	};
	LightingImpl lighting_impl;

	struct UIImpl : RenderPassImplementation
	{
		UIImpl(SceneViewerApplication *app)
			: app(app)
		{
		}
		void build_render_pass(RenderPass &pass, Vulkan::CommandBuffer &cmd) override;

		SceneViewerApplication *app;
	};
	UIImpl ui_impl;

	RenderContext context;
	Renderer renderer;
	FPSCamera cam;
	VisibilityList visible;
	SceneLoader scene_loader;
	std::unique_ptr<AnimationSystem> animation_system;
	UI::Window *window;

	void on_swapchain_changed(const Event &e);
	void on_swapchain_destroyed(const Event &e);
	RenderGraph graph;
};

extern int application_main(int argc, char *argv[]);
std::unique_ptr<ApplicationPlatform> create_default_application_platform(unsigned width, unsigned height);
}