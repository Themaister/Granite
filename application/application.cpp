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

void SceneViewerApplication::apply_water_depth_tint(Vulkan::CommandBuffer &cmd)
{
	auto &device = cmd.get_device();
	cmd.set_quad_state();
	cmd.set_input_attachments(0, 1);
	cmd.set_blend_enable(true);
	cmd.set_blend_op(VK_BLEND_OP_ADD);
	CommandBufferUtil::set_quad_vertex_state(cmd);
	cmd.set_depth_test(true, false);
	cmd.set_depth_compare(VK_COMPARE_OP_GREATER);

	struct Tint
	{
		mat4 inv_view_proj;
		vec3 falloff;
	} tint;

	tint.inv_view_proj = context.get_render_parameters().inv_view_projection;
	tint.falloff = vec3(1.0f / 1.5f, 1.0f / 2.5f, 1.0f / 5.0f);
	cmd.push_constants(&tint, 0, sizeof(tint));

	cmd.set_blend_factors(VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_SRC_COLOR, VK_BLEND_FACTOR_ZERO);
	auto *program = device.get_shader_manager().register_graphics("assets://shaders/water_tint.vert",
	                                                              "assets://shaders/water_tint.frag");
	auto variant = program->register_variant({});
	cmd.set_program(*program->get_program(variant));
	cmd.draw(4);
}

void SceneViewerApplication::lighting_pass(Vulkan::CommandBuffer &cmd, bool reflection_pass)
{
	cmd.set_quad_state();
	cmd.set_input_attachments(0, 1);
	cmd.set_blend_enable(true);
	cmd.set_blend_factors(VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE);
	cmd.set_blend_op(VK_BLEND_OP_ADD);
	CommandBufferUtil::set_quad_vertex_state(cmd);

	auto &device = cmd.get_device();
	auto *program = device.get_shader_manager().register_graphics("assets://shaders/lights/directional.vert",
	                                                              "assets://shaders/lights/directional.frag");
	unsigned variant = program->register_variant({{ "SHADOW_CASCADES", reflection_pass ? 0 : 1 }});
	cmd.set_program(*program->get_program(variant));
	cmd.set_depth_test(true, false);
	cmd.set_depth_compare(VK_COMPARE_OP_GREATER);
	assert(reflection && irradiance);
	cmd.set_texture(1, 0, reflection->get_image()->get_view(), Vulkan::StockSampler::LinearClamp);
	cmd.set_texture(1, 1, irradiance->get_image()->get_view(), Vulkan::StockSampler::LinearClamp);
	cmd.set_texture(1, 2, *shadow_map, Vulkan::StockSampler::LinearClamp);

	if (!reflection_pass)
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

	// Skip fog for non-reflection passes.
	if (!reflection_pass)
	{
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
		program = device.get_shader_manager().register_graphics("assets://shaders/lights/fog.vert",
		                                                        "assets://shaders/lights/fog.frag");
		variant = program->register_variant({});
		cmd.set_program(*program->get_program(variant));
		cmd.draw(4);
	}
}

SceneViewerApplication::SceneViewerApplication(const std::string &path, unsigned width, unsigned height)
	: Application(width, height),
      depth_renderer(Renderer::Type::DepthOnly),
      plane_reflection("assets://gltf-sandbox/textures/ocean_normal.ktx")
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

void SceneViewerApplication::render_main_pass(Vulkan::CommandBuffer &cmd, const mat4 &proj, const mat4 &view, bool reflections)
{
	auto &scene = scene_loader.get_scene();
	context.set_camera(proj, view);
	visible.clear();
	scene.gather_visible_opaque_renderables(context.get_visibility_frustum(), visible);
	scene.gather_background_renderables(visible);
	if (reflections)
		visible.push_back({ &plane_reflection, nullptr });
	renderer.begin();
	renderer.push_renderables(context, visible);
	renderer.flush(cmd, context);
}

static inline string tagcat(const std::string &a, const std::string &b)
{
	return a + "-" + b;
}

