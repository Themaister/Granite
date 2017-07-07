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

bool SceneViewerApplication::GBufferImpl::get_clear_color(unsigned, VkClearColorValue *value)
{
	if (value)
		memset(value, 0, sizeof(*value));
	return true;
}

bool SceneViewerApplication::GBufferImpl::get_clear_depth_stencil(VkClearDepthStencilValue *value)
{
	if (value)
	{
		value->stencil = 0;
		value->depth = 1.0f;
	}

	return true;
}

void SceneViewerApplication::GBufferImpl::build_render_pass(RenderPass &, Vulkan::CommandBuffer &cmd)
{
	app->renderer.flush(cmd, app->context);
}

void SceneViewerApplication::LightingImpl::on_device_created(Vulkan::Device &device)
{
	reflection = device.get_texture_manager().request_texture(skydome_reflection);
	irradiance = device.get_texture_manager().request_texture(skydome_irradiance);
}

static vec3 light_direction()
{
	return normalize(vec3(0.5f, 1.2f, 0.8f));
}

void SceneViewerApplication::LightingImpl::build_render_pass(RenderPass &, Vulkan::CommandBuffer &cmd)
{
	cmd.set_quad_state();
	cmd.set_input_attachments(1, 0);
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
	cmd.set_texture(0, 1, reflection->get_image()->get_view(), Vulkan::StockSampler::LinearClamp);
	cmd.set_texture(0, 2, irradiance->get_image()->get_view(), Vulkan::StockSampler::LinearClamp);
	cmd.set_texture(0, 3, shadow_map->get_view(), Vulkan::StockSampler::LinearShadow);

	struct DirectionalLightPush
	{
		vec4 inv_view_proj_col2;
		vec4 shadow_col2;
		vec4 direction;
		vec4 color_env_intensity;
		vec4 camera_pos_mipscale;
	} push;

	const float intensity = 1.0f;
	const float mipscale = 6.0f;

	mat4 total_shadow_transform = shadow_transform * app->context.get_render_parameters().inv_view_projection;

	struct DirectionalLightUBO
	{
		mat4 inv_view_projection;
		mat4 shadow_transform;
	};
	auto *ubo = static_cast<DirectionalLightUBO *>(cmd.allocate_constant_data(0, 0, sizeof(DirectionalLightUBO)));
	ubo->inv_view_projection = app->context.get_render_parameters().inv_view_projection;
	ubo->shadow_transform = total_shadow_transform;

	push.inv_view_proj_col2 = app->context.get_render_parameters().inv_view_projection[2];
	push.shadow_col2 = total_shadow_transform[2];
	push.color_env_intensity = vec4(3.0f, 2.5f, 2.5f, intensity);
	push.direction = vec4(light_direction(), 0.0f);
	push.camera_pos_mipscale = vec4(app->context.get_render_parameters().camera_position, mipscale);
	cmd.push_constants(&push, 0, sizeof(push));

	cmd.draw(4);

	struct Fog
	{
		mat4 inv_view_proj;
		vec4 camera_pos;
		vec4 color_falloff;
	} fog;

	fog.inv_view_proj = app->context.get_render_parameters().inv_view_projection;
	fog.camera_pos = vec4(app->context.get_render_parameters().camera_position, 0.0f);
	fog.color_falloff = vec4(app->context.get_fog_parameters().color, app->context.get_fog_parameters().falloff);
	cmd.push_constants(&fog, 0, sizeof(fog));

	cmd.set_blend_factors(VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_SRC_ALPHA);
	program = device.get_shader_manager().register_graphics("assets://shaders/lights/fog.vert", "assets://shaders/lights/fog.frag");
	variant = program->register_variant({});
	cmd.set_program(*program->get_program(variant));
	cmd.draw(4);
}

void SceneViewerApplication::UIImpl::build_render_pass(RenderPass &, Vulkan::CommandBuffer &cmd)
{
	UI::UIManager::get().render(cmd);
}

