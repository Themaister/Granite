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
#include "renderer.hpp"

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

	shadow_atlas.reset();
	shadow_atlas_point.reset();
	for (auto &rt : shadow_atlas_rt)
		rt.reset();
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

const mat4 *LightClusterer::get_active_spot_light_shadow_matrices() const
{
	return spot_light_shadow_transforms;
}

const vec4 *LightClusterer::get_active_point_light_shadow_transform() const
{
	return point_light_shadow_transforms;
}

const PositionalFragmentInfo *LightClusterer::get_active_spot_lights() const
{
	return spot_lights;
}

void LightClusterer::set_enable_clustering(bool enable)
{
	enable_clustering = enable;
}

void LightClusterer::set_enable_shadows(bool enable)
{
	enable_shadows = enable;
}

const Vulkan::ImageView *LightClusterer::get_cluster_image() const
{
	return enable_clustering ? target : nullptr;
}

const Vulkan::ImageView *LightClusterer::get_spot_light_shadows() const
{
	return (enable_shadows && shadow_atlas) ? &shadow_atlas->get_view() : nullptr;
}

const Vulkan::ImageView *LightClusterer::get_point_light_shadows() const
{
	return (enable_shadows && shadow_atlas_point) ? &shadow_atlas_point->get_view() : nullptr;
}

const mat4 &LightClusterer::get_cluster_transform() const
{
	return cluster_transform;
}

