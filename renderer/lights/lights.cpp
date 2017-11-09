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

#include "lights.hpp"
#include "render_queue.hpp"
#include "render_context.hpp"
#include "shader_suite.hpp"
#include "device.hpp"
#include "mesh_util.hpp"
#include "clusterer.hpp"
#include <atomic>

using namespace Vulkan;
using namespace Util;

namespace Granite
{
struct LightCookie
{
	LightCookie()
	{
		count.store(0);
	}

	unsigned get_cookie()
	{
		return count.fetch_add(1, std::memory_order_relaxed) + 1;
	}

	std::atomic_uint count;
};
static LightCookie light_cookie;

PositionalLight::PositionalLight(Type type)
	: type(type)
{
	cookie = light_cookie.get_cookie();
}

void PositionalLight::set_color(vec3 color)
{
	this->color = color;
	recompute_range();
}

void PositionalLight::set_falloff(float constant, float linear, float quadratic)
{
	this->constant = max(constant, 0.0f);
	this->linear = max(linear, 0.0f);
	this->quadratic = max(quadratic, 0.0f);
	recompute_range();
}

void PositionalLight::set_maximum_range(float range)
{
	cutoff_range = range;
	recompute_range();
}

void PositionalLight::recompute_range()
{
	if (linear == 0.0f && quadratic == 0.0f)
	{
		set_range(cutoff_range);
		return;
	}

	// Check when attenuation drops below a constant.
	const float target_atten = 0.1f;
	float max_color = max(max(color.r, color.g), color.b);

	if (max_color < target_atten * constant)
	{
		set_range(0.0001f);
		return;
	}

	float a = quadratic;
	float b = linear;
	float c = constant - max_color / target_atten;
	float d = (-b + sqrt(b * b - 4.0f * a * c)) / (2.0f * a);
	set_range(d);
}

void SpotLight::set_spot_parameters(float inner_cone, float outer_cone)
{
	this->inner_cone = clamp(inner_cone, 0.001f, 1.0f);
	this->outer_cone = clamp(outer_cone, 0.001f, 1.0f);
	recompute_range();
}

void SpotLight::set_range(float range)
{
	falloff_range = range;

	float max_range = min(falloff_range, cutoff_range);
	float min_z = -max_range;
	float xy = sqrt(1.0f - outer_cone * outer_cone) / outer_cone;
	xy_range = xy;
	xy *= max_range;
	aabb = AABB(vec3(-xy, -xy, min_z), vec3(xy, xy, 0.0f));
}

void SpotLight::set_shadow_info(const Vulkan::ImageView *shadow, const mat4 &transform)
{
	atlas = shadow;
	shadow_transform = transform;
}

PositionalFragmentInfo SpotLight::get_shader_info(const mat4 &transform) const
{
	// If the point light node has been scaled, renormalize this.
	// This assumes a uniform scale.
	float scale_factor = length(transform[0]);

	// This assumes a uniform scale.
	float max_range = min(falloff_range, cutoff_range) * scale_factor;

	return {
		vec4(color, outer_cone),
		vec4(constant, linear / scale_factor, quadratic / (scale_factor * scale_factor), 1.0f / max_range),
		vec4(transform[3].xyz(), inner_cone),
		vec4(-normalize(transform[2].xyz()), atan(xy_range)),
	};
}

SpotLight::SpotLight()
	: PositionalLight(PositionalLight::Type::Spot)
{
}

struct PositionalLightRenderInfo
{
	Program *program = nullptr;
	const Buffer *vbo = nullptr;
	const Buffer *ibo = nullptr;
	unsigned count = 0;

	const Vulkan::ImageView *atlas = nullptr;
	PositionalLight::Type type;

	struct Push
	{
		mat4 inv_view_projection;
		vec4 camera_pos;
		vec2 inv_resolution;
	} push;
};

struct PositionalVertexInfo
{
	mat4 model;
};

struct PositionalShaderInfo
{
	PositionalVertexInfo vertex;
	PositionalFragmentInfo fragment;
	union
	{
		mat4 shadow_transform;
		PointTransform point_transform;
	} u;
};

struct LightMesh : public EventHandler
{
	LightMesh()
	{
		EVENT_MANAGER_REGISTER_LATCH(LightMesh, on_device_created, on_device_destroyed, DeviceCreatedEvent);
	}

	BufferHandle spot_vbo;
	BufferHandle spot_ibo;
	unsigned spot_count = 0;

	BufferHandle point_vbo;
	BufferHandle point_ibo;
	unsigned point_count = 0;

