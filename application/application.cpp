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

#include "application.hpp"
#include <stdexcept>
#include "sprite.hpp"
#include "horizontal_packing.hpp"
#include "image_widget.hpp"
#include "label.hpp"
#include "post/hdr.hpp"

#define RAPIDJSON_ASSERT(x) do { if (!(x)) throw "JSON error"; } while(0)
#include "rapidjson/document.h"
#include "light_export.hpp"

using namespace std;
using namespace Vulkan;

namespace Granite
{
Application::Application()
{
	EventManager::get_global();
	Filesystem::get();
}

bool Application::init_wsi(std::unique_ptr<WSIPlatform> new_platform)
{
	platform = move(new_platform);
	wsi.set_platform(platform.get());
	if (!platform->has_external_swapchain() && !wsi.init())
		return false;

	return true;
}

static vec3 light_direction()
{
	return normalize(vec3(0.5f, 1.2f, 0.8f));
}

void SceneViewerApplication::read_config(const std::string &path)
{
	string json;
	if (!Filesystem::get().read_file_to_string(path, json))
	{
		LOGE("Failed to read config file. Assuming defaults.\n");
		return;
	}

	rapidjson::Document doc;
	doc.Parse(json);

	if (doc.HasMember("renderer"))
	{
		auto *renderer =  doc["renderer"].GetString();
		if (strcmp(renderer, "forward") == 0)
			config.renderer_type = RendererType::GeneralForward;
		else if (strcmp(renderer, "deferred") == 0)
			config.renderer_type = RendererType::GeneralDeferred;
		else
			throw invalid_argument("Invalid renderer option.");
	}

	if (doc.HasMember("msaa"))
		config.msaa = doc["msaa"].GetUint();

	if (doc.HasMember("directionalLightShadows"))
		config.directional_light_shadows = doc["directionalLightShadows"].GetBool();
	if (doc.HasMember("directionalLightShadowsCascaded"))
		config.directional_light_cascaded_shadows = doc["directionalLightShadowsCascaded"].GetBool();
	if (doc.HasMember("clusteredLights"))
		config.clustered_lights = doc["clusteredLights"].GetBool();
	if (doc.HasMember("clusteredLightsShadows"))
		config.clustered_lights_shadows = doc["clusteredLightsShadows"].GetBool();
	if (doc.HasMember("hdrBloom"))
		config.hdr_bloom = doc["hdrBloom"].GetBool();

	if (doc.HasMember("shadowMapResolutionMain"))
		config.shadow_map_resolution_main = doc["shadowMapResolutionMain"].GetFloat();
	if (doc.HasMember("shadowMapResolutionNear"))
		config.shadow_map_resolution_near = doc["shadowMapResolutionNear"].GetFloat();

	if (doc.HasMember("cameraIndex"))
		config.camera_index = doc["cameraIndex"].GetInt();

	if (doc.HasMember("renderTargetFp16"))
		config.rt_fp16 = doc["renderTargetFp16"].GetBool();

	if (doc.HasMember("timestamps"))
		config.timestamps = doc["timestamps"].GetBool();

	if (doc.HasMember("rescaleScene"))
		config.rescale_scene = doc["rescaleScene"].GetBool();

	if (doc.HasMember("directionalLightCascadeCutoff"))
		config.cascade_cutoff_distance = doc["directionalLightCascadeCutoff"].GetFloat();

	if (doc.HasMember("directionalLightShadowsForceUpdate"))
		config.force_shadow_map_update = doc["directionalLightShadowsForceUpdate"].GetBool();
}

SceneViewerApplication::SceneViewerApplication(const std::string &path, const std::string &config_path)
	: forward_renderer(RendererType::GeneralForward),
      deferred_renderer(RendererType::GeneralDeferred),
      depth_renderer(RendererType::DepthOnly)
{
	if (!config_path.empty())
		read_config(config_path);

	scene_loader.load_scene(path);
	animation_system = scene_loader.consume_animation_system();
	context.set_lighting_parameters(&lighting);
	cam.set_depth_range(0.1f, 1000.0f);

	auto &ibl = scene_loader.get_scene().get_entity_pool().get_component_group<IBLComponent>();
	if (!ibl.empty())
	{
		auto *ibl_component = get<0>(ibl.front());
		skydome_reflection = ibl_component->reflection_path;
		skydome_irradiance = ibl_component->irradiance_path;
		skydome_intensity = ibl_component->intensity;
	}

	auto &skybox = scene_loader.get_scene().get_entity_pool().get_component_group<SkyboxComponent>();
	for (auto &box : skybox)
		get<0>(box)->skybox->set_color_mod(vec3(skydome_intensity));

	// Create a dummy background if there isn't any background.
	if (scene_loader.get_scene().get_entity_pool().get_component_group<UnboundedComponent>().empty())
	{
		auto cylinder = Util::make_abstract_handle<AbstractRenderable, SkyCylinder>("builtin://textures/background.png");
		static_cast<SkyCylinder *>(cylinder.get())->set_xz_scale(8.0f / pi<float>());
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
				selected_camera = &get<0>(scene_cameras[config.camera_index])->camera;
			else
				LOGE("Camera index is out of bounds, using normal camera.");
		}
	}

