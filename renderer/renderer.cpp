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

Renderer::Renderer(Type type)
	: type(type)
{
	EventManager::get_global().register_latch_handler(DeviceCreatedEvent::type_id,
                                                      &Renderer::on_device_created,
                                                      &Renderer::on_device_destroyed,
                                                      this);

	if (type == Type::GeneralDeferred || type == Type::GeneralForward)
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
		case Type::GeneralForward:
			global_defines.push_back({ "RENDERER_FORWARD", 1 });
			break;

		case Type::GeneralDeferred:
			global_defines.push_back({ "RENDERER_DEFERRED", 1 });
			break;

		case Type::DepthOnly:
			global_defines.push_back({ "RENDERER_DEPTH", 1 });
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

void Renderer::on_device_created(const Event &e)
{
	auto &created = e.as<DeviceCreatedEvent>();
	auto &device = created.get_device();

	if (type == Type::GeneralDeferred || type == Type::GeneralForward)
	{
		suite[ecast(RenderableType::Mesh)].init_graphics(&device.get_shader_manager(),
		                                                 "assets://shaders/static_mesh.vert",
		                                                 "assets://shaders/static_mesh.frag");
		suite[ecast(RenderableType::DebugMesh)].init_graphics(&device.get_shader_manager(),
		                                                      "assets://shaders/debug_mesh.vert",
		                                                      "assets://shaders/debug_mesh.frag");
		suite[ecast(RenderableType::Skybox)].init_graphics(&device.get_shader_manager(), "assets://shaders/skybox.vert",
		                                                   "assets://shaders/skybox.frag");
		suite[ecast(RenderableType::Ground)].init_graphics(&device.get_shader_manager(), "assets://shaders/ground.vert",
		                                                   "assets://shaders/ground.frag");
		suite[ecast(RenderableType::TexturePlane)].init_graphics(&device.get_shader_manager(),
		                                                         "assets://shaders/texture_plane.vert",
		                                                         "assets://shaders/texture_plane.frag");
	}
	else if (type == Type::DepthOnly)
	{
		suite[ecast(RenderableType::Mesh)].init_graphics(&device.get_shader_manager(),
		                                                 "assets://shaders/static_mesh.vert",
		                                                 "assets://shaders/static_mesh_depth.frag");
		suite[ecast(RenderableType::Ground)].init_graphics(&device.get_shader_manager(), "assets://shaders/ground.vert",
		                                                   "assets://shaders/ground_depth.frag");
	}

	for (auto &s : suite)
		s.bake_base_defines();
	this->device = &device;
}

void Renderer::on_device_destroyed(const Event &)
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

	if (lighting->environment_radiance)
		cmd.set_texture(1, 0, *lighting->environment_radiance, Vulkan::StockSampler::LinearClamp);
	if (lighting->environment_irradiance)
		cmd.set_texture(1, 1, *lighting->environment_irradiance, Vulkan::StockSampler::LinearClamp);
	if (lighting->shadow_far)
		cmd.set_texture(1, 2, *lighting->shadow_far, Vulkan::StockSampler::LinearShadow);
	if (lighting->shadow_near)
		cmd.set_texture(1, 3, *lighting->shadow_near, Vulkan::StockSampler::LinearShadow);
}

void Renderer::flush(Vulkan::CommandBuffer &cmd, RenderContext &context)
{
	auto *global = static_cast<RenderParameters *>(cmd.allocate_constant_data(0, 0, sizeof(RenderParameters)));
	*global = context.get_render_parameters();
	if (type == Type::GeneralForward)
		set_lighting_parameters(cmd, context);

	queue.sort();

	cmd.set_opaque_state();

	if (type == Type::DepthOnly)
	{
		cmd.set_depth_bias(true);
		cmd.set_depth_bias(1.0f, 1.0f);
		cmd.set_cull_mode(VK_CULL_MODE_FRONT_BIT);
	}

	CommandBufferSavedState state;
	cmd.save_state(COMMAND_BUFFER_SAVED_SCISSOR_BIT | COMMAND_BUFFER_SAVED_VIEWPORT_BIT | COMMAND_BUFFER_SAVED_RENDER_STATE_BIT, state);
	queue.dispatch(Queue::Opaque, cmd, &state);

	if (type == Type::GeneralForward)
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
}
