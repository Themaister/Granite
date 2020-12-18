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

#include "scene_viewer_application.hpp"
#include "light_export.hpp"
#include "muglm/matrix_helper.hpp"
#include "post/hdr.hpp"
#include "post/ssao.hpp"
#include "rapidjson_wrapper.hpp"
#include "task_composer.hpp"
#include "thread_group.hpp"
#include "utils/image_utils.hpp"
//#include "ocean.hpp"
#include <float.h>
#include <stdexcept>

using namespace std;
using namespace Vulkan;

namespace Granite
{

static vec3 light_direction()
{
	return normalize(vec3(0.5f, 1.2f, 0.8f));
}

void SceneViewerApplication::read_quirks(const std::string &path)
{
	string json;
	if (!Global::filesystem()->read_file_to_string(path, json))
	{
		LOGE("Failed to read quirks file. Assuming defaults.\n");
		return;
	}

	rapidjson::Document doc;
	doc.Parse(json);

	if (doc.HasMember("instanceDeferredLights"))
		ImplementationQuirks::get().instance_deferred_lights = doc["instanceDeferredLights"].GetBool();
	if (doc.HasMember("mergeSubpasses"))
		ImplementationQuirks::get().merge_subpasses = doc["mergeSubpasses"].GetBool();
	if (doc.HasMember("useTransientColor"))
		ImplementationQuirks::get().use_transient_color = doc["useTransientColor"].GetBool();
	if (doc.HasMember("useTransientDepthStencil"))
		ImplementationQuirks::get().use_transient_depth_stencil = doc["useTransientDepthStencil"].GetBool();
	if (doc.HasMember("clusteringListIteration"))
		ImplementationQuirks::get().clustering_list_iteration = doc["clusteringListIteration"].GetBool();
	if (doc.HasMember("clusteringForceCPU"))
		ImplementationQuirks::get().clustering_force_cpu = doc["clusteringForceCPU"].GetBool();
	if (doc.HasMember("queueWaitOnSubmission"))
		ImplementationQuirks::get().queue_wait_on_submission = doc["queueWaitOnSubmission"].GetBool();
	if (doc.HasMember("stagingNeedDeviceLocal"))
		ImplementationQuirks::get().staging_need_device_local = doc["stagingNeedDeviceLocal"].GetBool();
	if (doc.HasMember("useAsyncComputePost"))
		ImplementationQuirks::get().use_async_compute_post = doc["useAsyncComputePost"].GetBool();
	if (doc.HasMember("renderGraphForceSingleQueue"))
		ImplementationQuirks::get().render_graph_force_single_queue = doc["renderGraphForceSingleQueue"].GetBool();
	if (doc.HasMember("forceNoSubgroups"))
		ImplementationQuirks::get().force_no_subgroups = doc["forceNoSubgroups"].GetBool();
}

void SceneViewerApplication::read_config(const std::string &path)
{
	string json;
	if (!Global::filesystem()->read_file_to_string(path, json))
	{
		LOGE("Failed to read config file. Assuming defaults.\n");
		return;
	}

	rapidjson::Document doc;
	doc.Parse(json);

	if (doc.HasMember("renderer"))
	{
		auto *renderer = doc["renderer"].GetString();
		if (strcmp(renderer, "forward") == 0)
			config.renderer_type = RendererType::GeneralForward;
		else if (strcmp(renderer, "deferred") == 0)
			config.renderer_type = RendererType::GeneralDeferred;
		else
			throw invalid_argument("Invalid renderer option.");
	}

	if (doc.HasMember("msaa"))
		config.msaa = doc["msaa"].GetUint();

	if (doc.HasMember("ssao"))
		config.ssao = doc["ssao"].GetBool();

	if (doc.HasMember("directionalLightShadows"))
		config.directional_light_shadows = doc["directionalLightShadows"].GetBool();

	if (doc.HasMember("directionalLightShadowsCascaded"))
		config.directional_light_cascaded_shadows = doc["directionalLightShadowsCascaded"].GetBool();

	if (doc.HasMember("directionalLightShadowsVSM"))
		config.directional_light_shadows_vsm = doc["directionalLightShadowsVSM"].GetBool();

	if (doc.HasMember("PCFKernelWidth"))
	{
		unsigned width = doc["PCFKernelWidth"].GetUint();
		if (width == 5)
			config.pcf_flags = SCENE_RENDERER_SHADOW_PCF_5X_BIT;
		else if (width == 3)
			config.pcf_flags = SCENE_RENDERER_SHADOW_PCF_3X_BIT;
		else if (width == 1)
			config.pcf_flags = 0;
		else
		{
			config.pcf_flags = 0;
			LOGE("Invalid PCFKernelWidth, assuming default of 1.\n");
		}
		renderer_suite_config.pcf_width = width;
	}
	if (doc.HasMember("clusteredLights"))
		config.clustered_lights = doc["clusteredLights"].GetBool();
	if (doc.HasMember("clusteredLightsBindless"))
		config.clustered_lights_bindless = doc["clusteredLightsBindless"].GetBool();
	if (doc.HasMember("clusteredLightsShadows"))
		config.clustered_lights_shadows = doc["clusteredLightsShadows"].GetBool();
	if (doc.HasMember("clusteredLightsShadowsResolution"))
		config.clustered_lights_shadow_resolution = doc["clusteredLightsShadowsResolution"].GetUint();
	if (doc.HasMember("clusteredLightsShadowsVSM"))
		config.clustered_lights_shadows_vsm = doc["clusteredLightsShadowsVSM"].GetBool();
	if (doc.HasMember("hdrBloom"))
		config.hdr_bloom = doc["hdrBloom"].GetBool();
	if (doc.HasMember("hdrBloomDynamicExposure"))
		config.hdr_bloom_dynamic_exposure = doc["hdrBloomDynamicExposure"].GetBool();
	if (doc.HasMember("showUi"))
		config.show_ui = doc["showUi"].GetBool();
	if (doc.HasMember("forwardDepthPrepass"))
		config.forward_depth_prepass = doc["forwardDepthPrepass"].GetBool();
	if (doc.HasMember("deferredClusteredStencilCulling"))
		config.deferred_clustered_stencil_culling = doc["deferredClusteredStencilCulling"].GetBool();

	if (doc.HasMember("shadowMapResolution"))
		config.shadow_map_resolution = doc["shadowMapResolution"].GetFloat();

	if (doc.HasMember("cameraIndex"))
		config.camera_index = doc["cameraIndex"].GetInt();

	if (doc.HasMember("renderTargetFp16"))
		config.rt_fp16 = doc["renderTargetFp16"].GetBool();

	if (doc.HasMember("timestamps"))
		config.timestamps = doc["timestamps"].GetBool();

	if (doc.HasMember("rescaleScene"))
		config.rescale_scene = doc["rescaleScene"].GetBool();

	if (doc.HasMember("postAA"))
	{
		auto *aa = doc["postAA"].GetString();
		config.postaa_type = string_to_post_antialiasing_type(aa);
	}

	if (doc.HasMember("maxSpotLights"))
		config.max_spot_lights = doc["maxSpotLights"].GetUint();
	if (doc.HasMember("maxPointLights"))
		config.max_point_lights = doc["maxPointLights"].GetUint();
	if (doc.HasMember("volumetricFog"))
		config.volumetric_fog = doc["volumetricFog"].GetBool();
}

SceneViewerApplication::SceneViewerApplication(const std::string &path, const std::string &config_path,
                                               const std::string &quirks_path)
{
	renderer_suite.set_default_renderers();

	if (!config_path.empty())
		read_config(config_path);
	if (!quirks_path.empty())
		read_quirks(quirks_path);

	renderer_suite_config.cascaded_directional_shadows = config.directional_light_cascaded_shadows;
	renderer_suite_config.directional_light_vsm = config.directional_light_shadows_vsm;

	scene_loader.load_scene(path);

	// Why not. :D
	//Ocean::add_to_scene(scene_loader.get_scene());

	animation_system = scene_loader.consume_animation_system();
	context.set_lighting_parameters(&lighting);
	cam.set_depth_range(0.1f, 1000.0f);

	auto &ibl = scene_loader.get_scene().get_entity_pool().get_component_group<IBLComponent>();
	if (!ibl.empty())
	{
		auto *ibl_component = get_component<IBLComponent>(ibl.front());
		skydome_reflection = ibl_component->reflection_path;
		skydome_irradiance = ibl_component->irradiance_path;
		skydome_intensity = ibl_component->intensity;
	}

	auto &skybox = scene_loader.get_scene().get_entity_pool().get_component_group<SkyboxComponent>();
	for (auto &box : skybox)
		get_component<SkyboxComponent>(box)->skybox->set_color_mod(vec3(skydome_intensity));

	// Create a dummy background if there isn't any background.
	if (scene_loader.get_scene().get_entity_pool().get_component_group<BackgroundComponent>().empty())
	{
		auto cylinder = Util::make_handle<SkyCylinder>("builtin://textures/background.png");
		cylinder->set_xz_scale(8.0f / pi<float>());
		scene_loader.get_scene().create_renderable(cylinder, nullptr);
	}

	auto *environment = scene_loader.get_scene().get_environment();
	if (environment)
		lighting.fog = environment->fog;
	else
		lighting.fog = {};

	cam.look_at(vec3(0.0f, 0.0f, 8.0f), vec3(0.0f));

	// Pick a camera to show.
	selected_camera = &cam;

	if (config.camera_index >= 0)
	{
		auto &scene_cameras = scene_loader.get_scene().get_entity_pool().get_component_group<CameraComponent>();
		if (!scene_cameras.empty())
		{
			if (unsigned(config.camera_index) < scene_cameras.size())
				selected_camera = &get_component<CameraComponent>(scene_cameras[config.camera_index])->camera;
			else
				LOGE("Camera index is out of bounds, using normal camera.");
		}
	}

	// Pick a directional light.
	default_directional_light.color = vec3(6.0f, 5.5f, 4.5f);
	default_directional_light.direction = light_direction();
	auto &dir_lights = scene_loader.get_scene().get_entity_pool().get_component_group<DirectionalLightComponent>();
	if (!dir_lights.empty())
		selected_directional = get_component<DirectionalLightComponent>(dir_lights.front());
	else
		selected_directional = &default_directional_light;

	if (config.clustered_lights_shadows || config.clustered_lights)
	{
		cluster = make_unique<LightClusterer>();
		auto entity = scene_loader.get_scene().create_entity();
		auto *refresh = entity->allocate_component<PerFrameUpdateComponent>();
		refresh->refresh = cluster.get();

		if (config.clustered_lights)
		{
			auto *rp = entity->allocate_component<RenderPassComponent>();
			rp->creator = cluster.get();
			lighting.cluster = cluster.get();
		}
		else
		{
			cluster->set_scene(&scene_loader.get_scene());
			cluster->set_base_renderer(&renderer_suite);
			cluster->set_base_render_context(&context);
		}

		cluster->set_max_spot_lights(config.max_spot_lights);
		cluster->set_max_point_lights(config.max_point_lights);
		cluster->set_enable_shadows(config.clustered_lights_shadows);
		cluster->set_enable_clustering(config.clustered_lights);
		cluster->set_enable_bindless(config.clustered_lights_bindless);
		cluster->set_shadow_resolution(config.clustered_lights_shadow_resolution);

		if (config.clustered_lights_shadows_vsm)
			cluster->set_shadow_type(LightClusterer::ShadowType::VSM);
		else
			cluster->set_shadow_type(LightClusterer::ShadowType::PCF);

		if (config.clustered_lights_bindless)
		{
			cluster->set_resolution(128, 64, 4 * 1024);
		}
	}

	if (config.volumetric_fog)
	{
		volumetric_fog = make_unique<VolumetricFog>();
		volumetric_fog->set_resolution(160, 92, 64);
		volumetric_fog->set_z_range(80.0f);
		lighting.volumetric_fog = volumetric_fog.get();
		auto entity = scene_loader.get_scene().create_entity();
		auto *rp = entity->allocate_component<RenderPassComponent>();
		rp->creator = volumetric_fog.get();

		if (config.clustered_lights)
		{
			if (config.clustered_lights_bindless)
			{
				volumetric_fog->add_storage_buffer_dependency("cluster-bitmask");
				volumetric_fog->add_storage_buffer_dependency("cluster-range");
				volumetric_fog->add_storage_buffer_dependency("cluster-transforms");
			}
			else
				volumetric_fog->add_texture_dependency("light-cluster");
		}

		if (config.directional_light_shadows)
			volumetric_fog->add_texture_dependency("shadow-main");
	}

	if (config.deferred_clustered_stencil_culling)
	{
		auto entity = scene_loader.get_scene().create_entity();
		entity->allocate_component<PerFrameUpdateComponent>()->refresh = &deferred_lights;
	}
	deferred_lights.set_scene(&scene_loader.get_scene());
	deferred_lights.set_renderers(&renderer_suite);
	deferred_lights.set_enable_clustered_stencil_culling(config.deferred_clustered_stencil_culling);

	context.set_camera(*selected_camera);

	graph.enable_timestamps(config.timestamps);

	if (config.rescale_scene)
		rescale_scene(10.0f);

	EVENT_MANAGER_REGISTER_LATCH(SceneViewerApplication, on_swapchain_changed, on_swapchain_destroyed,
	                             SwapchainParameterEvent);
	EVENT_MANAGER_REGISTER_LATCH(SceneViewerApplication, on_device_created, on_device_destroyed, DeviceCreatedEvent);
	EVENT_MANAGER_REGISTER(SceneViewerApplication, on_key_down, KeyboardEvent);
}

void SceneViewerApplication::export_lights()
{
	auto lights = export_lights_to_json(lighting.directional, scene_loader.get_scene());
	if (!Global::filesystem()->write_string_to_file("cache://lights.json", lights))
		LOGE("Failed to export light data.\n");
}

void SceneViewerApplication::export_cameras()
{
	auto cameras = export_cameras_to_json(recorded_cameras);
	if (!Global::filesystem()->write_string_to_file("cache://cameras.json", cameras))
		LOGE("Failed to export camera data.\n");
}

SceneViewerApplication::~SceneViewerApplication()
{
	export_lights();
	export_cameras();
}

void SceneViewerApplication::loop_animations()
{
}

void SceneViewerApplication::rescale_scene(float radius)
{
	scene_loader.get_scene().update_all_transforms();

	AABB aabb(vec3(FLT_MAX), vec3(-FLT_MAX));
	auto &objects = scene_loader.get_scene()
	                    .get_entity_pool()
	                    .get_component_group<RenderInfoComponent, RenderableComponent>();
	for (auto &caster : objects)
		aabb.expand(get_component<RenderInfoComponent>(caster)->world_aabb);

	float scale_factor = radius / aabb.get_radius();
	auto root_node = scene_loader.get_scene().get_root_node();
	auto new_root_node = scene_loader.get_scene().create_node();
	new_root_node->transform.scale = vec3(scale_factor);
	new_root_node->add_child(root_node);
	scene_loader.get_scene().set_root_node(new_root_node);
}

void SceneViewerApplication::on_device_created(const DeviceCreatedEvent &device)
{
	if (!skydome_reflection.empty())
		reflection = device.get_device().get_texture_manager().request_texture(skydome_reflection);
	if (!skydome_irradiance.empty())
		irradiance = device.get_device().get_texture_manager().request_texture(skydome_irradiance);
	graph.set_device(&device.get_device());
	context.set_device(&device.get_device());
}

void SceneViewerApplication::on_device_destroyed(const DeviceCreatedEvent &)
{
	reflection = nullptr;
	irradiance = nullptr;
	graph.set_device(nullptr);
}

bool SceneViewerApplication::on_key_down(const KeyboardEvent &e)
{
	if (e.get_key_state() != KeyState::Pressed)
		return true;

	switch (e.get_key())
	{
	case Key::O:
		selected_camera->set_ortho(!selected_camera->get_ortho(), 5.0f);
		break;

	case Key::X:
	{
		vec3 pos = selected_camera->get_position();
		auto &scene = scene_loader.get_scene();
		auto node = scene.create_node();
		scene.get_root_node()->add_child(node);

		SceneFormats::LightInfo light;
		light.type = SceneFormats::LightInfo::Type::Spot;
		light.outer_cone = 0.9f;
		light.inner_cone = 0.92f;
		light.color = vec3(10.0f);

		node->transform.translation = pos;
		node->transform.rotation = conjugate(look_at_arbitrary_up(selected_camera->get_front()));

		scene.create_light(light, node.get());
		break;
	}

	case Key::C:
	{
		vec3 pos = selected_camera->get_position();
		auto &scene = scene_loader.get_scene();
		auto node = scene.create_node();
		scene.get_root_node()->add_child(node);

		SceneFormats::LightInfo light;
		light.type = SceneFormats::LightInfo::Type::Point;
		light.color = vec3(10.0f);
		node->transform.translation = pos;

		scene.create_light(light, node.get());
		break;
	}

	case Key::V:
	{
		default_directional_light.direction = -selected_camera->get_front();
		selected_directional = &default_directional_light;
		need_shadow_map_update = true;
		break;
	}

	case Key::B:
	{
		float fovy = selected_camera->get_fovy();
		float aspect = selected_camera->get_aspect();
		float znear = selected_camera->get_znear();
		float zfar = selected_camera->get_zfar();

		RecordedCamera camera;
		camera.direction = selected_camera->get_front();
		camera.position = selected_camera->get_position();
		camera.up = selected_camera->get_up();
		camera.aspect = aspect;
		camera.fovy = fovy;
		camera.znear = znear;
		camera.zfar = zfar;
		recorded_cameras.push_back(camera);
		break;
	}

	case Key::R:
	{
		auto &scene = scene_loader.get_scene();
		scene.remove_entities_with_component<PositionalLightComponent>();
		break;
	}

	case Key::K:
	{
		capture_environment_probe();
		break;
	}

	case Key::Space:
	{
		auto mode = get_wsi().get_present_mode();
		if (mode == PresentMode::SyncToVBlank)
			get_wsi().set_present_mode(PresentMode::UnlockedMaybeTear);
		else
			get_wsi().set_present_mode(PresentMode::SyncToVBlank);
		break;
	}

	case Key::M:
	{
		get_wsi().set_backbuffer_srgb(!get_wsi().get_backbuffer_srgb());
		break;
	}

	default:
		break;
	}

	return true;
}

void SceneViewerApplication::capture_environment_probe()
{
	if (!config.clustered_lights)
		LOGE("Clustered lights are not enabled, lights will not be captured in the environment!\n");

	ImageCreateInfo info = ImageCreateInfo::render_target(512, 512, VK_FORMAT_R16G16B16A16_SFLOAT);
	info.levels = 1;
	info.layers = 6;
	info.usage |= VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
	info.initial_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	auto &device = get_wsi().get_device();

	auto handle = device.create_image(info, nullptr);
	auto cmd = device.request_command_buffer();

	VisibilityList visible;

	for (unsigned face = 0; face < 6; face++)
	{
		ImageViewCreateInfo view_info = {};
		view_info.layers = 1;
		view_info.base_layer = face;
		view_info.format = info.format;
		view_info.levels = 1;
		view_info.image = handle.get();
		auto rt_view = device.create_image_view(view_info);

		mat4 proj, view;
		compute_cube_render_transform(selected_camera->get_position(), face, proj, view, 0.1f, 300.0f);
		context.set_camera(proj, view);

		RenderPassInfo rp = {};
		rp.num_color_attachments = 1;
		rp.color_attachments[0] = rt_view.get();
		rp.store_attachments = 1;
		rp.clear_attachments = 1;
		rp.depth_stencil = &device.get_transient_attachment(512, 512, device.get_default_depth_format(), 0);
		rp.op_flags = RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT;
		rp.clear_depth_stencil.depth = 1.0f;
		rp.clear_depth_stencil.stencil = 0;
		rp.clear_color[0].float32[0] = 0.0f;
		rp.clear_color[0].float32[1] = 0.0f;
		rp.clear_color[0].float32[2] = 0.0f;
		rp.clear_color[0].float32[3] = 1.0f;
		cmd->begin_render_pass(rp);

		auto &scene = scene_loader.get_scene();
		visible.clear();
		scene.gather_visible_opaque_renderables(context.get_visibility_frustum(), visible);
		scene.gather_visible_render_pass_sinks(context.get_render_parameters().camera_position, visible);
		scene.gather_unbounded_renderables(visible);

		auto &forward_renderer = renderer_suite.get_renderer(RendererSuite::Type::ForwardOpaque);
		forward_renderer.set_mesh_renderer_options_from_lighting(lighting);
		forward_renderer.set_mesh_renderer_options(forward_renderer.get_mesh_renderer_options() | config.pcf_flags);

		forward_renderer.begin(queue);
		queue.push_renderables(context, visible);

		Renderer::RendererOptionFlags opt = Renderer::FRONT_FACE_CLOCKWISE_BIT;
		forward_renderer.flush(*cmd, queue, context, opt);

		cmd->end_render_pass();
	}

	cmd->image_barrier(*handle, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
	                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
	device.submit(cmd);
	auto buffer = save_image_to_cpu_buffer(device, *handle, CommandBuffer::Type::Generic);
	save_image_buffer_to_gtx(device, buffer, "cache://environment.gtx");
}

static inline string tagcat(const std::string &a, const std::string &b)
{
	return a + "-" + b;
}

void SceneViewerApplication::add_main_pass_forward(Device &device, const std::string &tag)
{
	AttachmentInfo color, depth;
	depth.format = device.get_default_depth_format();

	bool use_ssao = config.forward_depth_prepass && config.ssao && config.msaa == 1;

	if (use_ssao)
	{
		auto &prepass_depth = graph.add_pass(tagcat("depth-transient", tag), RENDER_GRAPH_QUEUE_GRAPHICS_BIT);
		prepass_depth.set_depth_stencil_output(tagcat("depth-transient", tag), depth);
		auto renderer = Util::make_handle<RenderPassSceneRenderer>();
		RenderPassSceneRenderer::Setup setup = {};
		setup.scene = &scene_loader.get_scene();
		setup.context = &context;
		setup.suite = &renderer_suite;
		setup.flags = SCENE_RENDERER_FORWARD_Z_PREPASS_BIT;
		renderer->init(setup);

		// TODO: Find a good way to let the prepass renderer share renderer with opaque / transparent passes.
		prepass_depth.set_render_pass_interface(std::move(renderer));
		setup_ssao_naive(graph, context, tagcat("ssao-output", tag), tagcat("depth-transient", tag), "");
	}

	bool supports_32bpp =
			device.image_format_is_supported(VK_FORMAT_B10G11R11_UFLOAT_PACK32,
			                                 VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT);

	if (config.hdr_bloom)
		color.format =
		    (config.rt_fp16 || !supports_32bpp) ? VK_FORMAT_R16G16B16A16_SFLOAT : VK_FORMAT_B10G11R11_UFLOAT_PACK32;
	else
		color.format = VK_FORMAT_UNDEFINED; // Swapchain format.

	color.samples = config.msaa;
	depth.samples = config.msaa;

	auto resolved = color;
	resolved.samples = 1;

	auto &lighting_pass = graph.add_pass(tagcat("lighting", tag), RENDER_GRAPH_QUEUE_GRAPHICS_BIT);

	if (color.samples > 1)
	{
		lighting_pass.add_color_output(tagcat("HDR-MS", tag), color);
		lighting_pass.add_resolve_output(tagcat("HDR", tag), resolved);
	}
	else
		lighting_pass.add_color_output(tagcat("HDR", tag), color);

	if (use_ssao)
	{
		ssao_output = &lighting_pass.add_texture_input(tagcat("ssao-output", tag));
		lighting_pass.set_depth_stencil_input(tagcat("depth-transient", tag));
		lighting_pass.add_fake_resource_write_alias(tagcat("depth-transient", tag), tagcat("depth", tag));
	}
	else
	{
		ssao_output = nullptr;
		lighting_pass.set_depth_stencil_output(tagcat("depth", tag), depth);
	}

	auto renderer = Util::make_handle<RenderPassSceneRenderer>();
	RenderPassSceneRenderer::Setup setup = {};
	setup.scene = &scene_loader.get_scene();
	setup.deferred_lights = &deferred_lights;
	setup.context = &context;
	setup.suite = &renderer_suite;
	setup.flags = SCENE_RENDERER_FORWARD_OPAQUE_BIT | SCENE_RENDERER_FORWARD_TRANSPARENT_BIT | config.pcf_flags;
	if (config.forward_depth_prepass && !use_ssao)
		setup.flags |= SCENE_RENDERER_FORWARD_Z_PREPASS_BIT;
	else if (config.forward_depth_prepass)
		setup.flags |= SCENE_RENDERER_FORWARD_Z_EXISTING_PREPASS_BIT;
	renderer->init(setup);

	lighting_pass.set_render_pass_interface(std::move(renderer));

	shadows = nullptr;
	if (config.directional_light_shadows)
		shadows = &lighting_pass.add_texture_input("shadow-main");
	scene_loader.get_scene().add_render_pass_dependencies(graph, lighting_pass);
}

void SceneViewerApplication::add_main_pass_deferred(Device &device, const std::string &tag)
{
	bool supports_32bpp =
	    device.image_format_is_supported(VK_FORMAT_B10G11R11_UFLOAT_PACK32, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT);
	AttachmentInfo emissive, albedo, normal, pbr, depth;
	if (config.hdr_bloom)
		emissive.format =
		    (config.rt_fp16 || !supports_32bpp) ? VK_FORMAT_R16G16B16A16_SFLOAT : VK_FORMAT_B10G11R11_UFLOAT_PACK32;
	else
		emissive.format = VK_FORMAT_UNDEFINED;

	albedo.format = VK_FORMAT_R8G8B8A8_SRGB;
	normal.format = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
	pbr.format = VK_FORMAT_R8G8_UNORM;
	depth.format = device.get_default_depth_stencil_format();

	auto &gbuffer = graph.add_pass(tagcat("gbuffer", tag), RENDER_GRAPH_QUEUE_GRAPHICS_BIT);
	gbuffer.add_color_output(tagcat("emissive", tag), emissive);
	gbuffer.add_color_output(tagcat("albedo", tag), albedo);
	gbuffer.add_color_output(tagcat("normal", tag), normal);
	gbuffer.add_color_output(tagcat("pbr", tag), pbr);
	gbuffer.set_depth_stencil_output(tagcat("depth-transient", tag), depth);

	{
		auto renderer = Util::make_handle<RenderPassSceneRenderer>();
		RenderPassSceneRenderer::Setup setup = {};
		setup.scene = &scene_loader.get_scene();
		setup.deferred_lights = &deferred_lights;
		setup.context = &context;
		setup.suite = &renderer_suite;
		setup.flags = SCENE_RENDERER_DEFERRED_GBUFFER_BIT;
		if (!config.clustered_lights && config.deferred_clustered_stencil_culling)
			setup.flags |= SCENE_RENDERER_DEFERRED_GBUFFER_LIGHT_PREPASS_BIT;
		renderer->init(setup);

		gbuffer.set_render_pass_interface(std::move(renderer));
	}

	if (config.ssao)
	{
		setup_ssao_naive(graph, context, tagcat("ssao-output", tag), tagcat("depth-transient", tag),
		                 tagcat("normal", tag));
	}

	auto &lighting_pass = graph.add_pass(tagcat("lighting", tag), RENDER_GRAPH_QUEUE_GRAPHICS_BIT);
	lighting_pass.add_color_output(tagcat("HDR", tag), emissive, tagcat("emissive", tag));
	lighting_pass.add_attachment_input(tagcat("albedo", tag));
	lighting_pass.add_attachment_input(tagcat("normal", tag));
	lighting_pass.add_attachment_input(tagcat("pbr", tag));
	lighting_pass.add_attachment_input(tagcat("depth-transient", tag));
	lighting_pass.set_depth_stencil_input(tagcat("depth-transient", tag));
	lighting_pass.add_fake_resource_write_alias(tagcat("depth-transient", tag), tagcat("depth", tag));

	{
		auto renderer = Util::make_handle<RenderPassSceneRenderer>();
		RenderPassSceneRenderer::Setup setup = {};
		setup.scene = &scene_loader.get_scene();
		setup.deferred_lights = &deferred_lights;
		setup.context = &context;
		setup.suite = &renderer_suite;
		setup.flags = SCENE_RENDERER_DEFERRED_LIGHTING_BIT | SCENE_RENDERER_FORWARD_TRANSPARENT_BIT | config.pcf_flags;
		if (config.clustered_lights)
			setup.flags |= SCENE_RENDERER_DEFERRED_CLUSTER_BIT;
		renderer->init(setup);

		lighting_pass.set_render_pass_interface(std::move(renderer));
	}

	if (config.ssao)
		ssao_output = &lighting_pass.add_texture_input(tagcat("ssao-output", tag));
	else
		ssao_output = nullptr;

	shadows = nullptr;
	if (config.directional_light_shadows)
		shadows = &lighting_pass.add_texture_input("shadow-main");

	scene_loader.get_scene().add_render_pass_dependencies(graph, gbuffer);
}

void SceneViewerApplication::add_main_pass(Device &device, const std::string &tag)
{
	switch (config.renderer_type)
	{
	case RendererType::GeneralForward:
		add_main_pass_forward(device, tag);
		break;

	case RendererType::GeneralDeferred:
		add_main_pass_deferred(device, tag);
		break;

	default:
		break;
	}
}

void SceneViewerApplication::add_shadow_pass(Device &, const std::string &tag)
{
	AttachmentInfo shadowmap;
	shadowmap.format = VK_FORMAT_D16_UNORM;
	shadowmap.samples = config.directional_light_shadows_vsm ? 4 : 1;
	shadowmap.size_class = SizeClass::Absolute;
	shadowmap.size_x = config.shadow_map_resolution;
	shadowmap.size_y = config.shadow_map_resolution;

	if (config.directional_light_cascaded_shadows)
		shadowmap.layers = NumShadowCascades;

	auto &shadowpass = graph.add_pass(tagcat("shadow", tag), RENDER_GRAPH_QUEUE_GRAPHICS_BIT);

	if (config.directional_light_shadows_vsm)
	{
		auto shadowmap_vsm_color = shadowmap;
		auto shadowmap_vsm_resolved_color = shadowmap;
		shadowmap_vsm_color.format = VK_FORMAT_R32G32_SFLOAT;
		shadowmap_vsm_color.samples = 4;
		shadowmap_vsm_resolved_color.format = VK_FORMAT_R32G32_SFLOAT;
		shadowmap_vsm_resolved_color.samples = 1;

		auto shadowmap_vsm_half = shadowmap_vsm_resolved_color;
		shadowmap_vsm_half.size_x *= 0.5f;
		shadowmap_vsm_half.size_y *= 0.5f;

		shadowpass.set_depth_stencil_output(tagcat("shadow-depth", tag), shadowmap);
		shadowpass.add_color_output(tagcat("shadow-msaa", tag), shadowmap_vsm_color);
		shadowpass.add_resolve_output(tagcat("shadow-raw", tag), shadowmap_vsm_resolved_color);

		auto &down_pass = graph.add_pass(tagcat("shadow-down", tag), RENDER_GRAPH_QUEUE_GRAPHICS_BIT);
		down_pass.add_color_output(tagcat("shadow-down", tag), shadowmap_vsm_half);
		auto &down_pass_res = down_pass.add_texture_input(tagcat("shadow-raw", tag));

		auto &up_pass = graph.add_pass(tagcat("shadow-up", tag), RENDER_GRAPH_QUEUE_GRAPHICS_BIT);
		up_pass.add_color_output(tagcat("shadow", tag), shadowmap_vsm_resolved_color);
		auto &up_pass_res = up_pass.add_texture_input(tagcat("shadow-down", tag));

		down_pass.set_build_render_pass([&, layered = shadowmap.layers > 1](CommandBuffer &cmd) {
			auto &input = graph.get_physical_texture_resource(down_pass_res);
			vec2 inv_size(1.0f / input.get_image().get_create_info().width,
			              1.0f / input.get_image().get_create_info().height);
			cmd.push_constants(&inv_size, 0, sizeof(inv_size));
			cmd.set_texture(0, 0, input, StockSampler::LinearClamp);
			CommandBufferUtil::draw_fullscreen_quad(cmd, "builtin://shaders/quad.vert",
			                                        "builtin://shaders/post/vsm_down_blur.frag",
			                                        {{ "LAYERED", layered ? 1 : 0 }});
		});

		up_pass.set_build_render_pass([&, layered = shadowmap.layers > 1](CommandBuffer &cmd) {
			auto &input = graph.get_physical_texture_resource(up_pass_res);
			vec2 inv_size(1.0f / input.get_image().get_create_info().width,
			              1.0f / input.get_image().get_create_info().height);
			cmd.set_texture(0, 0, input, StockSampler::LinearClamp);
			cmd.push_constants(&inv_size, 0, sizeof(inv_size));
			CommandBufferUtil::draw_fullscreen_quad(cmd, "builtin://shaders/quad.vert",
			                                        "builtin://shaders/post/vsm_up_blur.frag",
			                                        {{ "LAYERED", layered ? 1 : 0 }});
		});
	}
	else
	{
		shadowpass.set_depth_stencil_output(tagcat("shadow", tag), shadowmap);
	}

	Util::IntrusivePtr<RenderPassSceneRenderer> handle;
	RenderPassSceneRenderer::Setup setup = {};
	setup.scene = &scene_loader.get_scene();
	setup.suite = &renderer_suite;
	setup.flags = SCENE_RENDERER_DEPTH_BIT;
	if (config.directional_light_shadows_vsm)
		setup.flags |= SCENE_RENDERER_SHADOW_VSM_BIT;

	setup.context = &depth_context;
	setup.flags |= SCENE_RENDERER_DEPTH_DYNAMIC_BIT;

	handle = Util::make_handle<RenderPassSceneRenderer>();
	handle->init(setup);

	VkClearColorValue value = {};
	value.float32[0] = 1.0f;
	value.float32[1] = 1.0f;
	handle->set_clear_color(value);
	shadowpass.set_render_pass_interface(std::move(handle));
}

void SceneViewerApplication::on_swapchain_changed(const SwapchainParameterEvent &swap)
{
	auto physical_buffers = graph.consume_physical_buffers();

	shadows = nullptr;
	ssao_output = nullptr;

	graph.reset();
	graph.set_device(&swap.get_device());

	ResourceDimensions dim;
	dim.width = swap.get_width();
	dim.height = swap.get_height();
	dim.format = swap.get_format();
	dim.transform = swap.get_prerotate();
	graph.set_backbuffer_dimensions(dim);

	const char *backbuffer_source = getenv("GRANITE_SURFACE");
	const char *ui_source = backbuffer_source ? backbuffer_source : (config.hdr_bloom ? "tonemapped" : "HDR-main");

	scene_loader.get_scene().add_render_passes(graph);

	if (config.directional_light_shadows)
		add_shadow_pass(swap.get_device(), "main");

	add_main_pass(swap.get_device(), "main");

	if (config.hdr_bloom)
	{
		bool resolved = setup_before_post_chain_antialiasing(config.postaa_type, graph, jitter, "HDR-main",
		                                                     "depth-main", "HDR-resolved");

		HDROptions opts;
		opts.dynamic_exposure = config.hdr_bloom_dynamic_exposure;

		if (ImplementationQuirks::get().use_async_compute_post)
			setup_hdr_postprocess_compute(graph, resolved ? "HDR-resolved" : "HDR-main", "tonemapped", opts);
		else
			setup_hdr_postprocess(graph, resolved ? "HDR-resolved" : "HDR-main", "tonemapped", opts);
	}

	if (setup_after_post_chain_antialiasing(config.postaa_type, graph, jitter, ui_source, "depth-main",
	                                        "post-aa-output"))
	{
		ui_source = "post-aa-output";
	}

	if (config.show_ui)
	{
		auto &ui = graph.add_pass("ui", config.hdr_bloom || config.postaa_type != PostAAType::None ?
		                                    RenderGraph::get_default_post_graphics_queue() :
		                                    RENDER_GRAPH_QUEUE_GRAPHICS_BIT);

		AttachmentInfo ui_info;
		ui_info.supports_prerotate = true;
		ui.add_color_output("ui-output", ui_info, ui_source);
		graph.set_backbuffer_source("ui-output");

		ui.set_get_clear_color([](unsigned, VkClearColorValue *value) {
			memset(value, 0, sizeof(*value));
			return true;
		});

		ui.set_build_render_pass([this](CommandBuffer &cmd) { render_ui(cmd); });
	}
	else
		graph.set_backbuffer_source(ui_source);

	graph.bake();
	//graph.log();
	graph.install_physical_buffers(move(physical_buffers));

	need_shadow_map_update = true;
}

void SceneViewerApplication::on_swapchain_destroyed(const SwapchainParameterEvent &)
{
}

void SceneViewerApplication::update_shadow_scene_aabb()
{
	// Get the scene AABB for shadow casters.
	auto &scene = scene_loader.get_scene();
	auto &shadow_casters =
	    scene.get_entity_pool()
	        .get_component_group<RenderInfoComponent, RenderableComponent, CastsStaticShadowComponent>();
	AABB aabb(vec3(FLT_MAX), vec3(-FLT_MAX));
	for (auto &caster : shadow_casters)
		aabb.expand(get_component<RenderInfoComponent>(caster)->world_aabb);
	shadow_scene_aabb = aabb;
}

void SceneViewerApplication::setup_shadow_map()
{
	mat4 view = mat4_cast(look_at(-selected_directional->direction, vec3(0.0f, 1.0f, 0.0f)));
	AABB ortho_range_depth = shadow_scene_aabb.transform(view); // Just need this to determine Zmin/Zmax.

	// Project the scene AABB into the light and find our ortho ranges.
	// This will serve as the culling bounding box.
	// TODO: Make configurable.
	constexpr float FIRST_SLICE_CUTOFF = 10.0f;
	constexpr float BEGIN_LERP_FRACT = 0.8f;

	const float cascade_log_bias = 1.0f - muglm::log2(FIRST_SLICE_CUTOFF);
	const auto compute_z = [&](float slice) -> float {
		float slice_z = FIRST_SLICE_CUTOFF * muglm::exp2(slice - 1.0f);
		return slice_z;
	};

	lighting.shadow.cascade_log_bias = cascade_log_bias;

	if (config.directional_light_cascaded_shadows)
	{
		mat4 cascade_transforms[NumShadowCascades];
		AABB combined_aabb(vec3(FLT_MAX), vec3(-FLT_MAX));

		for (int i = 0; i < NumShadowCascades; i++)
		{
			float cascade_cutoffs_lo;
			float cascade_cutoffs_hi = compute_z(float(i + 1));
			if (i == 0)
				cascade_cutoffs_lo = 0.0001f;
			else
				cascade_cutoffs_lo = compute_z(float(i - 1) + BEGIN_LERP_FRACT);

			auto near_camera = *selected_camera;
			near_camera.set_depth_range(cascade_cutoffs_lo, cascade_cutoffs_hi);
			vec4 sphere = Frustum::get_bounding_sphere(inverse(near_camera.get_projection()), inverse(near_camera.get_view()));
			vec2 center_xy = (view * vec4(sphere.xyz(), 1.0f)).xy();
			sphere.w *= 1.01f;

			vec2 texel_size = vec2(2.0f * sphere.w) * vec2(1.0f / lighting.shadows->get_image().get_create_info().width,
			                                               1.0f / lighting.shadows->get_image().get_create_info().height);

			// Snap to texel grid.
			center_xy = round(center_xy / texel_size) * texel_size;

			AABB ortho_range = AABB(vec3(center_xy - vec2(sphere.w), ortho_range_depth.get_minimum().z),
			                        vec3(center_xy + vec2(sphere.w), ortho_range_depth.get_maximum().z));

			combined_aabb.expand(ortho_range);

			mat4 proj = ortho(ortho_range);
			cascade_transforms[i] = proj * view;
			lighting.shadow.transforms[i] =
					translate(vec3(0.5f, 0.5f, 0.0f)) *
					scale(vec3(0.5f, 0.5f, 1.0f)) *
					cascade_transforms[i];
		}

		depth_context.set_shadow_cascades(cascade_transforms);

		mat4 proj = ortho(combined_aabb);
		depth_context.set_camera(proj, view);
	}
	else
	{
		mat4 proj = ortho(ortho_range_depth);
		depth_context.set_camera(proj, view);
		lighting.shadow.transforms[0] =
				translate(vec3(0.5f, 0.5f, 0.0f)) *
				scale(vec3(0.5f, 0.5f, 1.0f)) *
				proj * view;
	}
}

void SceneViewerApplication::update_scene(TaskComposer &composer, double frame_time, double elapsed_time)
{
	last_frame_times[last_frame_index++ & FrameWindowSizeMask] = float(frame_time);
	auto &scene = scene_loader.get_scene();

	animation_system->animate(frame_time, elapsed_time);

	{
		TaskComposer update_composer(composer.get_thread_group());
		scene.update_transform_tree(update_composer);
		Threaded::scene_update_cached_transforms(scene, update_composer, 64);
		update_composer.get_outgoing_task()->wait();
	}

	jitter.step(selected_camera->get_projection(), selected_camera->get_view());

	if (reflection)
		lighting.environment_radiance = &reflection->get_image()->get_view();
	if (irradiance)
		lighting.environment_irradiance = &irradiance->get_image()->get_view();
	lighting.environment.intensity = skydome_intensity;
	lighting.refraction.falloff = vec3(1.0f / 1.5f, 1.0f / 2.5f, 1.0f / 5.0f);

	renderer_suite.update_mesh_rendering_options(context, renderer_suite_config);
	context.set_camera(*selected_camera);
	scene.set_render_pass_data(&renderer_suite, &context);

	lighting.directional.direction = selected_directional->direction;
	lighting.directional.color = selected_directional->color;

	scene.refresh_per_frame(context, composer);
}

void SceneViewerApplication::render_ui(CommandBuffer &cmd)
{
	auto &device = cmd.get_device();
	flat_renderer.begin();

	unsigned count = std::min<unsigned>(last_frame_index, FrameWindowSize);
	float total_time = 0.0f;
	float min_time = FLT_MAX;
	float max_time = 0.0f;
	for (unsigned i = 0; i < count; i++)
	{
		total_time += last_frame_times[i];
		min_time = std::min(min_time, last_frame_times[i]);
		max_time = std::max(max_time, last_frame_times[i]);
	}

	char avg_text[64];
	sprintf(avg_text, "Frame: %10.3f ms", (total_time / count) * 1000.0f);

	char min_text[64];
	sprintf(min_text, "Min: %10.3f ms", min_time * 1000.0f);

	char max_text[64];
	sprintf(max_text, "Max: %10.3f ms", max_time * 1000.0f);

	char latency_text[64];
	sprintf(latency_text, "Latency: %10.3f ms", get_wsi().get_estimated_video_latency() * 1e3f);

	vec3 offset(5.0f, 5.0f, 0.0f);
	vec2 size(cmd.get_viewport().width - 10.0f, cmd.get_viewport().height - 10.0f);
	vec4 color(1.0f, 1.0f, 0.0f, 1.0f);
	Font::Alignment alignment = Font::Alignment::TopRight;

	flat_renderer.render_text(Global::ui_manager()->get_font(UI::FontSize::Large), avg_text, offset, size, color,
	                          alignment, 1.0f);
	flat_renderer.render_text(Global::ui_manager()->get_font(UI::FontSize::Large), min_text,
	                          offset + vec3(0.0f, 20.0f, 0.0f), size - vec2(0.0f, 20.0f), color, alignment, 1.0f);
	flat_renderer.render_text(Global::ui_manager()->get_font(UI::FontSize::Large), max_text,
	                          offset + vec3(0.0f, 40.0f, 0.0f), size - vec2(0.0f, 40.0f), color, alignment, 1.0f);
	flat_renderer.render_text(Global::ui_manager()->get_font(UI::FontSize::Large), latency_text,
	                          offset + vec3(0.0f, 60.0f, 0.0f), size - vec2(0.0f, 60.0f), color, alignment, 1.0f);

	HeapBudget budgets[VK_MAX_MEMORY_HEAPS];
	device.get_memory_budget(budgets);
	for (uint32_t i = 0; i < device.get_memory_properties().memoryHeapCount; i++)
	{
		char heap_text[256];
		sprintf(heap_text, "Heap #%u: (%.1f MiB / %.1f MiB) [%.1f / %.1f]", i,
		        double(budgets[i].device_usage) / double(1024 * 1024),
		        double(budgets[i].budget_size) / double(1024 * 1024),
		        double(budgets[i].tracked_usage) / double(1024 * 1024),
		        double(budgets[i].max_size) / double(1024 * 1024));
		flat_renderer.render_text(Global::ui_manager()->get_font(UI::FontSize::Normal), heap_text,
		                          offset + vec3(0.0f, 90.0f + 15.0f * float(i), 0.0f),
		                          size - vec2(0.0f, 90.0f + 15.0f * float(i)),
		                          color, alignment, 1.0f);
	}

	flat_renderer.flush(cmd, vec3(0.0f), vec3(cmd.get_viewport().width, cmd.get_viewport().height, 1.0f));
}

void SceneViewerApplication::render_scene(TaskComposer &composer)
{
	auto &wsi = get_wsi();
	auto &device = wsi.get_device();
	auto &scene = scene_loader.get_scene();

	graph.setup_attachments(device, &device.get_swapchain_view());
	lighting.shadows = graph.maybe_get_physical_texture_resource(shadows);
	lighting.ambient_occlusion = graph.maybe_get_physical_texture_resource(ssao_output);

	if (lighting.shadows)
	{
		if (need_shadow_map_update)
			update_shadow_scene_aabb();
		setup_shadow_map();
	}

	scene.bind_render_graph_resources(graph);
	graph.enqueue_render_passes(device, composer);
	need_shadow_map_update = false;
}

void SceneViewerApplication::render_frame(double frame_time, double elapsed_time)
{
	auto *file = Global::thread_group()->get_timeline_trace_file();
	TaskComposer composer(*Global::thread_group());

	Util::TimelineTraceFile::Event *e = nullptr;

	if (file)
		e = file->begin_event("update-scene-enqueue");
	update_scene(composer, frame_time, elapsed_time);
	if (e)
		file->end_event(e);

	if (file)
		e = file->begin_event("render-scene-enqueue");
	render_scene(composer);
	if (e)
		file->end_event(e);

	if (file)
		e = file->begin_event("render-frame-wait");
	composer.get_outgoing_task()->wait();
	if (e)
		file->end_event(e);

	renderer_suite.promote_read_write_cache_to_read_only();
	get_wsi().get_device().promote_read_write_caches_to_read_only();
}

} // namespace Granite
