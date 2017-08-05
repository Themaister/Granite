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

#define RENDERER_FORWARD 0
#define RENDERER_DEFERRED 1
#define RENDERER RENDERER_DEFERRED

#include "application.hpp"
#include <stdexcept>
#include "sprite.hpp"
#include "horizontal_packing.hpp"
#include "image_widget.hpp"
#include "label.hpp"
#include "post/hdr.hpp"

using namespace std;
using namespace Vulkan;

namespace Granite
{
Application::Application(unsigned width, unsigned height)
{
	EventManager::get_global();
	Filesystem::get();

	platform = create_default_application_platform(width, height);

	if (!wsi.init(platform.get(), width, height))
		throw runtime_error("Failed to initialize WSI.");
}

static vec3 light_direction()
{
	return normalize(vec3(0.5f, 1.2f, 0.8f));
}

static const float cascade_cutoff_distance = 10.0f;

SceneViewerApplication::SceneViewerApplication(const std::string &path, unsigned width, unsigned height)
	: Application(width, height),
	  forward_renderer(RendererType::GeneralForward),
      deferred_renderer(RendererType::GeneralDeferred),
      depth_renderer(RendererType::DepthOnly),
      plane_reflection("builtin://gltf-sandbox/textures/ocean_normal.ktx")
{
	scene_loader.load_scene(path);
	animation_system = scene_loader.consume_animation_system();
	context.set_lighting_parameters(&lighting);

	auto &skybox = scene_loader.get_scene().get_entity_pool().get_component_group<SkyboxComponent>();
	if (!skybox.empty())
	{
		auto *skybox_component = get<0>(skybox.front());
		skydome_reflection = skybox_component->reflection_path;
		skydome_irradiance = skybox_component->irradiance_path;
	}

	auto *environment = scene_loader.get_scene().get_environment();
	if (environment)
		lighting.fog = environment->fog;
	else
		lighting.fog = {};

	cam.look_at(vec3(0.0f, 0.0f, 8.0f), vec3(0.0f));
	context.set_camera(cam);

	auto &ui = UI::UIManager::get();
	window = ui.add_child<UI::Window>();
	auto *w0 = window->add_child<UI::Widget>();
	auto *w1 = window->add_child<UI::Widget>();
	auto *w2 = window->add_child<UI::Widget>();
	auto *image = window->add_child<UI::Image>("builtin://gltf-sandbox/textures/maister.png");
	image->set_minimum_geometry(image->get_target_geometry() * vec2(1.0f / 16.0f));
	image->set_keep_aspect_ratio(true);
	auto *w3 = window->add_child<UI::Widget>();
	w0->set_background_color(vec4(1.0f, 0.0f, 0.0f, 1.0f));
	w1->set_background_color(vec4(0.0f, 1.0f, 0.0f, 1.0f));
	w2->set_background_color(vec4(1.0f, 1.0f, 0.0f, 1.0f));
	w3->set_background_color(vec4(0.0f, 1.0f, 1.0f, 1.0f));
	w0->set_target_geometry(vec2(400.0f, 60.0f));
	w1->set_target_geometry(vec2(400.0f, 60.0f));
	w2->set_target_geometry(vec2(400.0f, 60.0f));
	w3->set_target_geometry(vec2(40.0f, 60.0f));
	w0->set_minimum_geometry(vec2(40.0f, 10.0f));
	w1->set_minimum_geometry(vec2(40.0f, 10.0f));
	w2->set_minimum_geometry(vec2(40.0f, 10.0f));
	w3->set_minimum_geometry(vec2(40.0f, 10.0f));
	window->set_target_geometry(ivec2(10));

	auto *label = window->add_child<UI::Label>("Hai :D");
	label->set_margin(20.0f);
	label->set_color(vec4(0.0f, 0.0f, 0.0f, 1.0f));
	label->set_font_alignment(Font::Alignment::Center);

	auto *w4 = window->add_child<UI::HorizontalPacking>();
	w4->set_margin(10.0f);
	auto *tmp = w4->add_child<UI::Widget>();
	tmp->set_background_color(vec4(0.0f, 0.0f, 0.0f, 1.0f));
	tmp->set_minimum_geometry(vec2(50.0f));
	//tmp->set_target_geometry(vec2(50.0f));
	tmp = w4->add_child<UI::Widget>();
	tmp->set_background_color(vec4(0.0f, 0.0f, 0.0f, 1.0f));
	tmp->set_minimum_geometry(vec2(50.0f));
	//tmp->set_target_geometry(vec2(50.0f));

	w2->set_size_is_flexible(true);

	EVENT_MANAGER_REGISTER_LATCH(SceneViewerApplication, on_swapchain_changed, on_swapchain_destroyed, SwapchainParameterEvent);
	EVENT_MANAGER_REGISTER_LATCH(SceneViewerApplication, on_device_created, on_device_destroyed, DeviceCreatedEvent);
}

void SceneViewerApplication::on_device_created(const Event &e)
{
	auto &device = e.as<DeviceCreatedEvent>();
	reflection = device.get_device().get_texture_manager().request_texture(skydome_reflection);
	irradiance = device.get_device().get_texture_manager().request_texture(skydome_irradiance);
	graph.set_device(&device.get_device());
}

void SceneViewerApplication::on_device_destroyed(const Event &)
{
	reflection = nullptr;
	irradiance = nullptr;
	graph.set_device(nullptr);
}

void SceneViewerApplication::render_main_pass(Vulkan::CommandBuffer &cmd, const mat4 &proj, const mat4 &view)
{
	auto &scene = scene_loader.get_scene();
	context.set_camera(proj, view);
	visible.clear();
	scene.gather_visible_opaque_renderables(context.get_visibility_frustum(), visible);
	scene.gather_background_renderables(visible);
	scene.gather_visible_render_pass_sinks(context.get_render_parameters().camera_position, visible);
	deferred_renderer.begin();
	deferred_renderer.push_renderables(context, visible);
	deferred_renderer.flush(cmd, context);
}

static inline string tagcat(const std::string &a, const std::string &b)
{
	return a + "-" + b;
}

void SceneViewerApplication::add_main_pass(Vulkan::Device &device, const std::string &tag, MainPassType type)
{
#if RENDERER == RENDERER_FORWARD
	AttachmentInfo color, depth;
	color.format = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
	depth.format = device.get_default_depth_format();

	auto &lighting = graph.add_pass(tagcat("lighting", tag), VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
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

	lighting.set_build_render_pass([this, type](Vulkan::CommandBuffer &cmd) {
		renderer.set_mesh_renderer_options(Renderer::ENVIRONMENT_ENABLE_BIT |
		                                   Renderer::SHADOW_ENABLE_BIT |
		                                   Renderer::FOG_ENABLE_BIT |
		                                   Renderer::SHADOW_CASCADE_ENABLE_BIT);
		render_main_pass(cmd, cam.get_projection(), cam.get_view());
	});

	lighting.add_texture_input("shadow-main");
	if (type == MainPassType::Main)
	{
		lighting.add_texture_input("shadow-near");
		scene_loader.get_scene().add_render_pass_dependencies(graph, lighting);
	}
#elif RENDERER == RENDERER_DEFERRED
	AttachmentInfo emissive, albedo, normal, pbr, depth;
	emissive.format = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
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
	gbuffer.set_build_render_pass([this, type](Vulkan::CommandBuffer &cmd) {
		render_main_pass(cmd, cam.get_projection(), cam.get_view());
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
			memset(value, 0, sizeof(*value));
		return true;
	});

	auto &lighting = graph.add_pass(tagcat("lighting", tag), VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
	lighting.add_color_output(tagcat("HDR", tag), emissive, tagcat("emissive", tag));
	lighting.add_attachment_input(tagcat("albedo", tag));
	lighting.add_attachment_input(tagcat("normal", tag));
	lighting.add_attachment_input(tagcat("pbr", tag));
	lighting.add_attachment_input(tagcat("depth", tag));
	lighting.set_depth_stencil_input(tagcat("depth", tag));

	lighting.add_texture_input("shadow-main");
	lighting.add_texture_input("shadow-near");
	scene_loader.get_scene().add_render_pass_dependencies(graph, gbuffer);

	lighting.set_build_render_pass([this, type](Vulkan::CommandBuffer &cmd) {
		DeferredLightRenderer::render_light(cmd, context);
	});
#endif
}

void SceneViewerApplication::add_shadow_pass(Vulkan::Device &device, const std::string &tag, DepthPassType type)
{
	AttachmentInfo shadowmap;
	shadowmap.format = device.get_default_depth_format();
	shadowmap.samples = 1;
	shadowmap.size_class = SizeClass::Absolute;

	if (type == DepthPassType::Main)
	{
		shadowmap.size_x = 4096.0f;
		shadowmap.size_y = 4096.0f;
	}
	else
	{
		shadowmap.size_x = 1024.0f;
		shadowmap.size_y = 1024.0f;
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

void SceneViewerApplication::on_swapchain_changed(const Event &e)
{
	auto &swap = e.as<SwapchainParameterEvent>();
	auto physical_buffers = graph.consume_physical_buffers();
	graph.reset();
	graph.set_device(&swap.get_device());

	ResourceDimensions dim;
	dim.width = swap.get_width();
	dim.height = swap.get_height();
	dim.format = swap.get_format();
	graph.set_backbuffer_dimensions(dim);
	AttachmentInfo backbuffer;

	const char *backbuffer_source = getenv("GRANITE_SURFACE");
	graph.set_backbuffer_source(backbuffer_source ? backbuffer_source : "backbuffer");

	scene_loader.get_scene().add_render_passes(graph);

	add_shadow_pass(swap.get_device(), "main", DepthPassType::Main);
	add_shadow_pass(swap.get_device(), "near", DepthPassType::Near);
	add_main_pass(swap.get_device(), "reflection", MainPassType::Reflection);
	add_main_pass(swap.get_device(), "refraction", MainPassType::Refraction);
	add_main_pass(swap.get_device(), "main", MainPassType::Main);
	setup_hdr_postprocess(graph, "HDR-main", "tonemapped");

	auto &ui = graph.add_pass("ui", VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
	ui.add_color_output("backbuffer", backbuffer, "tonemapped");
	ui.set_build_render_pass([this](Vulkan::CommandBuffer &cmd) {
		UI::UIManager::get().render(cmd);
	});

	graph.bake();
	graph.log();
	graph.install_physical_buffers(move(physical_buffers));

	need_shadow_map_update = true;
}

void SceneViewerApplication::on_swapchain_destroyed(const Event &)
{
}

void SceneViewerApplication::update_shadow_map()
{
	auto &scene = scene_loader.get_scene();
	depth_visible.clear();

	// Get the scene AABB for shadow casters.
	auto &shadow_casters = scene.get_entity_pool().get_component_group<CachedSpatialTransformComponent, RenderableComponent, CastsShadowComponent>();
	AABB aabb(vec3(FLT_MAX), vec3(-FLT_MAX));
	for (auto &caster : shadow_casters)
		aabb.expand(get<0>(caster)->world_aabb);
	scene_aabb = aabb;

	mat4 view = mat4_cast(look_at(-light_direction(), vec3(0.0f, 1.0f, 0.0f)));

	// Project the scene AABB into the light and find our ortho ranges.
	AABB ortho_range = aabb.transform(view);
	mat4 proj = ortho(ortho_range);

	// Standard scale/bias.
	lighting.shadow.far_transform = glm::translate(vec3(0.5f, 0.5f, 0.0f)) * glm::scale(vec3(0.5f, 0.5f, 1.0f)) * proj * view;
	depth_context.set_camera(proj, view);

	depth_renderer.begin();
	scene.gather_visible_shadow_renderables(depth_context.get_visibility_frustum(), depth_visible);
	depth_renderer.push_renderables(depth_context, depth_visible);
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
	mat4 view = mat4_cast(look_at(-light_direction(), vec3(0.0f, 1.0f, 0.0f)));
	AABB ortho_range_depth = scene_aabb.transform(view); // Just need this to determine Zmin/Zmax.

	auto near_camera = static_cast<Camera &>(cam);
	near_camera.set_depth_range(near_camera.get_znear(), cascade_cutoff_distance);
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
	scene.gather_visible_shadow_renderables(depth_context.get_visibility_frustum(), depth_visible);
	depth_renderer.push_renderables(depth_context, depth_visible);
	depth_renderer.flush(cmd, depth_context);
}

void SceneViewerApplication::render_frame(double, double elapsed_time)
{
	auto &wsi = get_wsi();
	auto &scene = scene_loader.get_scene();
	auto &device = wsi.get_device();

	lighting.environment_radiance = &reflection->get_image()->get_view();
	lighting.environment_irradiance = &irradiance->get_image()->get_view();
	lighting.shadow.inv_cutoff_distance = 1.0f / cascade_cutoff_distance;
	lighting.environment.intensity = 1.0f;
	lighting.environment.mipscale = 6.0f;
	lighting.refraction.falloff = vec3(1.0f / 1.5f, 1.0f / 2.5f, 1.0f / 5.0f);
	lighting.directional.direction = light_direction();
	lighting.directional.color = vec3(3.0f, 2.5f, 2.5f);

	context.set_camera(cam);
	scene.set_render_pass_data(&forward_renderer, &context);

	animation_system->animate(elapsed_time);
	scene.update_cached_transforms();
	scene.refresh_per_frame(context);

	window->set_background_color(vec4(1.0f));
	window->set_margin(5);
	window->set_floating_position(ivec2(40));
	window->set_title("My Window");
	//window->set_target_geometry(window->get_target_geometry() + vec2(1.0f));

	graph.setup_attachments(device, &device.get_swapchain_view());
	lighting.shadow_far = &graph.get_physical_texture_resource(graph.get_texture_resource("shadow-main").get_physical_index());
	lighting.shadow_near = &graph.get_physical_texture_resource(graph.get_texture_resource("shadow-near").get_physical_index());
	scene.bind_render_graph_resources(graph);
	graph.enqueue_render_passes(device);

	need_shadow_map_update = false;
}

int Application::run()
{
	auto &wsi = get_wsi();
	while (get_platform().alive(wsi))
	{
		Filesystem::get().poll_notifications();
		wsi.begin_frame();
		render_frame(wsi.get_platform().get_frame_timer().get_frame_time(),
					 wsi.get_platform().get_frame_timer().get_elapsed());
		wsi.end_frame();
	}
	return 0;
}

}
