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

#include "clusterer.hpp"
#include "render_graph.hpp"
#include "scene.hpp"
#include "render_context.hpp"

using namespace Vulkan;
using namespace std;

namespace Granite
{
LightClusterer::LightClusterer()
{
	EVENT_MANAGER_REGISTER_LATCH(LightClusterer, on_device_created, on_device_destroyed, DeviceCreatedEvent);
}

void LightClusterer::on_device_created(const Vulkan::DeviceCreatedEvent &e)
{
	auto &shader_manager = e.get_device().get_shader_manager();
	program = shader_manager.register_compute("builtin://shaders/lights/clustering.comp");
	inherit_variant = program->register_variant({{ "INHERIT", 1 }});
	cull_variant = program->register_variant({});
}

void LightClusterer::on_device_destroyed(const Vulkan::DeviceCreatedEvent &)
{
	program = nullptr;
	inherit_variant = 0;
	cull_variant = 0;
}

void LightClusterer::set_scene(Scene *scene)
{
	this->scene = scene;
	lights = &scene->get_entity_pool().get_component_group<PositionalLightComponent, CachedSpatialTransformComponent>();
}

void LightClusterer::set_resolution(unsigned x, unsigned y, unsigned z)
{
	this->x = x;
	this->y = y;
	this->z = z;
}

void LightClusterer::setup_render_pass_dependencies(RenderGraph &, RenderPass &target)
{
	// TODO: Other passes might want this?
	target.add_texture_input("light-cluster");
}

RendererType LightClusterer::get_renderer_type()
{
	return RendererType::External;
}

void LightClusterer::set_base_render_context(const RenderContext *context)
{
	this->context = context;
}

void LightClusterer::setup_render_pass_resources(RenderGraph &graph)
{
	target = &graph.get_physical_texture_resource(graph.get_texture_resource("light-cluster").get_physical_index());
	pre_cull_target = &graph.get_physical_texture_resource(graph.get_texture_resource("light-cluster-prepass").get_physical_index());
}

unsigned LightClusterer::get_active_point_light_count() const
{
	return point_count;
}

unsigned LightClusterer::get_active_spot_light_count() const
{
	return spot_count;
}

const PositionalFragmentInfo *LightClusterer::get_active_point_lights() const
{
	return point_lights;
}

const PositionalFragmentInfo *LightClusterer::get_active_spot_lights() const
{
	return spot_lights;
}

const Vulkan::ImageView &LightClusterer::get_cluster_image() const
{
	return *target;
}

const mat4 &LightClusterer::get_cluster_transform() const
{
	return cluster_transform;
}

void LightClusterer::refresh(RenderContext &context)
{
	point_count = 0;
	spot_count = 0;
	auto &frustum = context.get_visibility_frustum();

	for (auto &light : *lights)
	{
		auto &l = *get<0>(light)->light;
		auto *transform = get<1>(light);

		// Frustum cull lights here.
		if (!frustum.intersects(transform->world_aabb))
			continue;

		if (l.get_type() == PositionalLight::Type::Spot && spot_count < MaxLights)
		{
			spot_lights[spot_count] = static_cast<SpotLight &>(l).get_shader_info(transform->transform->world_transform);
#if 0
			vec3 center = spot_lights[spot_count].position_inner.xyz();
			float radius = 1.0f / spot_lights[spot_count].falloff_inv_radius.w;
			AABB point_aabb(center - radius, center + radius);
			aabb.expand(point_aabb);
#endif
			spot_count++;
		}
		else if (l.get_type() == PositionalLight::Type::Point && point_count < MaxLights)
		{
			point_lights[point_count] = static_cast<PointLight &>(l).get_shader_info(transform->transform->world_transform);

#if 0
			vec3 center = point_lights[point_count].position_inner.xyz();
			float radius = 1.0f / point_lights[point_count].falloff_inv_radius.w;
			AABB point_aabb(center - radius, center + radius);
			aabb.expand(point_aabb);
#endif

			point_count++;
		}
	}

	// Figure out aabb bounds in view space.
	auto &inv_proj = context.get_render_parameters().inv_projection;
	const auto project = [](const vec4 &v) -> vec3 {
		return v.xyz() / v.w;
	};

	vec3 ul = project(inv_proj * vec4(-1.0f, -1.0f, 1.0f, 1.0f));
	vec3 ll = project(inv_proj * vec4(-1.0f, +1.0f, 1.0f, 1.0f));
	vec3 ur = project(inv_proj * vec4(+1.0f, -1.0f, 1.0f, 1.0f));
	vec3 lr = project(inv_proj * vec4(+1.0f, +1.0f, 1.0f, 1.0f));

	vec3 min_view = min(min(ul, ll), min(ur, lr));
	vec3 max_view = max(max(ul, ll), max(ur, lr));
	// Make sure scaling the box does not move the near plane.
	max_view.z = 0.0f;

	mat4 ortho_box = ortho(AABB(min_view, max_view));

	if (point_count || spot_count)
		cluster_transform = ortho_box * context.get_render_parameters().view;
	else
		cluster_transform = mat4(1.0f);
}

void LightClusterer::build_cluster(Vulkan::CommandBuffer &cmd, Vulkan::ImageView &view, const Vulkan::ImageView *pre_culled)
{
	unsigned res_x = x;
	unsigned res_y = y;
	unsigned res_z = z;
	if (!pre_culled)
	{
		res_x /= ClusterPrepassDownsample;
		res_y /= ClusterPrepassDownsample;
		res_z /= ClusterPrepassDownsample;
	}

	cmd.set_program(*program->get_program(pre_culled ? inherit_variant : cull_variant));
	cmd.set_storage_texture(0, 0, view);
	if (pre_culled)
		cmd.set_texture(0, 1, *pre_culled, StockSampler::NearestWrap);

	auto *spot_buffer = static_cast<PositionalFragmentInfo *>(cmd.allocate_constant_data(1, 0, MaxLights * sizeof(PositionalFragmentInfo)));
	auto *point_buffer = static_cast<PositionalFragmentInfo *>(cmd.allocate_constant_data(1, 1, MaxLights * sizeof(PositionalFragmentInfo)));
	memcpy(spot_buffer, spot_lights, spot_count * sizeof(PositionalFragmentInfo));
	memcpy(point_buffer, point_lights, point_count * sizeof(PositionalFragmentInfo));

	struct Push
	{
		mat4 inverse_cluster_transform;
		uvec4 size_z_log2;
		vec4 inv_texture_size;
		vec4 inv_size_radius;
		uint32_t spot_count;
		uint32_t point_count;
	};

	auto inverse_cluster_transform = inverse(cluster_transform);

	vec3 inv_res = vec3(1.0f / res_x, 1.0f / res_y, 1.0f / res_z);
	float radius = 0.5f * length(mat3(inverse_cluster_transform) * (vec3(2.0f, 2.0f, 1.0f) * inv_res));

	Push push = {
			inverse_cluster_transform,
			uvec4(res_x, res_y, res_z, trailing_zeroes(res_z)),
			vec4(1.0f / res_x, 1.0f / res_y, 1.0f / (ClusterHierarchies * res_z), 1.0f),
			vec4(inv_res, radius),
			spot_count, point_count,
	};
	cmd.push_constants(&push, 0, sizeof(push));
	cmd.dispatch((res_x + 3) / 4, (res_y + 3) / 4, ClusterHierarchies * ((res_z + 3) / 4));
}

void LightClusterer::add_render_passes(RenderGraph &graph)
{
	AttachmentInfo att;
	att.levels = 1;
	att.layers = 1;
	att.format = VK_FORMAT_R32G32B32A32_UINT;
	att.samples = 1;
	att.size_class = SizeClass::Absolute;
	att.size_x = x;
	att.size_y = y;
	att.size_z = z * ClusterHierarchies;
	att.persistent = true;

	AttachmentInfo att_prepass = att;
	assert((x % ClusterPrepassDownsample) == 0);
	assert((y % ClusterPrepassDownsample) == 0);
	assert((z % ClusterPrepassDownsample) == 0);
	assert((z & (z - 1)) == 0);
	att_prepass.size_x /= ClusterPrepassDownsample;
	att_prepass.size_y /= ClusterPrepassDownsample;
	att_prepass.size_z /= ClusterPrepassDownsample;

	auto &prepass = graph.add_pass("clustering-prepass", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
	prepass.add_storage_texture_output("light-cluster-prepass", att_prepass);
	prepass.set_build_render_pass([this](Vulkan::CommandBuffer &cmd) {
		build_cluster(cmd, *pre_cull_target, nullptr);
	});

	auto &pass = graph.add_pass("clustering", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
	pass.add_storage_texture_output("light-cluster", att);
	pass.add_texture_input("light-cluster-prepass");
	pass.set_build_render_pass([this](Vulkan::CommandBuffer &cmd) {
		build_cluster(cmd, *target, pre_cull_target);
	});
}

void LightClusterer::set_base_renderer(Renderer *)
{
}
}