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
	variant = program->register_variant({});
}

void LightClusterer::on_device_destroyed(const Vulkan::DeviceCreatedEvent &)
{
	program = nullptr;
	variant = 0;
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

void LightClusterer::build_cluster(Vulkan::CommandBuffer &cmd, Vulkan::ImageView &view)
{
	point_count = 0;
	spot_count = 0;

	AABB aabb(vec3(FLT_MAX), vec3(-FLT_MAX));

	for (auto &light : *lights)
	{
		auto &l = *get<0>(light)->light;
		auto *transform = get<1>(light);

		if (l.get_type() == PositionalLight::Type::Spot && spot_count < MaxLights)
		{
			spot_lights[spot_count++] = static_cast<SpotLight &>(l).get_shader_info(transform->transform->world_transform);
			aabb.expand(transform->world_aabb);
		}
		else if (l.get_type() == PositionalLight::Type::Point && point_count < MaxLights)
		{
			point_lights[point_count++] = static_cast<PointLight &>(l).get_shader_info(transform->transform->world_transform);
			aabb.expand(transform->world_aabb);
		}
	}

	if (point_count || spot_count)
		cluster_transform = ortho(aabb);
	else
		cluster_transform = mat4(1.0f);

	auto inverse_cluster_transform = inverse(cluster_transform);

	cmd.set_program(*program->get_program(variant));
	cmd.set_storage_texture(0, 0, view);

	auto *point_buffer = static_cast<PositionalFragmentInfo *>(cmd.allocate_constant_data(1, 0, MaxLights * sizeof(PositionalFragmentInfo)));
	auto *spot_buffer = static_cast<PositionalFragmentInfo *>(cmd.allocate_constant_data(1, 1, MaxLights * sizeof(PositionalFragmentInfo)));
	memcpy(point_buffer, point_lights, point_count * sizeof(PositionalFragmentInfo));
	memcpy(spot_buffer, spot_lights, spot_count * sizeof(PositionalFragmentInfo));

	struct Push
	{
		mat4 inverse_cluster_transform;
		uvec4 size;
		vec4 inv_size;
		uint32_t spot_count;
		uint32_t point_count;
	};
	Push push = { inverse_cluster_transform, uvec4(x, y, z, 0), vec4(1.0f / x, 1.0f / y, 1.0f / z, 1.0f), spot_count, point_count };
	cmd.push_constants(&push, 0, sizeof(push));
	cmd.dispatch((x + 7) / 8, (y + 7) / 8, z);
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
	att.size_z = z;
	att.persistent = true;

	auto &pass = graph.add_pass("clustering", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
	pass.add_storage_texture_output("light-cluster", att);
	pass.set_build_render_pass([this](Vulkan::CommandBuffer &cmd) {
		build_cluster(cmd, *target);
	});
}

void LightClusterer::set_base_renderer(Renderer *)
{
}
}