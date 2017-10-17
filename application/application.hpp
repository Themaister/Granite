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
#include "lights/clusterer.hpp"

namespace Granite
{

class Application
{
public:
	Application();
	virtual ~Application() = default;
	virtual void render_frame(double frame_time, double elapsed_time) = 0;
	bool init_wsi(std::unique_ptr<Vulkan::WSIPlatform> platform);

	Vulkan::WSI &get_wsi()
	{
		return wsi;
	}

	Vulkan::WSIPlatform &get_platform()
	{
		return *platform;
	}

	virtual std::string get_name()
	{
		return "granite";
	}

	virtual unsigned get_version()
	{
		return 0;
	}

	virtual unsigned get_default_width()
	{
		return 1280;
	}

	virtual unsigned get_default_height()
	{
		return 720;
	}

	bool poll();
	void run_frame();

private:
	unsigned width = 0, height = 0;
	Vulkan::WSI wsi;
	std::unique_ptr<Vulkan::WSIPlatform> platform;
};

class SceneViewerApplication : public Application, public EventHandler
{
public:
	SceneViewerApplication(const std::string &path, const std::string &config_path);
	~SceneViewerApplication();
	void render_frame(double frame_time, double elapsed_time) override;
	void rescale_scene(float radius);
	void loop_animations();

protected:
	void update_scene(double frame_time, double elapsed_time);
	void render_scene();

	RenderContext context;
	RenderContext depth_context;
	Renderer forward_renderer;
	Renderer deferred_renderer;
	Renderer depth_renderer;
	LightingParameters lighting;
	FPSCamera cam;
	VisibilityList visible;
	VisibilityList depth_visible;
	SceneLoader scene_loader;
	std::unique_ptr<AnimationSystem> animation_system;

	Camera *selected_camera = nullptr;
	DirectionalLightComponent *selected_directional = nullptr;
	DirectionalLightComponent default_directional_light;

	void on_device_created(const Vulkan::DeviceCreatedEvent &e);
	void on_device_destroyed(const Vulkan::DeviceCreatedEvent &e);
	void on_swapchain_changed(const Vulkan::SwapchainParameterEvent &e);
	void on_swapchain_destroyed(const Vulkan::SwapchainParameterEvent &e);
	RenderGraph graph;

	Vulkan::Texture *reflection = nullptr;
	Vulkan::Texture *irradiance = nullptr;

	bool need_shadow_map_update = true;
	void update_shadow_map();
	std::string skydome_reflection;
	std::string skydome_irradiance;
	AABB shadow_scene_aabb;

	std::unique_ptr<LightClusterer> cluster;

	void update_shadow_scene_aabb();
	void render_shadow_map_near(Vulkan::CommandBuffer &cmd);
	void render_shadow_map_far(Vulkan::CommandBuffer &cmd);
	void render_main_pass(Vulkan::CommandBuffer &cmd, const mat4 &proj, const mat4 &view);
	void render_transparent_objects(Vulkan::CommandBuffer &cmd, const mat4 &proj, const mat4 &view);
	void render_positional_lights(Vulkan::CommandBuffer &cmd, const mat4 &proj, const mat4 &view);

	void add_main_pass(Vulkan::Device &device, const std::string &tag);
	void add_main_pass_forward(Vulkan::Device &device, const std::string &tag);
	void add_main_pass_deferred(Vulkan::Device &device, const std::string &tag);

	enum class DepthPassType
	{
		Main,
		Near
	};
	void add_shadow_pass(Vulkan::Device &device, const std::string &tag, DepthPassType type);

private:
	void read_config(const std::string &path);
	struct Config
	{
		RendererType renderer_type = RendererType::GeneralForward;
		unsigned msaa = 1;
		bool directional_light_shadows = true;
		bool directional_light_cascaded_shadows = true;
		bool clustered_lights = true;
		bool clustered_lights_shadows = true;
		bool hdr_bloom = true;

		float shadow_map_resolution_main = 2048.0f;
		float shadow_map_resolution_near = 1024.0f;
		int camera_index = -1;

		bool rt_fp16 = false;
		bool timestamps = false;
		bool rescale_scene = false;
		bool force_shadow_map_update = false;
		float cascade_cutoff_distance = 10.0f;
	};
	Config config;
};

extern Application *application_create(int argc, char *argv[]);

// Call this to ensure application-main is linked in correctly without having to mess around
// with -Wl,--whole-archive.
void application_dummy();
}