	void create_point_mesh(const DeviceCreatedEvent &e)
	{
		auto mesh = create_sphere_mesh(3);

		BufferCreateInfo info = {};
		info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		info.size = mesh.positions.size() * sizeof(mesh.positions[0]);
		info.domain = BufferDomain::Device;
		point_vbo = e.get_device().create_buffer(info, mesh.positions.data());

		info.size = mesh.indices.size() * sizeof(uint16_t);
		info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
		point_ibo = e.get_device().create_buffer(info, mesh.indices.data());

		point_count = mesh.indices.size();
	}

	void create_spot_mesh(const DeviceCreatedEvent &e)
	{
		vec3 positions[17 + 2];
		positions[0] = vec3(0.0f);
		positions[1] = vec3(0.0f, 0.0f, -1.0f);

		float half_angle = 2.0f * pi<float>() / 32.0f;
		float padding_mod = 1.0f / cos(half_angle);

		for (unsigned i = 0; i <= 16; i++)
		{
			float rad = 2.0f * pi<float>() * float(i) / 16.0f;
			positions[i + 2] = vec3(padding_mod * cos(rad), padding_mod * sin(rad), -1.0f);
		}

		std::vector<uint16_t> indices;
		indices.reserve(2 * 3 * 16);
		for (unsigned i = 0; i < 16; i++)
		{
			indices.push_back(0);
			indices.push_back((i & 15) + 2);
			indices.push_back(((i + 1) & 15) + 2);
		}

		for (unsigned i = 0; i < 16; i++)
		{
			indices.push_back(1);
			indices.push_back(((i + 1) & 15) + 2);
			indices.push_back((i & 15) + 2);
		}

		spot_count = indices.size();

		BufferCreateInfo info = {};
		info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		info.size = sizeof(positions);
		info.domain = BufferDomain::Device;
		spot_vbo = e.get_device().create_buffer(info, positions);

		info.size = indices.size() * sizeof(uint16_t);
		info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
		spot_ibo = e.get_device().create_buffer(info, indices.data());
	}

	void on_device_created(const Vulkan::DeviceCreatedEvent &e)
	{
		create_spot_mesh(e);
		create_point_mesh(e);
	}

	void on_device_destroyed(const DeviceCreatedEvent &)
	{
		spot_vbo.reset();
		spot_ibo.reset();
		point_vbo.reset();
		point_ibo.reset();
	}
};
static LightMesh light_mesh;

static void positional_render_full_screen(CommandBuffer &cmd, const RenderQueueData *infos, unsigned num_instances)
{
	auto &light_info = *static_cast<const PositionalLightRenderInfo *>(infos[0].render_info);
	cmd.set_program(*light_info.program);
	CommandBufferUtil::set_quad_vertex_state(cmd);
	cmd.set_cull_mode(VK_CULL_MODE_NONE);
	cmd.set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);

	auto push = light_info.push;
	push.inv_resolution = vec2(1.0f / cmd.get_viewport().width, 1.0f / cmd.get_viewport().height);
	cmd.push_constants(&push, 0, sizeof(push));

	if (light_info.atlas)
		cmd.set_texture(2, 2, *light_info.atlas, Vulkan::StockSampler::LinearShadow);

	for (unsigned i = 0; i < num_instances; )
	{
		unsigned to_render = min(256u, num_instances - i);

		auto *frag = cmd.allocate_typed_constant_data<PositionalFragmentInfo>(2, 0, to_render);
		auto *vert = cmd.allocate_typed_constant_data<PositionalVertexInfo>(2, 1, to_render);

		for (unsigned j = 0; j < to_render; j++)
		{
			vert[j] = static_cast<const PositionalShaderInfo *>(infos[i + j].instance_data)->vertex;
			frag[j] = static_cast<const PositionalShaderInfo *>(infos[i + j].instance_data)->fragment;
		}

		if (light_info.atlas)
		{
			if (light_info.type == PositionalLight::Type::Spot)
			{
				auto *t = cmd.allocate_typed_constant_data<mat4>(2, 3, to_render);
				for (unsigned j = 0; j < to_render; j++)
					t[j] = static_cast<const PositionalShaderInfo *>(infos[i + j].instance_data)->u.shadow_transform;
			}
			else
			{
				auto *t = cmd.allocate_typed_constant_data<PointTransform>(2, 3, to_render);
				for (unsigned j = 0; j < to_render; j++)
					t[j] = static_cast<const PositionalShaderInfo *>(infos[i + j].instance_data)->u.point_transform;
			}
		}

		cmd.draw(4, to_render);
		i += to_render;
	}
}