void LightClusterer::render_atlas_point(RenderContext &context)
{
	auto &device = context.get_device();
	auto cmd = device.request_command_buffer();

	if (!shadow_atlas_point)
	{
		ImageCreateInfo info = ImageCreateInfo::render_target(512, 512, VK_FORMAT_D16_UNORM);
		info.layers = 6 * MaxLights;
		info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
		info.initial_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		shadow_atlas_point = device.create_image(info, nullptr);

		for (unsigned i = 0; i < 6 * MaxLights; i++)
		{
			ImageViewCreateInfo view;
			view.image = shadow_atlas_point.get();
			view.layers = 1;
			view.base_layer = i;
			shadow_atlas_rt[i] = device.create_image_view(view);
		}
	}
	else
	{
		cmd->image_barrier(*shadow_atlas_point, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
		                   VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
		shadow_atlas_point->set_layout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
	}

	RenderContext depth_context;
	VisibilityList visible;

	for (unsigned i = 0; i < point_count; i++)
	{
		for (unsigned face = 0; face < 6; face++)
		{
			mat4 view, proj;
			compute_cube_render_transform(point_lights[i].position_inner.xyz(), face, proj, view,
			                              0.1f, 1.0f / point_lights[i].falloff_inv_radius.w);
			depth_context.set_camera(proj, view);

			if (face == 0)
			{
				point_light_shadow_transforms[i] = vec4(proj[2].zw(), proj[3].zw());
				point_light_handles[i]->set_shadow_info(&shadow_atlas_point->get_view(), point_light_shadow_transforms[i], i);
			}

			visible.clear();
			scene->gather_visible_static_shadow_renderables(depth_context.get_visibility_frustum(), visible);

			depth_renderer->begin();
			depth_renderer->push_depth_renderables(depth_context, visible);

			RenderPassInfo rp;
			rp.op_flags = RENDER_PASS_OP_DEPTH_STENCIL_OPTIMAL_BIT |
			              RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT |
			              RENDER_PASS_OP_STORE_DEPTH_STENCIL_BIT;
			rp.num_color_attachments = 0;
			rp.depth_stencil = shadow_atlas_rt[6 * i + face].get();
			rp.clear_depth_stencil.depth = 1.0f;
			cmd->begin_render_pass(rp);
			depth_renderer->flush(*cmd, depth_context, Renderer::FRONT_FACE_CLOCKWISE_BIT);
			cmd->end_render_pass();
		}
	}

	cmd->image_barrier(*shadow_atlas_point, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	                   VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
	                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
	shadow_atlas_point->set_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	device.submit(cmd);
}

void LightClusterer::render_atlas_spot(RenderContext &context)
{
	auto &device = context.get_device();
	auto cmd = device.request_command_buffer();

	if (!shadow_atlas)
	{
		ImageCreateInfo info = ImageCreateInfo::render_target(4096, 2048, VK_FORMAT_D16_UNORM);
		info.initial_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		shadow_atlas = device.create_image(info, nullptr);
	}
	else
	{
		cmd->image_barrier(*shadow_atlas, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
		                   VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
		shadow_atlas->set_layout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
	}

	RenderContext depth_context;
	VisibilityList visible;

	for (unsigned i = 0; i < spot_count; i++)
	{
		float range = tan(spot_lights[i].direction_half_angle.w);
		mat4 view = mat4_cast(look_at_arbitrary_up(spot_lights[i].direction_half_angle.xyz())) *
		            translate(-spot_lights[i].position_inner.xyz());
		mat4 proj = projection(range * 2.0f, 1.0f, 0.1f, 1.0f / spot_lights[i].falloff_inv_radius.w);

		// Carve out the atlas region where the spot light shadows live.
		spot_light_shadow_transforms[i] =
				translate(vec3(float(i & 7) / 8.0f, float(i >> 3) / 4.0f, 0.0f)) *
				scale(vec3(1.0f / 8.0f, 1.0f / 4.0f, 1.0f)) *
				translate(vec3(0.5f, 0.5f, 0.0f)) *
				scale(vec3(0.5f, 0.5f, 1.0f)) *
				proj * view;

		spot_light_handles[i]->set_shadow_info(&shadow_atlas->get_view(), spot_light_shadow_transforms[i]);

		depth_context.set_camera(proj, view);
		visible.clear();
		scene->gather_visible_static_shadow_renderables(depth_context.get_visibility_frustum(), visible);

		depth_renderer->begin();
		depth_renderer->push_depth_renderables(depth_context, visible);

		RenderPassInfo rp;
		rp.op_flags = RENDER_PASS_OP_DEPTH_STENCIL_OPTIMAL_BIT |
		              RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT |
		              RENDER_PASS_OP_STORE_DEPTH_STENCIL_BIT;
		rp.num_color_attachments = 0;
		rp.depth_stencil = &shadow_atlas->get_view();
		rp.clear_depth_stencil.depth = 1.0f;
		rp.render_area.offset.x = 512 * (i & 7);
		rp.render_area.offset.y = 512 * (i >> 3);
		rp.render_area.extent.width = 512;
		rp.render_area.extent.height = 512;
		cmd->begin_render_pass(rp);
		cmd->set_viewport({ float(512 * (i & 7)), float(512 * (i >> 3)), 512.0f, 512.0f, 0.0f, 1.0f });
		cmd->set_scissor(rp.render_area);
		depth_renderer->flush(*cmd, depth_context);
		cmd->end_render_pass();
	}

	cmd->image_barrier(*shadow_atlas, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	                   VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
	                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
	shadow_atlas->set_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	device.submit(cmd);
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

		if (l.get_type() == PositionalLight::Type::Spot)
		{
			auto &spot = static_cast<SpotLight &>(l);
			spot.set_shadow_info(nullptr, {});
			if (spot_count < MaxLights)
			{
				spot_lights[spot_count] = spot.get_shader_info(transform->transform->world_transform);
				spot_light_handles[spot_count] = &spot;
#if 0
				vec3 center = spot_lights[spot_count].position_inner.xyz();
				float radius = 1.0f / spot_lights[spot_count].falloff_inv_radius.w;
				AABB point_aabb(center - radius, center + radius);
				aabb.expand(point_aabb);
#endif
				spot_count++;
			}
		}
		else if (l.get_type() == PositionalLight::Type::Point)
		{
			auto &point = static_cast<PointLight &>(l);
			point.set_shadow_info(nullptr, {}, 0);
			if (point_count < MaxLights)
			{
				point_lights[point_count] = point.get_shader_info(transform->transform->world_transform);
				point_light_handles[point_count] = &point;

#if 0
				vec3 center = point_lights[point_count].position_inner.xyz();
				float radius = 1.0f / point_lights[point_count].falloff_inv_radius.w;
				AABB point_aabb(center - radius, center + radius);
				aabb.expand(point_aabb);
#endif

				point_count++;
			}
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

	if (enable_shadows)
	{
		render_atlas_spot(context);
		render_atlas_point(context);
	}
	else
	{
		shadow_atlas.reset();
		shadow_atlas_point.reset();
	}
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

	prepass.set_need_render_pass([this]() {
		return enable_clustering;
	});

	auto &pass = graph.add_pass("clustering", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
	pass.add_storage_texture_output("light-cluster", att);
	pass.add_texture_input("light-cluster-prepass");
	pass.set_build_render_pass([this](Vulkan::CommandBuffer &cmd) {
		build_cluster(cmd, *target, pre_cull_target);
	});

	pass.set_need_render_pass([this]() {
		return enable_clustering;
	});
}

void LightClusterer::set_base_renderer(Renderer *, Renderer *, Renderer *depth)
{
	depth_renderer = depth;
}
}