void SceneViewerApplication::add_main_pass(Vulkan::Device &device, const std::string &tag, MainPassType type)
{
	AttachmentInfo emissive, albedo, normal, pbr, depth;
	emissive.format = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
	albedo.format = VK_FORMAT_R8G8B8A8_SRGB;
	normal.format = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
	pbr.format = VK_FORMAT_R8G8_UNORM;
	depth.format = device.get_default_depth_stencil_format();

	AttachmentInfo reflection_blur;
	reflection_blur.format = VK_FORMAT_B10G11R11_UFLOAT_PACK32;

	if (type != MainPassType::Main)
	{
		emissive.size_x = 0.5f;
		emissive.size_y = 0.5f;
		albedo.size_x = 0.5f;
		albedo.size_y = 0.5f;
		normal.size_x = 0.5f;
		normal.size_y = 0.5f;
		pbr.size_x = 0.5f;
		pbr.size_y = 0.5f;
		depth.size_x = 0.5f;
		depth.size_y = 0.5f;

		reflection_blur.size_x = 0.25f;
		reflection_blur.size_y = 0.25f;
		reflection_blur.levels = 0;
	}

	auto &gbuffer = graph.add_pass(tagcat("gbuffer", tag), VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
	gbuffer.add_color_output(tagcat("emissive", tag), emissive);
	gbuffer.add_color_output(tagcat("albedo", tag), albedo);
	gbuffer.add_color_output(tagcat("normal", tag), normal);
	gbuffer.add_color_output(tagcat("pbr", tag), pbr);
	gbuffer.set_depth_stencil_output(tagcat("depth", tag), depth);
	gbuffer.set_build_render_pass([this, type](Vulkan::CommandBuffer &cmd) {
		if (type == MainPassType::Reflection)
		{
			mat4 proj, view;
			float z_near;
			vec3 center = vec3(50.0f, -1.5f, 10.0f);
			vec3 normal = vec3(0.0f, 1.0f, 0.0f);
			float rad_up = 10.0f;
			float rad_x = 10.0f;
			compute_plane_reflection(proj, view, cam.get_position(), center, normal, vec3(1.0f, 0.0f, 0.0f),
			                         rad_up, rad_x, z_near, 200.0f);

			plane_reflection.set_position(center);
			plane_reflection.set_normal(normal);
			plane_reflection.set_dpdy(vec3(-rad_up, 0.0f, 0.0f));
			plane_reflection.set_dpdx(vec3(0.0f, 0.0f, -rad_x));
			render_main_pass(cmd, proj, view, false);
		}
		else if (type == MainPassType::Refraction)
		{
			mat4 proj, view;
			float z_near;
			vec3 center = vec3(50.0f, -1.5f, 10.0f);
			vec3 normal = vec3(0.0f, 1.0f, 0.0f);
			float rad_up = 10.0f;
			float rad_x = 10.0f;
			compute_plane_refraction(proj, view, cam.get_position(), center, normal, vec3(1.0f, 0.0f, 0.0f),
			                         rad_up, rad_x, z_near, 200.0f);
			render_main_pass(cmd, proj, view, false);
		}
		else
		{
			render_main_pass(cmd, cam.get_projection(), cam.get_view(), true);
		}
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

	lighting.add_texture_input("vsm-main");
	if (type == MainPassType::Main)
	{
		lighting.add_texture_input("vsm-near");
		lighting.add_texture_input("reflection");
		lighting.add_texture_input("refraction");
	}

	lighting.set_build_render_pass([this, type](Vulkan::CommandBuffer &cmd) {
		lighting_pass(cmd, type != MainPassType::Main);
		if (type == MainPassType::Refraction)
			apply_water_depth_tint(cmd);
	});

	if (type != MainPassType::Main)
	{
		auto &reflection_blur_pass = graph.add_pass(tagcat("blur", tag), VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
		reflection_blur_pass.add_texture_input(tagcat("HDR", tag));
		reflection_blur_pass.add_color_output(tag, reflection_blur);
		reflection_blur_pass.set_build_render_pass([this, &reflection_blur_pass](Vulkan::CommandBuffer &cmd) {
			reflection_blur_pass.set_texture_inputs(cmd, 0, 0, Vulkan::StockSampler::LinearClamp);
			CommandBufferUtil::draw_quad(cmd, "assets://shaders/quad.vert", "assets://shaders/blur.frag",
			                             {{"METHOD", 6}});
		});
	}
}

void SceneViewerApplication::add_shadow_pass(Vulkan::Device &device, const std::string &tag, DepthPassType type)
{
	AttachmentInfo shadowmap;
	AttachmentInfo vsm_output;
	shadowmap.format = device.get_default_depth_format();
	shadowmap.samples = 1;
	shadowmap.size_class = SizeClass::Absolute;

	vsm_output.format = VK_FORMAT_R32G32_SFLOAT;
	vsm_output.samples = 1;
	vsm_output.size_class = SizeClass::Absolute;

	if (type == DepthPassType::Main)
	{
		shadowmap.size_x = 4096.0f;
		shadowmap.size_y = 4096.0f;
		vsm_output.size_x = 4096.0f;
		vsm_output.size_y = 4096.0f;
	}
	else
	{
		shadowmap.size_x = 1024.0f;
		shadowmap.size_y = 1024.0f;
		vsm_output.size_x = 1024.0f;
		vsm_output.size_y = 1024.0f;
	}

	auto vsm_resolve_output = vsm_output;
	vsm_resolve_output.samples = 1;
	auto vsm_mipmapped = vsm_resolve_output;
	vsm_mipmapped.levels = 0;

	auto &shadowpass = graph.add_pass(tagcat("shadow", tag), VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
	shadowpass.set_depth_stencil_output(tagcat("shadowmap", tag), shadowmap);
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

	auto &vsm_resolve = graph.add_pass(tagcat("vsm-resolve", tag), VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
	vsm_resolve.add_attachment_input(tagcat("shadowmap", tag));
	vsm_resolve.add_color_output(tagcat("vsm-resolved", tag), vsm_resolve_output);
	vsm_resolve.set_build_render_pass([this](Vulkan::CommandBuffer &cmd) {
		cmd.set_input_attachments(0, 0);
		CommandBufferUtil::draw_quad(cmd, "assets://shaders/quad.vert", "assets://shaders/lights/resolve_vsm.frag");
	});

	vsm_resolve.set_need_render_pass([this, type]() {
		return type == DepthPassType::Main ? need_shadow_map_update : true;
	});

	auto &vsm_vertical = graph.add_pass(tagcat("vsm-vertical", tag), VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
	vsm_vertical.add_texture_input(tagcat("vsm-resolved", tag));
	vsm_vertical.add_color_output(tagcat("vsm-vertical", tag), vsm_resolve_output);
	vsm_vertical.set_build_render_pass([this, &vsm_vertical](Vulkan::CommandBuffer &cmd) {
		vsm_vertical.set_texture_inputs(cmd, 0, 0, Vulkan::StockSampler::NearestClamp);
		CommandBufferUtil::draw_quad(cmd, "assets://shaders/quad.vert", "assets://shaders/blur.frag", {{ "METHOD", 4 }});
	});

	vsm_vertical.set_need_render_pass([this, type]() {
		return type == DepthPassType::Main ? need_shadow_map_update : true;
	});

	auto &vsm_horizontal = graph.add_pass(tagcat("vsm-horizontal", tag), VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
	vsm_horizontal.add_texture_input(tagcat("vsm-vertical", tag));
	vsm_horizontal.add_color_output(tagcat("vsm", tag), vsm_mipmapped);
	vsm_horizontal.set_build_render_pass([this, &vsm_horizontal](Vulkan::CommandBuffer &cmd) {
		vsm_horizontal.set_texture_inputs(cmd, 0, 0, Vulkan::StockSampler::NearestClamp);
		CommandBufferUtil::draw_quad(cmd, "assets://shaders/quad.vert", "assets://shaders/blur.frag", {{ "METHOD", 1 }});
	});

	vsm_horizontal.set_need_render_pass([this, type]() {
		return type == DepthPassType::Main ? need_shadow_map_update : true;
	});
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
	AttachmentInfo backbuffer;

	const char *backbuffer_source = getenv("GRANITE_SURFACE");
	graph.set_backbuffer_source(backbuffer_source ? backbuffer_source : "backbuffer");

	add_shadow_pass(swap.get_device(), "main", DepthPassType::Main);
	add_shadow_pass(swap.get_device(), "near", DepthPassType::Near);
	add_main_pass(swap.get_device(), "reflection", MainPassType::Reflection);
	add_main_pass(swap.get_device(), "refraction", MainPassType::Refraction);
	add_main_pass(swap.get_device(), "main", MainPassType::Main);
	setup_hdr_postprocess(graph, "HDR-main", "tonemapped");

	auto &ui = graph.add_pass("ui", VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
	ui.add_color_output("backbuffer", backbuffer, "tonemapped");
	ui.set_build_render_pass([this](Vulkan::CommandBuffer &cmd) {
		//UI::UIManager::get().render(cmd);
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

	animation_system->animate(elapsed_time);
	scene.update_cached_transforms();
	scene.refresh_per_frame(context);

	window->set_background_color(vec4(1.0f));
	window->set_margin(5);
	window->set_floating_position(ivec2(40));
	window->set_title("My Window");
	//window->set_target_geometry(window->get_target_geometry() + vec2(1.0f));

	graph.setup_attachments(device, &device.get_swapchain_view());
	shadow_map = &graph.get_physical_texture_resource(graph.get_texture_resource("vsm-main").get_physical_index());
	shadow_map_near = &graph.get_physical_texture_resource(graph.get_texture_resource("vsm-near").get_physical_index());
	plane_reflection.set_reflection_texture(&graph.get_physical_texture_resource(graph.get_texture_resource("reflection").get_physical_index()));
	plane_reflection.set_refraction_texture(&graph.get_physical_texture_resource(graph.get_texture_resource("refraction").get_physical_index()));
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