	// Pick a directional light.
	default_directional_light.color = vec3(6.0f, 5.5f, 4.5f);
	default_directional_light.direction = light_direction();
	auto &dir_lights = scene_loader.get_scene().get_entity_pool().get_component_group<DirectionalLightComponent>();
	if (!dir_lights.empty())
		selected_directional = get<0>(dir_lights.front());
	else
		selected_directional = &default_directional_light;

	if (config.clustered_lights_shadows || config.clustered_lights)
	{
		cluster.reset(new LightClusterer);
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
			cluster->set_base_renderer(&forward_renderer, &deferred_renderer, &depth_renderer);
			cluster->set_base_render_context(&context);
		}

		cluster->set_enable_shadows(config.clustered_lights_shadows);
		cluster->set_enable_clustering(config.clustered_lights);
		cluster->set_force_update_shadows(config.force_shadow_map_update);
	}

	context.set_camera(*selected_camera);

	graph.enable_timestamps(config.timestamps);

	if (config.rescale_scene)
		rescale_scene(10.0f);

	EVENT_MANAGER_REGISTER_LATCH(SceneViewerApplication, on_swapchain_changed, on_swapchain_destroyed, SwapchainParameterEvent);
	EVENT_MANAGER_REGISTER_LATCH(SceneViewerApplication, on_device_created, on_device_destroyed, DeviceCreatedEvent);
	EVENT_MANAGER_REGISTER(SceneViewerApplication, on_key_down, KeyboardEvent);
}

void SceneViewerApplication::export_lights()
{
	auto lights = export_lights_to_json(lighting.directional, scene_loader.get_scene());
	if (!Filesystem::get().write_string_to_file("cache://lights.json", lights))
		LOGE("Failed to export light data.\n");
}

void SceneViewerApplication::export_cameras()
{
	auto cameras = export_cameras_to_json(recorded_cameras);
	if (!Filesystem::get().write_string_to_file("cache://cameras.json", cameras))
		LOGE("Failed to export camera data.\n");
}

SceneViewerApplication::~SceneViewerApplication()
{
	graph.report_timestamps();
	export_lights();
	export_cameras();
}

void SceneViewerApplication::loop_animations()
{
}

void SceneViewerApplication::rescale_scene(float radius)
{
	scene_loader.get_scene().update_cached_transforms();

	AABB aabb(vec3(FLT_MAX), vec3(-FLT_MAX));
	auto &objects = scene_loader.get_scene().get_entity_pool().get_component_group<CachedSpatialTransformComponent, RenderableComponent>();
	for (auto &caster : objects)
		aabb.expand(get<0>(caster)->world_aabb);

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
		light.quadratic_falloff = 0.01f;
		light.constant_falloff = 0.0f;
		light.color = vec3(1.0f);

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
		light.quadratic_falloff = 0.01f;
		light.constant_falloff = 0.0f;
		light.color = vec3(1.0f);
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

	default:
		break;
	}

	return true;
}