static void positional_render_common(CommandBuffer &cmd, const RenderQueueData *infos, unsigned num_instances)
{
	auto &light_info = *static_cast<const PositionalLightRenderInfo *>(infos[0].render_info);
	cmd.set_program(*light_info.program);
	cmd.set_vertex_binding(0, *light_info.vbo, 0, sizeof(vec3));
	cmd.set_vertex_attrib(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0);
	cmd.set_index_buffer(*light_info.ibo, 0, VK_INDEX_TYPE_UINT16);

	if (light_info.type == PositionalLight::Type::Spot)
	{
		cmd.set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
		cmd.set_primitive_restart(false);
	}
	else
	{
		cmd.set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
		cmd.set_primitive_restart(true);
	}

	auto push = light_info.push;
	push.inv_resolution = vec2(1.0f / cmd.get_viewport().width, 1.0f / cmd.get_viewport().height);
	cmd.push_constants(&push, 0, sizeof(push));

	if (light_info.atlas)
		cmd.set_texture(2, 2, *light_info.atlas, Vulkan::StockSampler::LinearShadow);

	for (unsigned i = 0; i < num_instances; )
	{
		unsigned to_render = min(256u, num_instances - i);
		auto *frag = cmd.allocate_typed_constant_data<PositionalFragmentInfo>(2, 0, to_render);
		auto *vert = cmd.allocate_typed_constant_data<PositionalVertexInfo>(2, 1, to_render);

		for (unsigned j = 0; j < to_render; j++)
		{
			vert[j] = static_cast<const PositionalShaderInfo *>(infos[i + j].instance_data)->vertex;
			frag[j] = static_cast<const PositionalShaderInfo *>(infos[i + j].instance_data)->fragment;
		}

		if (light_info.atlas)
		{
			if (light_info.type == PositionalLight::Type::Spot)
			{
				auto *t = cmd.allocate_typed_constant_data<mat4>(2, 3, to_render);
				for (unsigned j = 0; j < to_render; j++)
					t[j] = static_cast<const PositionalShaderInfo *>(infos[i + j].instance_data)->u.shadow_transform;
			}
			else
			{
				auto *t = cmd.allocate_typed_constant_data<PointTransform>(2, 3, to_render);
				for (unsigned j = 0; j < to_render; j++)
					t[j] = static_cast<const PositionalShaderInfo *>(infos[i + j].instance_data)->u.point_transform;
			}
		}

		cmd.draw_indexed(light_info.count, to_render);
		i += to_render;
	}
}

static void positional_render_front(CommandBuffer &cmd, const RenderQueueData *infos, unsigned num_instances)
{
	cmd.set_cull_mode(VK_CULL_MODE_BACK_BIT);
	positional_render_common(cmd, infos, num_instances);
}

static void positional_render_back(CommandBuffer &cmd, const RenderQueueData *infos, unsigned num_instances)
{
	cmd.set_cull_mode(VK_CULL_MODE_FRONT_BIT);
	cmd.set_depth_compare(VK_COMPARE_OP_GREATER);
	positional_render_common(cmd, infos, num_instances);
}

void SpotLight::get_render_info(const RenderContext &context, const CachedSpatialTransformComponent *transform,
                                RenderQueue &queue) const
{
	auto &params = context.get_render_parameters();
	auto &aabb = transform->world_aabb;
	float to_center = dot(aabb.get_center() - params.camera_position, params.camera_front);
	float radius = aabb.get_radius();
	float aabb_near = to_center - params.z_near - radius;
	float aabb_far = to_center + radius - params.z_far;

	RenderFunc func;

	if (aabb_near < 0.0f) // We risk clipping into the mesh, and since we can't rely on depthClamp, use backface.
	{
		if (aabb_far > 0.0f) // We risk clipping into far plane as well ... Use a full-screen quad.
			func = positional_render_full_screen;
		else
			func = positional_render_back;
	}
	else
		func = positional_render_front;

	Hasher h;
	h.u32(ecast(PositionalLight::Type::Spot));
	if (atlas)
		h.u64(atlas->get_cookie());
	else
		h.u64(0);

	auto instance_key = h.get();
	h.pointer(func);
	auto sorting_key = h.get();

	auto *spot = queue.allocate_one<PositionalShaderInfo>();

	float max_range = min(falloff_range, cutoff_range);
	spot->vertex.model = transform->transform->world_transform * scale(vec3(xy_range * max_range, xy_range * max_range, max_range));
	spot->fragment = get_shader_info(transform->transform->world_transform);
	spot->u.shadow_transform = shadow_transform;

	auto *spot_info = queue.push<PositionalLightRenderInfo>(Queue::Light, instance_key, sorting_key,
	                                                        func, spot);

	if (spot_info)
	{
		PositionalLightRenderInfo info;

		info.count = light_mesh.spot_count;
		info.vbo = light_mesh.spot_vbo.get();
		info.ibo = light_mesh.spot_ibo.get();

		info.push.inv_view_projection = params.inv_view_projection;
		info.push.camera_pos = vec4(params.camera_position, 0.0f);
		info.atlas = atlas;
		info.type = PositionalLight::Type::Spot;

		unsigned variant = func == positional_render_full_screen ? 1 : 0;
		if (atlas)
			variant += 2;

		info.program = queue.get_shader_suites()[ecast(RenderableType::SpotLight)].get_program(DrawPipeline::AlphaBlend, 0, 0, variant).get();
		*spot_info = info;
	}
}

