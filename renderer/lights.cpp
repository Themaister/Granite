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

using namespace Vulkan;
using namespace Util;

namespace Granite
{
PositionalLight::PositionalLight(Type type)
	: type(type)
{
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
	maximum_range = range;
	recompute_range();
}

void PositionalLight::recompute_range()
{
	if (linear == 0.0f && quadratic == 0.0f)
	{
		set_range(maximum_range);
		return;
	}

	// Check when attenuation drops below a constant.
	const float target_atten = 0.01f;
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
	this->range = range;
	float min_z = -range;
	float xy = range * sqrt(1.0f - outer_cone * outer_cone) / outer_cone;
	xy_range = xy;
	aabb = AABB(vec3(-xy, -xy, min_z), vec3(xy, xy, 0.0f));
}

PositionalFragmentInfo SpotLight::get_shader_info(const mat4 &transform) const
{
	return {
			vec4(color, outer_cone),
			vec4(constant, linear, quadratic, 1.0f / range),
			vec4(transform[3].xyz(), inner_cone),
			vec4(-transform[2].xyz(), xy_range),
	};
}

SpotLight::SpotLight()
	: PositionalLight(PositionalLight::Type::Spot)
{
}

struct SpotLightRenderInfo
{
	Program *program = nullptr;
	const Buffer *vbo = nullptr;
	const Buffer *ibo = nullptr;
	unsigned count = 0;
};

struct PositionalVertexInfo
{
	mat4 model;
};

struct PositionalShaderInfo
{
	PositionalVertexInfo vertex;
	PositionalFragmentInfo fragment;
};

struct LightMesh : public EventHandler
{
	LightMesh()
	{
		EVENT_MANAGER_REGISTER_LATCH(LightMesh, on_device_created, on_device_destroyed, DeviceCreatedEvent);
	}

	Vulkan::BufferHandle spot_vbo;
	Vulkan::BufferHandle spot_ibo;
	unsigned spot_count = 0;

	void on_device_created(const Vulkan::DeviceCreatedEvent &e)
	{
		static const vec3 positions[] = {
			vec3(0.0f, 0.0f, 0.0f),
			vec3(-1.0f, -1.0f, -1.0f),
			vec3(+1.0f, -1.0f, -1.0f),
			vec3(-1.0f, +1.0f, -1.0f),
			vec3(+1.0f, +1.0f, -1.0f),
		};

		static const uint16_t indices[] = {
			1, 0, 3,
			0, 2, 4,
			0, 4, 3,
			0, 1, 2,
			4, 2, 3,
			2, 1, 3,
		};

		spot_count = sizeof(indices) / sizeof(indices[0]);

		BufferCreateInfo info = {};
		info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		info.size = sizeof(positions);
		info.domain = BufferDomain::Device;
		spot_vbo = e.get_device().create_buffer(info, positions);

		info.size = sizeof(indices);
		info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
		spot_ibo = e.get_device().create_buffer(info, indices);
	};

	void on_device_destroyed(const DeviceCreatedEvent &)
	{
		spot_vbo.reset();
		spot_ibo.reset();
	}
};
static LightMesh light_mesh;

static void spot_render_full_screen(CommandBuffer &cmd, const RenderQueueData *infos, unsigned num_instances)
{
	auto &spot_info = *static_cast<const SpotLightRenderInfo *>(infos[0].render_info);
	cmd.set_program(*spot_info.program);
	CommandBufferUtil::set_quad_vertex_state(cmd);
	cmd.set_cull_mode(VK_CULL_MODE_NONE);
	cmd.set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);

	for (unsigned i = 0; i < num_instances; )
	{
		unsigned to_render = min(256u, num_instances - i);

		auto *frag = static_cast<PositionalFragmentInfo *>(cmd.allocate_constant_data(2, 0,
		                                                                              sizeof(PositionalFragmentInfo) * to_render));
		auto *vert = static_cast<PositionalVertexInfo *>(cmd.allocate_constant_data(2, 1,
		                                                                            sizeof(PositionalVertexInfo) * to_render));

		for (unsigned j = 0; j < to_render; j++)
		{
			vert[j] = static_cast<const PositionalShaderInfo *>(infos[i + j].instance_data)->vertex;
			frag[j] = static_cast<const PositionalShaderInfo *>(infos[i + j].instance_data)->fragment;
		}

		cmd.draw(4, to_render);
		i += to_render;
	}
}