void SceneViewerApplication::render_main_pass(Vulkan::CommandBuffer &cmd, const mat4 &proj, const mat4 &view)
{
	auto &scene = scene_loader.get_scene();
	context.set_camera(proj, view);
	visible.clear();
	scene.gather_visible_opaque_renderables(context.get_visibility_frustum(), visible);
	scene.gather_background_renderables(visible);
	scene.gather_visible_render_pass_sinks(context.get_render_parameters().camera_position, visible);

	if (config.renderer_type == RendererType::GeneralForward)
	{
		forward_renderer.set_mesh_renderer_options_from_lighting(lighting);
		forward_renderer.begin();
		forward_renderer.push_renderables(context, visible);
		forward_renderer.flush(cmd, context);
	}
	else if (config.renderer_type == RendererType::GeneralDeferred)
	{
		deferred_renderer.begin();
		deferred_renderer.push_renderables(context, visible);
		deferred_renderer.flush(cmd, context);
	}
}

void SceneViewerApplication::render_transparent_objects(Vulkan::CommandBuffer &cmd, const mat4 &proj, const mat4 &view)
{
	auto &scene = scene_loader.get_scene();
	context.set_camera(proj, view);
	visible.clear();
	scene.gather_visible_transparent_renderables(context.get_visibility_frustum(), visible);
	forward_renderer.set_mesh_renderer_options_from_lighting(lighting);
	forward_renderer.begin();
	forward_renderer.push_renderables(context, visible);
	forward_renderer.flush(cmd, context);
}

void SceneViewerApplication::render_positional_lights(Vulkan::CommandBuffer &cmd, const mat4 &proj, const mat4 &view)
{
	auto &scene = scene_loader.get_scene();
	context.set_camera(proj, view);
	visible.clear();
	scene.gather_visible_positional_lights(context.get_visibility_frustum(), visible);
	deferred_renderer.begin();
	deferred_renderer.push_renderables(context, visible);
	deferred_renderer.flush(cmd, context);
}

static inline string tagcat(const std::string &a, const std::string &b)
{
	return a + "-" + b;
}

void SceneViewerApplication::add_main_pass_forward(Vulkan::Device &device, const std::string &tag)
{
	AttachmentInfo color, depth;

	if (config.hdr_bloom)
		color.format = config.rt_fp16 ? VK_FORMAT_R16G16B16A16_SFLOAT : VK_FORMAT_B10G11R11_UFLOAT_PACK32;
	else
		color.format = VK_FORMAT_UNDEFINED; // Swapchain format.

	depth.format = device.get_default_depth_format();
	color.samples = config.msaa;
	depth.samples = config.msaa;

	auto resolved = color;
	resolved.samples = 1;

	auto &lighting = graph.add_pass(tagcat("lighting", tag), VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);

	if (color.samples > 1)
	{
		lighting.add_color_output(tagcat("HDR-MS", tag), color);
		lighting.add_resolve_output(tagcat("HDR", tag), resolved);
	}
	else
		lighting.add_color_output(tagcat("HDR", tag), color);

	lighting.set_depth_stencil_output(tagcat("depth", tag), depth);

	lighting.set_get_clear_depth_stencil([](VkClearDepthStencilValue *value) -> bool {
		if (value)
		{
			value->depth = 1.0f;
			value->stencil = 0;
		}
		return true;
	});

	lighting.set_get_clear_color([](unsigned, VkClearColorValue *value) -> bool {
		if (value)
			memset(value, 0, sizeof(*value));
		return true;
	});

	lighting.set_build_render_pass([this](Vulkan::CommandBuffer &cmd) {
		render_main_pass(cmd, selected_camera->get_projection(), selected_camera->get_view());
		render_transparent_objects(cmd, selected_camera->get_projection(), selected_camera->get_view());
	});

	if (config.directional_light_shadows)
	{
		lighting.add_texture_input("shadow-main");
		if (config.directional_light_cascaded_shadows)
			lighting.add_texture_input("shadow-near");
	}
	scene_loader.get_scene().add_render_pass_dependencies(graph, lighting);
}

