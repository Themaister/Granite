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

#pragma once

#include "application.hpp"
#include "render_context.hpp"
#include "scene_loader.hpp"
#include "animation_system.hpp"
#include "renderer.hpp"
#include "timer.hpp"
#include "event.hpp"
#include "font.hpp"
#include "ui_manager.hpp"
#include "render_graph.hpp"
#include "mesh_util.hpp"
#include "scene_renderer.hpp"
#include "lights/clusterer.hpp"
#include "lights/volumetric_fog.hpp"
#include "lights/deferred_lights.hpp"
#include "camera_export.hpp"
#include "post/aa.hpp"
#include "post/temporal.hpp"

namespace Granite
{
class TaskComposer;

class SceneViewerApplication : public Application, public EventHandler
{
public:
	SceneViewerApplication(const std::string &path, const std::string &config_path, const std::string &quirks_path);
	~SceneViewerApplication();
	void render_frame(double frame_time, double elapsed_time) override;
	void rescale_scene(float radius);
	void loop_animations();

protected:
	void update_scene(TaskComposer &composer, double frame_time, double elapsed_time);
	void render_scene(TaskComposer &composer);

	RenderContext context;
	RenderContext depth_context;

	RendererSuite renderer_suite;
	RendererSuite::Config renderer_suite_config;
	FlatRenderer flat_renderer;
	LightingParameters lighting;
	FPSCamera cam;
	SceneLoader scene_loader;
	std::unique_ptr<AnimationSystem> animation_system;

	Camera *selected_camera = nullptr;
	DirectionalLightComponent *selected_directional = nullptr;
	DirectionalLightComponent default_directional_light;

	void on_device_created(const Vulkan::DeviceCreatedEvent &e);
	void on_device_destroyed(const Vulkan::DeviceCreatedEvent &e);
	void on_swapchain_changed(const Vulkan::SwapchainParameterEvent &e);
	void on_swapchain_destroyed(const Vulkan::SwapchainParameterEvent &e);
	bool on_key_down(const KeyboardEvent &e);
	RenderGraph graph;

	Vulkan::Texture *reflection = nullptr;
	Vulkan::Texture *irradiance = nullptr;

	bool need_shadow_map_update = true;
	std::string skydome_reflection;
	std::string skydome_irradiance;
	float skydome_intensity = 1.0f;
	AABB shadow_scene_aabb;

	std::unique_ptr<LightClusterer> cluster;
	std::unique_ptr<VolumetricFog> volumetric_fog;
	DeferredLights deferred_lights;
	RenderQueue queue;

	void setup_shadow_map();
	void update_shadow_scene_aabb();
	void render_ui(Vulkan::CommandBuffer &cmd);

	void add_main_pass(Vulkan::Device &device, const std::string &tag);
	void add_main_pass_forward(Vulkan::Device &device, const std::string &tag);
	void add_main_pass_deferred(Vulkan::Device &device, const std::string &tag);

	void add_shadow_pass(Vulkan::Device &device, const std::string &tag);

	std::vector<RecordedCamera> recorded_cameras;

private:
	void read_config(const std::string &path);
	void read_quirks(const std::string &path);
	struct Config
	{
		RendererType renderer_type = RendererType::GeneralDeferred;
		unsigned msaa = 1;
		float shadow_map_resolution = 2048.0f;
		unsigned clustered_lights_shadow_resolution = 512;
		int camera_index = -1;

		unsigned max_spot_lights = 32;
		unsigned max_point_lights = 32;

		SceneRendererFlags pcf_flags = 0;
		bool directional_light_shadows = true;
		bool directional_light_cascaded_shadows = true;
		bool directional_light_shadows_vsm = false;
		bool clustered_lights = false;
		bool clustered_lights_bindless = false;
		bool clustered_lights_shadows = true;
		bool clustered_lights_shadows_vsm = false;
		bool hdr_bloom = true;
		bool hdr_bloom_dynamic_exposure = true;
		bool forward_depth_prepass = false;
		bool deferred_clustered_stencil_culling = true;
		bool rt_fp16 = false;
		bool timestamps = false;
		bool rescale_scene = false;
		bool show_ui = true;
		bool volumetric_fog = false;
		bool ssao = true;
		PostAAType postaa_type = PostAAType::None;
	};
	Config config;

	void export_lights();
	void export_cameras();

	enum { FrameWindowSize = 64, FrameWindowSizeMask = FrameWindowSize - 1 };
	float last_frame_times[FrameWindowSize] = {};
	unsigned last_frame_index = 0;

	TemporalJitter jitter;
	void capture_environment_probe();

	RenderTextureResource *ssao_output = nullptr;
	RenderTextureResource *shadows = nullptr;
};
}