static void spot_render_common(CommandBuffer &cmd, const RenderQueueData *infos, unsigned num_instances)
{
	auto &spot_info = *static_cast<const SpotLightRenderInfo *>(infos[0].render_info);
	cmd.set_program(*spot_info.program);
	cmd.set_vertex_binding(0, *spot_info.vbo, 0, sizeof(vec3));
	cmd.set_vertex_attrib(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0);
	cmd.set_index_buffer(*spot_info.ibo, 0, VK_INDEX_TYPE_UINT16);

	for (unsigned i = 0; i < num_instances; )
	{
		unsigned to_render = min(256u, num_instances - i);
		auto *frag = static_cast<PositionalFragmentInfo *>(cmd.allocate_constant_data(2, 0,
		                                                                              sizeof(PositionalFragmentInfo) * to_render));
		auto *vert = static_cast<PositionalVertexInfo *>(cmd.allocate_constant_data(2, 1,
		                                                                            sizeof(PositionalVertexInfo) * to_render));

		for (unsigned j = 0; j < to_render; j++)
		{
			vert[j] = static_cast<const PositionalShaderInfo *>(infos[i + j].instance_data)->vertex;
			frag[j] = static_cast<const PositionalShaderInfo *>(infos[i + j].instance_data)->fragment;
		}

		cmd.draw_indexed(spot_info.count, to_render);
		i += to_render;
	}
}

static void spot_render_front(CommandBuffer &cmd, const RenderQueueData *infos, unsigned num_instances)
{
	cmd.set_cull_mode(VK_CULL_MODE_BACK_BIT);
	spot_render_common(cmd, infos, num_instances);
}

static void spot_render_back(CommandBuffer &cmd, const RenderQueueData *infos, unsigned num_instances)
{
	cmd.set_cull_mode(VK_CULL_MODE_FRONT_BIT);
	cmd.set_depth_compare(VK_COMPARE_OP_GREATER);
	spot_render_common(cmd, infos, num_instances);
}

void SpotLight::get_render_info(const RenderContext &context, const CachedSpatialTransformComponent *transform,
                                RenderQueue &queue) const
{
	SpotLightRenderInfo info;

	auto &params = context.get_render_parameters();
	auto &aabb = transform->world_aabb;
	float to_center = dot(aabb.get_center() - params.camera_position, params.camera_front);
	float radius = aabb.get_radius();
	float aabb_near = to_center - params.z_near - radius;
	float aabb_far = to_center + radius - params.z_far;

	info.count = light_mesh.spot_count;
	info.vbo = light_mesh.spot_vbo.get();
	info.ibo = light_mesh.spot_ibo.get();

	RenderFunc func;

	if (aabb_near < 0.0f) // We risk clipping into the mesh, and since we can't rely on depthClamp, use backface.
	{
		if (aabb_far > 0.0f) // We risk clipping into far plane as well ... Use a full-screen quad.
			func = spot_render_full_screen;
		else
			func = spot_render_back;
	}
	else
		func = spot_render_front;

	Hasher h;
	auto instance_key = h.get();
	h.pointer(func);
	auto sorting_key = h.get();

	auto *spot = queue.allocate_one<PositionalShaderInfo>();
	spot->vertex.model = transform->transform->world_transform * scale(vec3(xy_range, xy_range, range));
	spot->fragment = get_shader_info(transform->transform->world_transform);

	auto *spot_info = queue.push<SpotLightRenderInfo>(Queue::Light, instance_key, sorting_key,
	                                                  func, spot);

	if (spot_info)
	{
		info.program = queue.get_shader_suites()[ecast(RenderableType::SpotLight)].get_program(DrawPipeline::AlphaBlend, 0, 0).get();
		*spot_info = info;
	}
}

PointLight::PointLight()
	: PositionalLight(PositionalLight::Type::Point)
{
}

void PointLight::set_range(float range)
{
	this->range = range;
	aabb = AABB(vec3(-range), vec3(range));
}

PositionalFragmentInfo PointLight::get_shader_info(const mat4 &transform) const
{
	return {
			vec4(color, 0.0f),
			vec4(constant, linear, quadratic, 1.0f / range),
			vec4(transform[3].xyz(), 0.0f),
			vec4(-transform[2].xyz(), 0.0f),
	};
}

void PointLight::get_render_info(const RenderContext &context, const CachedSpatialTransformComponent *transform,
                                 RenderQueue &queue) const
{
	(void)context;
	(void)transform;
	(void)queue;
}

}