void SceneViewerApplication::add_main_pass_deferred(Vulkan::Device &device, const std::string &tag)
{
	AttachmentInfo emissive, albedo, normal, pbr, depth;
	if (config.hdr_bloom)
		emissive.format = config.rt_fp16 ? VK_FORMAT_R16G16B16A16_SFLOAT : VK_FORMAT_B10G11R11_UFLOAT_PACK32;
	else
		emissive.format = VK_FORMAT_UNDEFINED;

	albedo.format = VK_FORMAT_R8G8B8A8_SRGB;
	normal.format = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
	pbr.format = VK_FORMAT_R8G8_UNORM;
	depth.format = device.get_default_depth_stencil_format();

	auto &gbuffer = graph.add_pass(tagcat("gbuffer", tag), VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
	gbuffer.add_color_output(tagcat("emissive", tag), emissive);
	gbuffer.add_color_output(tagcat("albedo", tag), albedo);
	gbuffer.add_color_output(tagcat("normal", tag), normal);
	gbuffer.add_color_output(tagcat("pbr", tag), pbr);
	gbuffer.set_depth_stencil_output(tagcat("depth", tag), depth);
	gbuffer.set_build_render_pass([this](Vulkan::CommandBuffer &cmd) {
		render_main_pass(cmd, selected_camera->get_projection(), selected_camera->get_view());
	});

	gbuffer.set_get_clear_depth_stencil([](VkClearDepthStencilValue *value) -> bool {
		if (value)
		{
			value->depth = 1.0f;
			value->stencil = 0;
		}
		return true;
	});

	gbuffer.set_get_clear_color([](unsigned, VkClearColorValue *value) -> bool {
		if (value)
		{
			value->float32[0] = 0.0f;
			value->float32[1] = 0.0f;
			value->float32[2] = 0.0f;
			value->float32[3] = 0.0f;
		}
		return true;
	});

	auto &lighting = graph.add_pass(tagcat("lighting", tag), VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
	lighting.add_color_output(tagcat("HDR-lighting", tag), emissive, tagcat("emissive", tag));
	lighting.add_attachment_input(tagcat("albedo", tag));
	lighting.add_attachment_input(tagcat("normal", tag));
	lighting.add_attachment_input(tagcat("pbr", tag));
	lighting.add_attachment_input(tagcat("depth", tag));
	lighting.set_depth_stencil_input(tagcat("depth", tag));

	if (config.directional_light_shadows)
	{
		lighting.add_texture_input("shadow-main");
		if (config.directional_light_cascaded_shadows)
			lighting.add_texture_input("shadow-near");
	}

	scene_loader.get_scene().add_render_pass_dependencies(graph, gbuffer);

	lighting.set_build_render_pass([this](Vulkan::CommandBuffer &cmd) {
		if (!config.clustered_lights)
			render_positional_lights(cmd, selected_camera->get_projection(), selected_camera->get_view());
		DeferredLightRenderer::render_light(cmd, context);
	});

	auto &transparent = graph.add_pass(tagcat("transparent", tag), VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
	transparent.add_color_output(tagcat("HDR", tag), emissive, tagcat("HDR-lighting", tag));
	transparent.set_depth_stencil_input(tagcat("depth", tag));
	transparent.set_build_render_pass([this](Vulkan::CommandBuffer &cmd) {
		render_transparent_objects(cmd, selected_camera->get_projection(), selected_camera->get_view());
	});
}

void SceneViewerApplication::add_main_pass(Vulkan::Device &device, const std::string &tag)
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

void SceneViewerApplication::add_shadow_pass(Vulkan::Device &, const std::string &tag, DepthPassType type)
{
	AttachmentInfo shadowmap;
	shadowmap.format = VK_FORMAT_D16_UNORM;
	shadowmap.samples = 1;
	shadowmap.size_class = SizeClass::Absolute;

	if (type == DepthPassType::Main)
	{
		shadowmap.size_x = config.shadow_map_resolution_main;
		shadowmap.size_y = config.shadow_map_resolution_main;
	}
	else
	{
		shadowmap.size_x = config.shadow_map_resolution_near;
		shadowmap.size_y = config.shadow_map_resolution_near;
	}

	auto &shadowpass = graph.add_pass(tagcat("shadow", tag), VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
	shadowpass.set_depth_stencil_output(tagcat("shadow", tag), shadowmap);
	shadowpass.set_build_render_pass([this, type](Vulkan::CommandBuffer &cmd) {
		if (type == DepthPassType::Main)
			render_shadow_map_far(cmd);
		else
			render_shadow_map_near(cmd);
	});

	shadowpass.set_get_clear_depth_stencil([](VkClearDepthStencilValue *value) -> bool {
		if (value)
		{
			value->depth = 1.0f;
			value->stencil = 0;
		}
		return true;
	});

	shadowpass.set_need_render_pass([this, type]() {
		return type == DepthPassType::Main ? need_shadow_map_update : true;
	});
}

void SceneViewerApplication::on_swapchain_changed(const SwapchainParameterEvent &swap)
{
	auto physical_buffers = graph.consume_physical_buffers();
	graph.reset();
	graph.set_device(&swap.get_device());

	ResourceDimensions dim;
	dim.width = swap.get_width();
	dim.height = swap.get_height();
	dim.format = swap.get_format();
	graph.set_backbuffer_dimensions(dim);

	const char *backbuffer_source = getenv("GRANITE_SURFACE");
	graph.set_backbuffer_source(backbuffer_source ? backbuffer_source : (config.hdr_bloom ? "tonemapped" : "HDR-main"));

	scene_loader.get_scene().add_render_passes(graph);

	if (config.directional_light_shadows)
	{
		add_shadow_pass(swap.get_device(), "main", DepthPassType::Main);
		if (config.directional_light_cascaded_shadows)
			add_shadow_pass(swap.get_device(), "near", DepthPassType::Near);
	}

	add_main_pass(swap.get_device(), "main");

	if (config.hdr_bloom)
		setup_hdr_postprocess(graph, "HDR-main", "tonemapped");

	graph.bake();
	graph.log();
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
			scene.get_entity_pool().get_component_group<CachedSpatialTransformComponent, RenderableComponent, CastsStaticShadowComponent>();
	AABB aabb(vec3(FLT_MAX), vec3(-FLT_MAX));
	for (auto &caster : shadow_casters)
		aabb.expand(get<0>(caster)->world_aabb);
	shadow_scene_aabb = aabb;
}

void SceneViewerApplication::update_shadow_map()
{
	auto &scene = scene_loader.get_scene();
	depth_visible.clear();

	mat4 view = mat4_cast(look_at(-selected_directional->direction, vec3(0.0f, 1.0f, 0.0f)));

	// Project the scene AABB into the light and find our ortho ranges.
	AABB ortho_range = shadow_scene_aabb.transform(view);
	mat4 proj = ortho(ortho_range);

	// Standard scale/bias.
	lighting.shadow.far_transform = glm::translate(vec3(0.5f, 0.5f, 0.0f)) * glm::scale(vec3(0.5f, 0.5f, 1.0f)) * proj * view;
	depth_context.set_camera(proj, view);

	depth_renderer.begin();
	scene.gather_visible_static_shadow_renderables(depth_context.get_visibility_frustum(), depth_visible);
	depth_renderer.push_depth_renderables(depth_context, depth_visible);
}

void SceneViewerApplication::render_shadow_map_far(Vulkan::CommandBuffer &cmd)
{
	update_shadow_map();
	depth_renderer.flush(cmd, depth_context);
}

void SceneViewerApplication::render_shadow_map_near(Vulkan::CommandBuffer &cmd)
{
	auto &scene = scene_loader.get_scene();
	depth_visible.clear();
	mat4 view = mat4_cast(look_at(-selected_directional->direction, vec3(0.0f, 1.0f, 0.0f)));
	AABB ortho_range_depth = shadow_scene_aabb.transform(view); // Just need this to determine Zmin/Zmax.

	auto near_camera = *selected_camera;
	near_camera.set_depth_range(near_camera.get_znear(), config.cascade_cutoff_distance);
	vec4 sphere = Frustum::get_bounding_sphere(inverse(near_camera.get_projection()), inverse(near_camera.get_view()));
	vec2 center_xy = (view * vec4(sphere.xyz(), 1.0f)).xy();
	sphere.w *= 1.01f;

	vec2 texel_size = vec2(2.0f * sphere.w) * vec2(1.0f / lighting.shadow_near->get_image().get_create_info().width,
	                                               1.0f / lighting.shadow_near->get_image().get_create_info().height);

	// Snap to texel grid.
	center_xy = round(center_xy / texel_size) * texel_size;

	AABB ortho_range = AABB(vec3(center_xy - vec2(sphere.w), ortho_range_depth.get_minimum().z),
	                        vec3(center_xy + vec2(sphere.w), ortho_range_depth.get_maximum().z));

	mat4 proj = ortho(ortho_range);
	lighting.shadow.near_transform = glm::translate(vec3(0.5f, 0.5f, 0.0f)) * glm::scale(vec3(0.5f, 0.5f, 1.0f)) * proj * view;
	depth_context.set_camera(proj, view);
	depth_renderer.begin();
	scene.gather_visible_dynamic_shadow_renderables(depth_context.get_visibility_frustum(), depth_visible);
	depth_renderer.push_depth_renderables(depth_context, depth_visible);
	depth_renderer.flush(cmd, depth_context);
}

void SceneViewerApplication::update_scene(double, double elapsed_time)
{
	auto &scene = scene_loader.get_scene();

	if (reflection)
		lighting.environment_radiance = &reflection->get_image()->get_view();
	if (irradiance)
		lighting.environment_irradiance = &irradiance->get_image()->get_view();
	lighting.shadow.inv_cutoff_distance = 1.0f / config.cascade_cutoff_distance;
	lighting.environment.intensity = skydome_intensity;
	lighting.refraction.falloff = vec3(1.0f / 1.5f, 1.0f / 2.5f, 1.0f / 5.0f);

	context.set_camera(*selected_camera);
	scene.set_render_pass_data(&forward_renderer, &deferred_renderer, &depth_renderer, &context);

	animation_system->animate(elapsed_time);
	scene.update_cached_transforms();

	lighting.directional.direction = selected_directional->direction;
	lighting.directional.color = selected_directional->color;

	scene.refresh_per_frame(context);
}

void SceneViewerApplication::render_scene()
{
	auto &wsi = get_wsi();
	auto &device = wsi.get_device();
	auto &scene = scene_loader.get_scene();

	if (config.force_shadow_map_update)
		need_shadow_map_update = true;

	if (need_shadow_map_update)
		update_shadow_scene_aabb();

	graph.setup_attachments(device, &device.get_swapchain_view());

	lighting.shadow_near = nullptr;
	lighting.shadow_far = nullptr;
	if (config.directional_light_shadows)
	{
		lighting.shadow_far = &graph.get_physical_texture_resource(
				graph.get_texture_resource("shadow-main").get_physical_index());

		if (config.directional_light_cascaded_shadows)
		{
			lighting.shadow_near = &graph.get_physical_texture_resource(
					graph.get_texture_resource("shadow-near").get_physical_index());
		}
	}

	scene.bind_render_graph_resources(graph);
	graph.enqueue_render_passes(device);

	need_shadow_map_update = false;
}

void SceneViewerApplication::render_frame(double frame_time, double elapsed_time)
{
	update_scene(frame_time, elapsed_time);
	render_scene();
}

bool Application::poll()
{
	auto &wsi = get_wsi();
	if (!get_platform().alive(wsi))
		return false;

	Filesystem::get().poll_notifications();
	EventManager::get_global().dispatch();
	return true;
}

void Application::run_frame()
{
	wsi.begin_frame();
	render_frame(wsi.get_platform().get_frame_timer().get_frame_time(),
	             wsi.get_platform().get_frame_timer().get_elapsed());
	wsi.end_frame();
}

}