PointLight::PointLight()
	: PositionalLight(PositionalLight::Type::Point)
{
}

void PointLight::set_range(float range)
{
	falloff_range = range;
	float max_range = 1.15f * min(falloff_range, cutoff_range); // Fudge factor used in vertex shader.
	aabb = AABB(vec3(-max_range), vec3(max_range));
}

PositionalFragmentInfo PointLight::get_shader_info(const mat4 &transform) const
{
	// If the point light node has been scaled, renormalize this.
	// This assumes a uniform scale.
	float scale_factor = length(transform[0]);

	// This assumes a uniform scale.
	float max_range = min(falloff_range, cutoff_range) * scale_factor;

	return {
		vec4(color, 0.0f),
		vec4(constant, linear / scale_factor, quadratic / (scale_factor * scale_factor), 1.0f / max_range),
		vec4(transform[3].xyz(), 0.0f),
		vec4(normalize(transform[2].xyz()), 0.0f),
	};
}

void PointLight::set_shadow_info(const Vulkan::ImageView *shadow, const PointTransform &transform)
{
	shadow_atlas = shadow;
	shadow_transform = transform;
}

void PointLight::get_render_info(const RenderContext &context, const CachedSpatialTransformComponent *transform,
                                 RenderQueue &queue) const
{
	auto &params = context.get_render_parameters();
	auto &aabb = transform->world_aabb;
	float to_center = dot(aabb.get_center() - params.camera_position, params.camera_front);
	float radius = aabb.get_radius();
	float aabb_near = to_center - params.z_near - radius;
	float aabb_far = to_center + radius - params.z_far;

	RenderFunc func;

	if (aabb_near < 0.0f) // We risk clipping into the mesh, and since we can't rely on depthClamp, use backface.
	{
		if (aabb_far > 0.0f) // We risk clipping into far plane as well ... Use a full-screen quad.
			func = positional_render_full_screen;
		else
			func = positional_render_back;
	}
	else
		func = positional_render_front;

	Hasher h;
	h.u32(ecast(PositionalLight::Type::Point));
	if (shadow_atlas)
		h.u64(shadow_atlas->get_cookie());
	else
		h.u64(0);

	auto instance_key = h.get();
	h.pointer(func);
	auto sorting_key = h.get();

	auto *point = queue.allocate_one<PositionalShaderInfo>();

	point->vertex.model = transform->transform->world_transform * scale(vec3(min(falloff_range, cutoff_range)));
	point->fragment = get_shader_info(transform->transform->world_transform);
	point->u.point_transform = shadow_transform;

	auto *point_info = queue.push<PositionalLightRenderInfo>(Queue::Light, instance_key, sorting_key,
	                                                         func, point);

	if (point_info)
	{
		PositionalLightRenderInfo info;

		info.count = light_mesh.point_count;
		info.vbo = light_mesh.point_vbo.get();
		info.ibo = light_mesh.point_ibo.get();

		info.push.inv_view_projection = params.inv_view_projection;
		info.push.camera_pos = vec4(params.camera_position, 0.0f);
		info.atlas = shadow_atlas;
		info.type = PositionalLight::Type::Point;

		unsigned variant = func == positional_render_full_screen ? 1 : 0;
		if (shadow_atlas)
			variant += 2;

		info.program = queue.get_shader_suites()[ecast(RenderableType::PointLight)].get_program(DrawPipeline::AlphaBlend, 0, 0, variant).get();
		*point_info = info;
	}
}

}