SceneViewerApplication::SceneViewerApplication(const std::string &path, unsigned width, unsigned height)
	: Application(width, height),
	  gbuffer_impl(this),
	  lighting_impl(this),
	  ui_impl(this),
      depth_renderer(Renderer::Type::DepthOnly)
{
	scene_loader.load_scene(path);
	animation_system = scene_loader.consume_animation_system();

	auto &skybox = scene_loader.get_scene().get_entity_pool().get_component_group<SkyboxComponent>();
	if (!skybox.empty())
	{
		auto *skybox_component = get<0>(skybox.front());
		lighting_impl.skydome_reflection = skybox_component->reflection_path;
		lighting_impl.skydome_irradiance = skybox_component->irradiance_path;
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
	lighting_impl.on_device_created(device.get_device());
}

void SceneViewerApplication::on_device_destroyed(const Event &)
{
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

	AttachmentInfo backbuffer;
	AttachmentInfo emissive, albedo, normal, pbr, depth;
	emissive.format = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
	albedo.format = VK_FORMAT_R8G8B8A8_SRGB;
	normal.format = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
	pbr.format = VK_FORMAT_R8G8_UNORM;
	depth.format = swap.get_device().get_default_depth_stencil_format();

	auto &gbuffer = graph.add_pass("gbuffer", VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
	gbuffer.add_color_output("emissive", emissive);
	gbuffer.add_color_output("albedo", albedo);
	gbuffer.add_color_output("normal", normal);
	gbuffer.add_color_output("pbr", pbr);
	gbuffer.set_depth_stencil_output("depth", depth);
	gbuffer.set_implementation(&gbuffer_impl);

	auto &lighting = graph.add_pass("lighting", VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
	lighting.add_color_output("HDR", emissive, "emissive");
	lighting.add_attachment_input("albedo");
	lighting.add_attachment_input("normal");
	lighting.add_attachment_input("pbr");
	lighting.add_attachment_input("depth");
	lighting.set_depth_stencil_input("depth");
	lighting.set_implementation(&lighting_impl);

	TonemapPass::setup_hdr_postprocess(graph, "HDR", "tonemapped");

	auto &ui = graph.add_pass("ui", VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
	ui.add_color_output("backbuffer", backbuffer, "tonemapped");
	ui.set_implementation(&ui_impl);

	graph.bake();
	graph.log();
	graph.install_physical_buffers(move(physical_buffers));
}

void SceneViewerApplication::on_swapchain_destroyed(const Event &)
{
}

void SceneViewerApplication::update_shadow_map()
{
	auto &device = get_wsi().get_device();
	auto &scene = scene_loader.get_scene();
	visible.clear();

	// Get the scene AABB for shadow casters.
	auto &shadow_casters = scene.get_entity_pool().get_component_group<CachedSpatialTransformComponent, RenderableComponent, CastsShadowComponent>();
	AABB aabb(vec3(FLT_MAX), vec3(-FLT_MAX));
	for (auto &caster : shadow_casters)
		aabb.expand(get<0>(caster)->world_aabb);

	mat4 view = mat4_cast(look_at(-light_direction(), vec3(0.0f, 1.0f, 0.0f)));

	// Project the scene AABB into the light and find our ortho ranges.
	AABB ortho_range = aabb.transform(view);
	mat4 proj = ortho(ortho_range);

	// Standard scale/bias.
	lighting_impl.shadow_transform = glm::translate(vec3(0.5f, 0.5f, 0.0f)) * glm::scale(vec3(0.5f, 0.5f, 1.0f)) * proj * view;
	context.set_camera(proj, view);

	depth_renderer.begin();
	scene.gather_visible_shadow_renderables(context.get_visibility_frustum(), visible);
	depth_renderer.push_renderables(context, visible);

	auto image_info = ImageCreateInfo::render_target(4096, 4096, VK_FORMAT_D16_UNORM);
	image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	image_info.initial_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	lighting_impl.shadow_map = device.create_image(image_info, nullptr);

	auto cmd = device.request_command_buffer();
	RenderPassInfo rp;
	rp.num_color_attachments = 0;
	rp.depth_stencil = &lighting_impl.shadow_map->get_view();
	rp.clear_depth_stencil.depth = 1.0f;
	rp.op_flags =
		RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT |
		RENDER_PASS_OP_STORE_DEPTH_STENCIL_BIT |
		RENDER_PASS_OP_DEPTH_STENCIL_OPTIMAL_BIT;

	cmd->begin_render_pass(rp);
	depth_renderer.flush(*cmd, context);
	cmd->end_render_pass();
	cmd->image_barrier(*lighting_impl.shadow_map,
	                   VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

	lighting_impl.shadow_map->set_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	device.submit(cmd);
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

	if (!lighting_impl.shadow_map)
		update_shadow_map();

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
	graph.enqueue_render_passes(device);
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
