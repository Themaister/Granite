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

#include "renderer.hpp"
#include "device.hpp"
#include "render_context.hpp"
#include "sprite.hpp"

using namespace Vulkan;
using namespace Util;
using namespace std;

namespace Granite
{

Renderer::Renderer(RendererType type)
	: type(type)
{
	EVENT_MANAGER_REGISTER_LATCH(Renderer, on_device_created, on_device_destroyed, DeviceCreatedEvent);

	if (type == RendererType::GeneralDeferred || type == RendererType::GeneralForward)
		set_mesh_renderer_options(SHADOW_CASCADE_ENABLE_BIT | SHADOW_ENABLE_BIT | FOG_ENABLE_BIT | ENVIRONMENT_ENABLE_BIT);
	else
		set_mesh_renderer_options(0);
}

void Renderer::set_mesh_renderer_options(RendererOptionFlags flags)
{
	if (renderer_options != flags)
	{
		vector<pair<string, int>> global_defines;
		if (flags & SHADOW_ENABLE_BIT)
			global_defines.push_back({ "SHADOWS", 1 });
		if (flags & SHADOW_CASCADE_ENABLE_BIT)
			global_defines.push_back({ "SHADOW_CASCADES", 1 });
		if (flags & FOG_ENABLE_BIT)
			global_defines.push_back({ "FOG", 1 });
		if (flags & ENVIRONMENT_ENABLE_BIT)
			global_defines.push_back({ "ENVIRONMENT", 1 });
		if (flags & REFRACTION_ENABLE_BIT)
			global_defines.push_back({ "REFRACTION", 1 });

		switch (type)
		{
		case RendererType::GeneralForward:
			global_defines.push_back({ "RENDERER_FORWARD", 1 });
			break;

		case RendererType::GeneralDeferred:
			global_defines.push_back({ "RENDERER_DEFERRED", 1 });
			break;

		case RendererType::DepthOnly:
			global_defines.push_back({ "RENDERER_DEPTH", 1 });
			break;

		default:
			break;
		}

		auto &meshes = suite[ecast(RenderableType::Mesh)];
		meshes.get_base_defines() = global_defines;
		meshes.bake_base_defines();
		auto &ground = suite[ecast(RenderableType::Ground)];
		ground.get_base_defines() = global_defines;
		ground.bake_base_defines();
		auto &plane = suite[ecast(RenderableType::TexturePlane)];
		plane.get_base_defines() = global_defines;
		plane.bake_base_defines();

		renderer_options = flags;
	}
}

void Renderer::on_device_created(const DeviceCreatedEvent &created)
{
	auto &device = created.get_device();

	if (type == RendererType::GeneralDeferred || type == RendererType::GeneralForward)
	{
		suite[ecast(RenderableType::Mesh)].init_graphics(&device.get_shader_manager(),
		                                                 "builtin://shaders/static_mesh.vert",
		                                                 "builtin://shaders/static_mesh.frag");
		suite[ecast(RenderableType::DebugMesh)].init_graphics(&device.get_shader_manager(),
		                                                      "builtin://shaders/debug_mesh.vert",
		                                                      "builtin://shaders/debug_mesh.frag");
		suite[ecast(RenderableType::Skybox)].init_graphics(&device.get_shader_manager(), "builtin://shaders/skybox.vert",
		                                                   "builtin://shaders/skybox.frag");
		suite[ecast(RenderableType::Ground)].init_graphics(&device.get_shader_manager(), "builtin://shaders/ground.vert",
		                                                   "builtin://shaders/ground.frag");
		suite[ecast(RenderableType::TexturePlane)].init_graphics(&device.get_shader_manager(),
		                                                         "builtin://shaders/texture_plane.vert",
		                                                         "builtin://shaders/texture_plane.frag");
	}
	else if (type == RendererType::DepthOnly)
	{
		suite[ecast(RenderableType::Mesh)].init_graphics(&device.get_shader_manager(),
		                                                 "builtin://shaders/static_mesh.vert",
		                                                 "builtin://shaders/static_mesh_depth.frag");
		suite[ecast(RenderableType::Ground)].init_graphics(&device.get_shader_manager(), "builtin://shaders/ground.vert",
		                                                   "builtin://shaders/dummy.frag");
		suite[ecast(RenderableType::TexturePlane)].init_graphics(&device.get_shader_manager(), "builtin://shaders/texture_plane.vert",
		                                                         "builtin://shaders/dummy.frag");
	}

	for (auto &s : suite)
		s.bake_base_defines();
	this->device = &device;
}

void Renderer::on_device_destroyed(const DeviceCreatedEvent &)
{
}

void Renderer::begin()
{
	queue.reset();
	queue.set_shader_suites(suite);
}

void Renderer::set_lighting_parameters(Vulkan::CommandBuffer &cmd, const RenderContext &context)
{
	auto *lighting = context.get_lighting_parameters();
	assert(lighting);

	auto *environment = static_cast<EnvironmentParameters *>(cmd.allocate_constant_data(0, 1, sizeof(EnvironmentParameters)));
	*environment = lighting->environment;

	auto *fog = static_cast<FogParameters *>(cmd.allocate_constant_data(0, 2, sizeof(FogParameters)));
	*fog = lighting->fog;

	auto *shadow = static_cast<ShadowParameters *>(cmd.allocate_constant_data(0, 3, sizeof(ShadowParameters)));
	*shadow = lighting->shadow;

	auto *directional = static_cast<DirectionalParameters *>(cmd.allocate_constant_data(0, 4, sizeof(DirectionalParameters)));
	*directional = lighting->directional;

	auto *refraction = static_cast<RefractionParameters *>(cmd.allocate_constant_data(0, 5, sizeof(RefractionParameters)));
	*refraction = lighting->refraction;

	auto *resolution = static_cast<ResolutionParameters *>(cmd.allocate_constant_data(0, 6, sizeof(ResolutionParameters)));
	resolution->resolution = vec2(cmd.get_viewport().width, cmd.get_viewport().height);
	resolution->inv_resolution = vec2(1.0f / cmd.get_viewport().width, 1.0f / cmd.get_viewport().height);

	if (lighting->environment_radiance != nullptr)
		cmd.set_texture(1, 0, *lighting->environment_radiance, Vulkan::StockSampler::LinearClamp);
	if (lighting->environment_irradiance != nullptr)
		cmd.set_texture(1, 1, *lighting->environment_irradiance, Vulkan::StockSampler::LinearClamp);
	if (lighting->shadow_far != nullptr)
		cmd.set_texture(1, 2, *lighting->shadow_far, Vulkan::StockSampler::LinearShadow);
	if (lighting->shadow_near != nullptr)
		cmd.set_texture(1, 3, *lighting->shadow_near, Vulkan::StockSampler::LinearShadow);
}

void Renderer::flush(Vulkan::CommandBuffer &cmd, RenderContext &context)
{
	auto *global = static_cast<RenderParameters *>(cmd.allocate_constant_data(0, 0, sizeof(RenderParameters)));
	*global = context.get_render_parameters();
	if (type == RendererType::GeneralForward)
		set_lighting_parameters(cmd, context);

	queue.sort();

	cmd.set_opaque_state();

	if (type == RendererType::DepthOnly)
	{
		cmd.set_depth_bias(true);
		cmd.set_depth_bias(1.0f, 1.0f);
		cmd.set_cull_mode(VK_CULL_MODE_FRONT_BIT);
	}

	CommandBufferSavedState state;
	cmd.save_state(COMMAND_BUFFER_SAVED_SCISSOR_BIT | COMMAND_BUFFER_SAVED_VIEWPORT_BIT | COMMAND_BUFFER_SAVED_RENDER_STATE_BIT, state);
	// No need to spend write bandwidth on writing 0 to light buffer, render opaque emissive on top.
	queue.dispatch(Queue::Opaque, cmd, &state);
	queue.dispatch(Queue::OpaqueEmissive, cmd, &state);

	if (type == RendererType::GeneralForward)
		queue.dispatch(Queue::Transparent, cmd, &state);
}

DebugMeshInstanceInfo &Renderer::render_debug(RenderContext &context, unsigned count)
{
	DebugMeshInfo debug;

	auto *instance_data = queue.allocate_one<DebugMeshInstanceInfo>();
	instance_data->count = count;
	instance_data->colors = queue.allocate_many<vec4>(count);
	instance_data->positions = queue.allocate_many<vec3>(count);

	Hasher hasher;
	hasher.string("debug");
	auto instance_key = hasher.get();
	auto sorting_key = RenderInfo::get_sort_key(context, Queue::Opaque, hasher.get(), hasher.get(), vec3(0.0f));
	debug.MVP = context.get_render_parameters().view_projection;

	auto *debug_info = queue.push<DebugMeshInfo>(Queue::Opaque, instance_key, sorting_key,
	                                             RenderFunctions::debug_mesh_render,
	                                             instance_data);

	if (debug_info)
	{
		debug.program = suite[ecast(RenderableType::DebugMesh)].get_program(DrawPipeline::Opaque,
		                                                                    MESH_ATTRIBUTE_POSITION_BIT |
		                                                                    MESH_ATTRIBUTE_VERTEX_COLOR_BIT, 0).get();
		*debug_info = debug;
	}

	return *instance_data;
}

template <typename T>
inline void dump_debug_coords(vec3 *pos, const T &t)
{
	*pos++ = t.get_coord(0.0f, 0.0f, 0.0f);
	*pos++ = t.get_coord(1.0f, 0.0f, 0.0f);
	*pos++ = t.get_coord(1.0f, 0.0f, 0.0f);
	*pos++ = t.get_coord(1.0f, 0.0f, 1.0f);
	*pos++ = t.get_coord(1.0f, 0.0f, 1.0f);
	*pos++ = t.get_coord(0.0f, 0.0f, 1.0f);
	*pos++ = t.get_coord(0.0f, 0.0f, 1.0f);
	*pos++ = t.get_coord(0.0f, 0.0f, 0.0f);

	*pos++ = t.get_coord(0.0f, 1.0f, 0.0f);
	*pos++ = t.get_coord(1.0f, 1.0f, 0.0f);
	*pos++ = t.get_coord(1.0f, 1.0f, 0.0f);
	*pos++ = t.get_coord(1.0f, 1.0f, 1.0f);
	*pos++ = t.get_coord(1.0f, 1.0f, 1.0f);
	*pos++ = t.get_coord(0.0f, 1.0f, 1.0f);
	*pos++ = t.get_coord(0.0f, 1.0f, 1.0f);
	*pos++ = t.get_coord(0.0f, 1.0f, 0.0f);

	*pos++ = t.get_coord(0.0f, 0.0f, 0.0f);
	*pos++ = t.get_coord(0.0f, 1.0f, 0.0f);
	*pos++ = t.get_coord(1.0f, 0.0f, 0.0f);
	*pos++ = t.get_coord(1.0f, 1.0f, 0.0f);
	*pos++ = t.get_coord(1.0f, 0.0f, 1.0f);
	*pos++ = t.get_coord(1.0f, 1.0f, 1.0f);
	*pos++ = t.get_coord(0.0f, 0.0f, 1.0f);
	*pos++ = t.get_coord(0.0f, 1.0f, 1.0f);
}

void Renderer::render_debug_frustum(RenderContext &context, const Frustum &frustum, const vec4 &color)
{
	auto &debug = render_debug(context, 12 * 2);
	for (unsigned i = 0; i < debug.count; i++)
		debug.colors[i] = color;
	dump_debug_coords(debug.positions, frustum);
}

void Renderer::render_debug_aabb(RenderContext &context, const AABB &aabb, const vec4 &color)
{
	auto &debug = render_debug(context, 12 * 2);
	for (unsigned i = 0; i < debug.count; i++)
		debug.colors[i] = color;
	dump_debug_coords(debug.positions, aabb);
}

void Renderer::push_renderables(RenderContext &context, const VisibilityList &visible)
{
	for (auto &vis : visible)
		vis.renderable->get_render_info(context, vis.transform, queue);
}

void DeferredLightRenderer::render_light(Vulkan::CommandBuffer &cmd, RenderContext &context)
{
	cmd.set_quad_state();
	cmd.set_input_attachments(0, 1);
	cmd.set_blend_enable(true);
	cmd.set_blend_factors(VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE);
	cmd.set_blend_op(VK_BLEND_OP_ADD);
	CommandBufferUtil::set_quad_vertex_state(cmd);

	auto &device = cmd.get_device();
	auto *program = device.get_shader_manager().register_graphics("builtin://shaders/lights/directional.vert",
	                                                              "builtin://shaders/lights/directional.frag");

	static const vector<pair<string, int>> defines = {
			{ "SHADOW_CASCADES", 1 },
			{ "ENVIRONMENT", 1 },
			{ "FOG", 1 },
			{ "SHADOWS", 1 },
	};

	unsigned variant = program->register_variant(defines);
	cmd.set_program(*program->get_program(variant));
	cmd.set_depth_test(true, false);
	cmd.set_depth_compare(VK_COMPARE_OP_GREATER);

	auto &light = *context.get_lighting_parameters();
	cmd.set_texture(1, 0, *light.environment_radiance, Vulkan::StockSampler::LinearClamp);
	cmd.set_texture(1, 1, *light.environment_irradiance, Vulkan::StockSampler::LinearClamp);
	cmd.set_texture(1, 2, *light.shadow_far, Vulkan::StockSampler::LinearShadow);
	cmd.set_texture(1, 3, *light.shadow_near, Vulkan::StockSampler::LinearShadow);

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

	mat4 total_shadow_transform = light.shadow.far_transform * context.get_render_parameters().inv_view_projection;
	mat4 total_shadow_transform_near = light.shadow.near_transform * context.get_render_parameters().inv_view_projection;

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
	push.direction_inv_cutoff = vec4(light.directional.direction, light.shadow.inv_cutoff_distance);
	push.camera_pos_mipscale = vec4(context.get_render_parameters().camera_position, mipscale);
	push.camera_front = context.get_render_parameters().camera_front;
	cmd.push_constants(&push, 0, sizeof(push));

	cmd.draw(4);

	// Skip fog for non-reflection passes.
	if (light.fog.falloff > 0.0f)
	{
		struct Fog
		{
			mat4 inv_view_proj;
			vec4 camera_pos;
			vec4 color_falloff;
		} fog;

		fog.inv_view_proj = context.get_render_parameters().inv_view_projection;
		fog.camera_pos = vec4(context.get_render_parameters().camera_position, 0.0f);
		fog.color_falloff = vec4(light.fog.color, light.fog.falloff);
		cmd.push_constants(&fog, 0, sizeof(fog));

		cmd.set_blend_factors(VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_SRC_ALPHA);
		program = device.get_shader_manager().register_graphics("builtin://shaders/lights/fog.vert",
		                                                        "builtin://shaders/lights/fog.frag");
		variant = program->register_variant({});
		cmd.set_program(*program->get_program(variant));
		cmd.draw(4);
	}
}
}
