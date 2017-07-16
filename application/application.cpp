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

void SceneViewerApplication::lighting_pass(Vulkan::CommandBuffer &cmd)
{
	cmd.set_quad_state();
	cmd.set_input_attachments(0, 1);
	cmd.set_blend_enable(true);
	cmd.set_blend_factors(VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE);
	cmd.set_blend_op(VK_BLEND_OP_ADD);

	int8_t *data = static_cast<int8_t *>(cmd.allocate_vertex_data(0, 8, 2));
	*data++ = -128;
	*data++ = +127;
	*data++ = +127;
	*data++ = +127;
	*data++ = -128;
	*data++ = -128;
	*data++ = +127;
	*data++ = -128;
	cmd.set_vertex_attrib(0, 0, VK_FORMAT_R8G8_SNORM, 0);

	auto &device = cmd.get_device();
	auto *program = device.get_shader_manager().register_graphics("assets://shaders/lights/directional.vert",
	                                                              "assets://shaders/lights/directional.frag");
	unsigned variant = program->register_variant({});
	cmd.set_program(*program->get_program(variant));
	cmd.set_depth_test(true, false);
	cmd.set_depth_compare(VK_COMPARE_OP_GREATER);
	assert(reflection && irradiance);
	cmd.set_texture(1, 0, reflection->get_image()->get_view(), Vulkan::StockSampler::LinearClamp);
	cmd.set_texture(1, 1, irradiance->get_image()->get_view(), Vulkan::StockSampler::LinearClamp);
	cmd.set_texture(1, 2, *shadow_map, Vulkan::StockSampler::LinearClamp);
	cmd.set_texture(1, 3, *shadow_map_near, Vulkan::StockSampler::LinearClamp);

	struct DirectionalLightPush
	{
		vec4 inv_view_proj_col2;
		vec4 shadow_col2;
		vec4 shadow_near_col2;
		vec4 direction_inv_cutoff;
		vec4 color_env_intensity;
		vec4 camera_pos_mipscale;
		vec3 camera_front;
	} push;

	const float intensity = 1.0f;
	const float mipscale = 6.0f;

	mat4 total_shadow_transform = shadow_transform * context.get_render_parameters().inv_view_projection;
	mat4 total_shadow_transform_near = shadow_transform_near * context.get_render_parameters().inv_view_projection;

	struct DirectionalLightUBO
	{
		mat4 inv_view_projection;
		mat4 shadow_transform;
		mat4 shadow_transform_near;
	};
	auto *ubo = static_cast<DirectionalLightUBO *>(cmd.allocate_constant_data(0, 0, sizeof(DirectionalLightUBO)));
	ubo->inv_view_projection = context.get_render_parameters().inv_view_projection;
	ubo->shadow_transform = total_shadow_transform;
	ubo->shadow_transform_near = total_shadow_transform_near;

	push.inv_view_proj_col2 = context.get_render_parameters().inv_view_projection[2];
	push.shadow_col2 = total_shadow_transform[2];
	push.shadow_near_col2 = total_shadow_transform_near[2];
	push.color_env_intensity = vec4(3.0f, 2.5f, 2.5f, intensity);
	push.direction_inv_cutoff = vec4(light_direction(), 1.0f / cascade_cutoff_distance);
	push.camera_pos_mipscale = vec4(context.get_render_parameters().camera_position, mipscale);
	push.camera_front = context.get_render_parameters().camera_front;
	cmd.push_constants(&push, 0, sizeof(push));

	cmd.draw(4);

	struct Fog
	{
		mat4 inv_view_proj;
		vec4 camera_pos;
		vec4 color_falloff;
	} fog;

	fog.inv_view_proj = context.get_render_parameters().inv_view_projection;
	fog.camera_pos = vec4(context.get_render_parameters().camera_position, 0.0f);
	fog.color_falloff = vec4(context.get_fog_parameters().color, context.get_fog_parameters().falloff);
	cmd.push_constants(&fog, 0, sizeof(fog));

	cmd.set_blend_factors(VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_SRC_ALPHA);
	program = device.get_shader_manager().register_graphics("assets://shaders/lights/fog.vert", "assets://shaders/lights/fog.frag");
	variant = program->register_variant({});
	cmd.set_program(*program->get_program(variant));
	cmd.draw(4);
}

