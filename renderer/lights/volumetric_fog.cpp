/* Copyright (c) 2017-2018 Hans-Kristian Arntzen
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

#include "volumetric_fog.hpp"
#include "render_graph.hpp"
#include "render_context.hpp"

using namespace Vulkan;
using namespace std;

namespace Granite
{
VolumetricFog::VolumetricFog()
{
	set_z_range(z_range);
}

void VolumetricFog::set_z_range(float range)
{
	z_range = range;
	slice_z_log2_scale = 1.0f / log2(1.0f + range);
}

void VolumetricFog::set_resolution(unsigned width, unsigned height, unsigned depth)
{
	this->width = width;
	this->height = height;
	this->depth = depth;
}

void VolumetricFog::add_texture_dependency(string name)
{
	texture_dependencies.push_back(move(name));
}

void VolumetricFog::compute_slice_extents()
{
	for (unsigned z = 0; z < depth; z++)
	{
		float end_z = exp2((z + 1.0f) / (depth * get_slice_z_log2_scale())) - 1.0f;
		float start_z = exp2(float(z) / (depth * get_slice_z_log2_scale())) - 1.0f;
		slice_extents[z] = end_z - start_z;
	}
}

void VolumetricFog::build_light_density(CommandBuffer &cmd, ImageView &light_density)
{
	struct Push
	{
		alignas(16) mat4 inv_view_projection;
		alignas(16) vec4 z_transform;
		alignas(16) uvec3 count;
		alignas(16) vec3 inv_resolution;
		alignas(8) vec2 xy_scale;
		alignas(4) float slice_z_log2_scale;
		alignas(4) float density_mod;
	} push;
	push.inv_view_projection = context->get_render_parameters().inv_view_projection;
	push.z_transform = vec4(context->get_render_parameters().projection[2].zw(),
	                        context->get_render_parameters().projection[3].zw());
	push.count = uvec3(width, height, depth);
	push.inv_resolution = vec3(1.0f / width, 1.0f / height, 1.0f / depth);
	push.xy_scale = vec2(context->get_render_parameters().inv_projection[0].x,
	                     context->get_render_parameters().inv_projection[1].y);
	push.slice_z_log2_scale = get_slice_z_log2_scale();
	push.density_mod = 0.01f;

	cmd.push_constants(&push, 0, sizeof(push));

	auto flags = Renderer::get_mesh_renderer_options_from_lighting(*context->get_lighting_parameters());
	flags &= ~Renderer::VOLUMETRIC_FOG_ENABLE_BIT;
	auto defines = Renderer::build_defines_from_renderer_options(RendererType::GeneralForward, flags);
	cmd.set_program("builtin://shaders/lights/fog_light_density.comp", defines);
	Renderer::bind_global_parameters(cmd, *context);
	Renderer::bind_lighting_parameters(cmd, *context);

	cmd.push_constants(&push, 0, sizeof(push));
	cmd.set_storage_texture(2, 0, light_density);
	memcpy(cmd.allocate_typed_constant_data<float>(2, 1, depth),
	       slice_extents,
	       sizeof(float) * depth);

	cmd.dispatch((width + 3) / 4, (height + 3) / 4, (depth + 3) / 4);
}

void VolumetricFog::build_fog(CommandBuffer &cmd, ImageView &fog, ImageView &light)
{
	cmd.set_program("builtin://shaders/lights/fog_accumulate.comp");
	struct Push
	{
		alignas(16) uvec3 count;
	} push;

	push.count = uvec3(width, height, depth);

	cmd.push_constants(&push, 0, sizeof(push));
	cmd.set_storage_texture(0, 0, fog);
	cmd.set_texture(0, 1, light, StockSampler::NearestClamp);
	cmd.dispatch((width + 7) / 8, (height + 7) / 8, 1);
}

void VolumetricFog::add_render_passes(RenderGraph &graph)
{
	compute_slice_extents();

	AttachmentInfo volume;
	volume.size_x = float(width);
	volume.size_y = float(height);
	volume.size_z = float(depth);
	volume.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	volume.aux_usage = VK_IMAGE_USAGE_SAMPLED_BIT;
	volume.size_class = SizeClass::Absolute;

	pass = &graph.add_pass("volumetric-fog", RENDER_GRAPH_QUEUE_COMPUTE_BIT);

	auto &in_scatter_volume = pass->add_storage_texture_output("volumetric-fog-inscatter", volume);
	fog_volume = &pass->add_storage_texture_output("volumetric-fog-output", volume);

	pass->set_build_render_pass([&](CommandBuffer &cmd) {
		auto &l = graph.get_physical_texture_resource(in_scatter_volume);
		auto &f = graph.get_physical_texture_resource(*fog_volume);

		build_light_density(cmd, l);
		cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
		            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
		build_fog(cmd, f, l);
	});
}

float VolumetricFog::get_slice_z_log2_scale() const
{
	return slice_z_log2_scale;
}

const ImageView &VolumetricFog::get_view() const
{
	return *view;
}

void VolumetricFog::set_base_renderer(Renderer *, Renderer *, Renderer *)
{
}

void VolumetricFog::set_base_render_context(const RenderContext *context)
{
	this->context = context;
}

void VolumetricFog::setup_render_pass_dependencies(RenderGraph &, RenderPass &target)
{
	target.add_texture_input("volumetric-fog-output");
	for (auto &dep : texture_dependencies)
		pass->add_texture_input(dep);
}

void VolumetricFog::setup_render_pass_resources(RenderGraph &graph)
{
	view = &graph.get_physical_texture_resource(*fog_volume);
}

void VolumetricFog::set_scene(Scene *)
{
}
}