SceneViewerApplication::SceneViewerApplication(const std::string &path, unsigned width, unsigned height)
	: Application(width, height),
      depth_renderer(Renderer::Type::DepthOnly)
{
	scene_loader.load_scene(path);
	animation_system = scene_loader.consume_animation_system();

	auto &skybox = scene_loader.get_scene().get_entity_pool().get_component_group<SkyboxComponent>();
	if (!skybox.empty())
	{
		auto *skybox_component = get<0>(skybox.front());
		skydome_reflection = skybox_component->reflection_path;
		skydome_irradiance = skybox_component->irradiance_path;
	}

	auto *environment = scene_loader.get_scene().get_environment();
	if (environment)
		context.set_fog_parameters(environment->fog);

	cam.look_at(vec3(0.0f, 0.0f, 8.0f), vec3(0.0f));
	context.set_camera(cam);

	auto &ui = UI::UIManager::get();
	window = ui.add_child<UI::Window>();
	auto *w0 = window->add_child<UI::Widget>();
	auto *w1 = window->add_child<UI::Widget>();
	auto *w2 = window->add_child<UI::Widget>();
	auto *image = window->add_child<UI::Image>("assets://gltf-sandbox/textures/maister.png");
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

	EventManager::get_global().register_latch_handler(SwapchainParameterEvent::type_id,
	                                                  &SceneViewerApplication::on_swapchain_changed,
	                                                  &SceneViewerApplication::on_swapchain_destroyed,
	                                                  this);

	EventManager::get_global().register_latch_handler(DeviceCreatedEvent::type_id,
	                                                  &SceneViewerApplication::on_device_created,
	                                                  &SceneViewerApplication::on_device_destroyed,
	                                                  this);
}

void SceneViewerApplication::on_device_created(const Event &e)
{
	auto &device = e.as<DeviceCreatedEvent>();
	reflection = device.get_device().get_texture_manager().request_texture(skydome_reflection);
	irradiance = device.get_device().get_texture_manager().request_texture(skydome_irradiance);
}

void SceneViewerApplication::on_device_destroyed(const Event &)
{
	reflection = nullptr;
	irradiance = nullptr;
}

void SceneViewerApplication::on_swapchain_changed(const Event &e)
{
	auto &swap = e.as<SwapchainParameterEvent>();
	auto physical_buffers = graph.consume_physical_buffers();
	graph.reset();

	ResourceDimensions dim;
	dim.width = swap.get_width();
	dim.height = swap.get_height();
	dim.format = swap.get_format();
	graph.set_backbuffer_dimensions(dim);

	const char *backbuffer_source = getenv("GRANITE_SURFACE");
	graph.set_backbuffer_source(backbuffer_source ? backbuffer_source : "backbuffer");

	AttachmentInfo shadowmap;
	shadowmap.size_class = SizeClass::Absolute;
	shadowmap.size_x = 2048.0f;
	shadowmap.size_y = 2048.0f;
	shadowmap.format = swap.get_device().get_default_depth_format();
	shadowmap.samples = 4;
	auto shadowmap_near = shadowmap;
	shadowmap_near.size_x = 512.0f;
	shadowmap_near.size_y = 512.0f;

	AttachmentInfo vsm_output;
	vsm_output.size_class = SizeClass::Absolute;
	vsm_output.size_x = 2048.0f;
	vsm_output.size_y = 2048.0f;
	vsm_output.format = VK_FORMAT_R32G32_SFLOAT;
	vsm_output.samples = 4;
	auto vsm_output_near = vsm_output;
	vsm_output_near.size_x = 512.0f;
	vsm_output_near.size_y = 512.0f;

	auto vsm_resolve_output = vsm_output;
	vsm_resolve_output.samples = 1;
	auto vsm_resolve_near_output = vsm_output;
	vsm_resolve_near_output.samples = 1;
	vsm_resolve_near_output.size_x = 512.0f;
	vsm_resolve_near_output.size_y = 512.0f;
	auto vsm = vsm_resolve_output;
	auto vsm_mipmapped = vsm;
	vsm_mipmapped.levels = 0;

	AttachmentInfo backbuffer;
	AttachmentInfo emissive, albedo, normal, pbr, depth;
	emissive.format = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
	albedo.format = VK_FORMAT_R8G8B8A8_SRGB;
	normal.format = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
	pbr.format = VK_FORMAT_R8G8_UNORM;
	depth.format = swap.get_device().get_default_depth_stencil_format();

	auto &shadowpass = graph.add_pass("shadow", VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
	shadowpass.set_depth_stencil_output("shadowmap", shadowmap);
	shadowpass.set_build_render_pass([this](Vulkan::CommandBuffer &cmd) {
		depth_renderer.flush(cmd, depth_context);
	});
	shadowpass.set_get_clear_depth_stencil([](VkClearDepthStencilValue *value) -> bool {
		if (value)
		{
			value->depth = 1.0f;
			value->stencil = 0;
		}
		return true;
	});

	shadowpass.set_need_render_pass([this]() {
		return need_shadow_map_update;
	});

	auto &shadowpass_near = graph.add_pass("shadow-near", VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
	shadowpass_near.set_depth_stencil_output("shadowmap-near", shadowmap_near);
	shadowpass_near.set_build_render_pass([this](Vulkan::CommandBuffer &cmd) {
		render_shadow_map_near(cmd);
	});

	shadowpass_near.set_get_clear_depth_stencil([](VkClearDepthStencilValue *value) -> bool {
		if (value)
		{
			value->depth = 1.0f;
			value->stencil = 0;
		}
		return true;
	});

	auto &vsm_resolve = graph.add_pass("vsm-resolve", VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
	vsm_resolve.add_attachment_input("shadowmap");
	vsm_resolve.add_color_output("vsm-output", vsm_output);
	vsm_resolve.add_resolve_output("vsm-resolved", vsm_resolve_output);
	vsm_resolve.set_build_render_pass([this](Vulkan::CommandBuffer &cmd) {
		cmd.set_input_attachments(0, 0);
		CommandBufferUtil::draw_quad(cmd, "assets://shaders/quad.vert", "assets://shaders/lights/resolve_vsm.frag");
	});

	auto &vsm_resolve_near = graph.add_pass("vsm-resolve-near", VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
	vsm_resolve_near.add_attachment_input("shadowmap-near");
	vsm_resolve_near.add_color_output("vsm-output-near", vsm_output_near);
	vsm_resolve_near.add_resolve_output("vsm-resolved-near", vsm_resolve_near_output);
	vsm_resolve_near.set_build_render_pass([this](Vulkan::CommandBuffer &cmd) {
		cmd.set_input_attachments(0, 0);
		CommandBufferUtil::draw_quad(cmd, "assets://shaders/quad.vert", "assets://shaders/lights/resolve_vsm.frag");
	});

	vsm_resolve.set_need_render_pass([this]() {
		return need_shadow_map_update;
	});

	auto &vsm_vertical = graph.add_pass("vsm-vertical", VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
	vsm_vertical.add_texture_input("vsm-resolved");
	vsm_vertical.add_color_output("vsm-vertical", vsm);
	vsm_vertical.set_build_render_pass([this, &vsm_vertical](Vulkan::CommandBuffer &cmd) {
		vsm_vertical.set_texture_inputs(cmd, 0, 0, Vulkan::StockSampler::NearestClamp);
		CommandBufferUtil::draw_quad(cmd, "assets://shaders/quad.vert", "assets://shaders/blur.frag", {{ "METHOD", 4 }});
	});

	vsm_vertical.set_need_render_pass([this]() {
		return need_shadow_map_update;
	});

	auto &vsm_horizontal = graph.add_pass("vsm-horizontal", VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
	vsm_horizontal.add_texture_input("vsm-vertical");
	vsm_horizontal.add_color_output("vsm", vsm_mipmapped);
	vsm_horizontal.set_build_render_pass([this, &vsm_horizontal](Vulkan::CommandBuffer &cmd) {
		vsm_horizontal.set_texture_inputs(cmd, 0, 0, Vulkan::StockSampler::NearestClamp);
		CommandBufferUtil::draw_quad(cmd, "assets://shaders/quad.vert", "assets://shaders/blur.frag", {{ "METHOD", 1 }});
	});

	vsm_horizontal.set_need_render_pass([this]() {
		return need_shadow_map_update;
	});

	auto &vsm_vertical_near = graph.add_pass("vsm-vertical-near", VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
	vsm_vertical_near.add_texture_input("vsm-resolved-near");
	vsm_vertical_near.add_color_output("vsm-vertical-near", vsm_resolve_near_output);
	vsm_vertical_near.set_build_render_pass([this, &vsm_vertical_near](Vulkan::CommandBuffer &cmd) {
		vsm_vertical_near.set_texture_inputs(cmd, 0, 0, Vulkan::StockSampler::NearestClamp);
		CommandBufferUtil::draw_quad(cmd, "assets://shaders/quad.vert", "assets://shaders/blur.frag", {{ "METHOD", 3 }});
	});

	auto &vsm_horizontal_near = graph.add_pass("vsm-horizontal-near", VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
	vsm_horizontal_near.add_texture_input("vsm-vertical-near");
	vsm_horizontal_near.add_color_output("vsm-near", vsm_resolve_near_output);
	vsm_horizontal_near.set_build_render_pass([this, &vsm_horizontal_near](Vulkan::CommandBuffer &cmd) {
		vsm_horizontal_near.set_texture_inputs(cmd, 0, 0, Vulkan::StockSampler::NearestClamp);
		CommandBufferUtil::draw_quad(cmd, "assets://shaders/quad.vert", "assets://shaders/blur.frag", {{ "METHOD", 0 }});
	});

	auto &gbuffer = graph.add_pass("gbuffer", VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
	gbuffer.add_color_output("emissive", emissive);
	gbuffer.add_color_output("albedo", albedo);
	gbuffer.add_color_output("normal", normal);
	gbuffer.add_color_output("pbr", pbr);
	gbuffer.set_depth_stencil_output("depth", depth);
	gbuffer.set_build_render_pass([this](Vulkan::CommandBuffer &cmd) {
		renderer.flush(cmd, context);
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

	auto &lighting = graph.add_pass("lighting", VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
	lighting.add_color_output("HDR", emissive, "emissive");
	lighting.add_attachment_input("albedo");
	lighting.add_attachment_input("normal");
	lighting.add_attachment_input("pbr");
	lighting.add_attachment_input("depth");
	lighting.set_depth_stencil_input("depth");
	lighting.add_texture_input("vsm");
	lighting.add_texture_input("vsm-near");
	lighting.set_build_render_pass([this](Vulkan::CommandBuffer &cmd) {
		lighting_pass(cmd);
	});

	setup_hdr_postprocess(graph, "HDR", "tonemapped");

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
	shadow_transform = glm::translate(vec3(0.5f, 0.5f, 0.0f)) * glm::scale(vec3(0.5f, 0.5f, 1.0f)) * proj * view;
	depth_context.set_camera(proj, view);

	depth_renderer.begin();
	scene.gather_visible_shadow_renderables(depth_context.get_visibility_frustum(), depth_visible);
	depth_renderer.push_renderables(depth_context, depth_visible);
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

	vec2 texel_size = vec2(2.0f * sphere.w) * vec2(1.0f / shadow_map_near->get_image().get_create_info().width,
	                                               1.0f / shadow_map_near->get_image().get_create_info().height);

	// Snap to texel grid.
	center_xy = round(center_xy / texel_size) * texel_size;

	AABB ortho_range = AABB(vec3(center_xy - vec2(sphere.w), ortho_range_depth.get_minimum().z),
	                        vec3(center_xy + vec2(sphere.w), ortho_range_depth.get_maximum().z));

	mat4 proj = ortho(ortho_range);
	shadow_transform_near = glm::translate(vec3(0.5f, 0.5f, 0.0f)) * glm::scale(vec3(0.5f, 0.5f, 1.0f)) * proj * view;
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

	context.set_camera(cam);
	animation_system->animate(elapsed_time);
	scene.update_cached_transforms();
	scene.refresh_per_frame(context);

	context.set_camera(cam);
	visible.clear();

	window->set_background_color(vec4(1.0f));
	window->set_margin(5);
	window->set_floating_position(ivec2(40));
	window->set_title("My Window");
	//window->set_target_geometry(window->get_target_geometry() + vec2(1.0f));

	scene.gather_visible_opaque_renderables(context.get_visibility_frustum(), visible);
	scene.gather_background_renderables(visible);

	renderer.begin();
	renderer.push_renderables(context, visible);
	graph.setup_attachments(device, &device.get_swapchain_view());
	shadow_map = &graph.get_physical_texture_resource(graph.get_texture_resource("vsm").get_physical_index());
	shadow_map_near = &graph.get_physical_texture_resource(graph.get_texture_resource("vsm-near").get_physical_index());
	if (need_shadow_map_update)
		update_shadow_map();
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
