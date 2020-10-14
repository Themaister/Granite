/* Copyright (c) 2017-2020 Hans-Kristian Arntzen
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
#include <random>
#include <cmath>

using namespace Vulkan;
using namespace std;

namespace Granite
{
VolumetricFog::VolumetricFog()
{
	set_z_range(z_range);
	EVENT_MANAGER_REGISTER_LATCH(VolumetricFog, on_device_created, on_device_destroyed, DeviceCreatedEvent);
	EVENT_MANAGER_REGISTER(VolumetricFog, on_frame_tick, FrameTickEvent);
}

bool VolumetricFog::on_frame_tick(const FrameTickEvent &e)
{
	const double period = 10.0;
	mod_time = float(fmod(e.get_elapsed_time(), period));
	return true;
}

void VolumetricFog::on_device_created(const DeviceCreatedEvent &)
{
}

void VolumetricFog::on_device_destroyed(const DeviceCreatedEvent &)
{
	dither_lut.reset();
}

void VolumetricFog::set_z_range(float range)
{
	z_range = range;
	slice_z_log2_scale = 1.0f / log2(1.0f + range);
}

void VolumetricFog::set_fog_density(float density)
{
	density_mod = density;
}

void VolumetricFog::set_resolution(unsigned width_, unsigned height_, unsigned depth_)
{
	width = width_;
	height = height_;
	depth = depth_;
}

void VolumetricFog::add_texture_dependency(string name)
{
	texture_dependencies.push_back(move(name));
}

void VolumetricFog::add_storage_buffer_dependency(string name)
{
	buffer_dependencies.push_back(move(name));
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

void VolumetricFog::build_density(CommandBuffer &cmd, ImageView &fog_density, float freq_mod)
{
	struct Push
	{
		alignas(16) mat4 inv_view_projection;
		alignas(16) vec4 z_transform;
		alignas(16) uvec3 count;
		alignas(4) float t;
		alignas(16) vec3 inv_resolution;
		alignas(4) float freq;
	} push;
	push.inv_view_projection = context->get_render_parameters().inv_view_projection;
	push.z_transform = vec4(context->get_render_parameters().projection[2].zw(),
	                        context->get_render_parameters().projection[3].zw());
	push.count = uvec3(width, height, depth);
	push.t = mod_time;
	push.count = uvec3(
			fog_density.get_image().get_width(),
			fog_density.get_image().get_height(),
			fog_density.get_image().get_depth());
	push.inv_resolution = vec3(
			1.0f / fog_density.get_image().get_width(),
			1.0f / fog_density.get_image().get_height(),
			1.0f / fog_density.get_image().get_depth());
	push.freq = 10.0f * freq_mod;

	cmd.push_constants(&push, 0, sizeof(push));
	cmd.set_storage_texture(2, 0, fog_density);

	cmd.set_program("builtin://shaders/lights/fog_density_simplex.comp");
	cmd.dispatch((fog_density.get_image().get_width() + 3) / 4,
	             (fog_density.get_image().get_height() + 3) / 4,
	             (fog_density.get_image().get_depth() + 3) / 4);
}

void VolumetricFog::build_light_density(CommandBuffer &cmd, ImageView &light_density, ImageView &fog_density, ImageView &fog_density_low_freq,
                                        ImageView *light_density_history)
{
	struct Push
	{
		alignas(16) mat4 inv_view_projection;
		alignas(16) vec4 z_transform;
		alignas(16) uvec3 count;
		alignas(4) float dither_offset;
		alignas(16) vec3 inv_resolution;
		alignas(4) float inscatter_strength;
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
	push.density_mod = density_mod;
	push.inscatter_strength = inscatter_mod;
	push.dither_offset = float(dither_offset & 1023);
	dither_offset++;

	cmd.push_constants(&push, 0, sizeof(push));

	auto flags = Renderer::get_mesh_renderer_options_from_lighting(*context->get_lighting_parameters());
	flags &= ~(Renderer::VOLUMETRIC_FOG_ENABLE_BIT | Renderer::AMBIENT_OCCLUSION_BIT);
	auto defines = Renderer::build_defines_from_renderer_options(RendererType::GeneralForward, flags);
	if (light_density_history)
	{
		defines.emplace_back("TEMPORAL_REPROJECTION", 1);
		cmd.set_texture(2, 5, *light_density_history, StockSampler::LinearClamp);

		struct Temporal
		{
			mat4 old_projection;
			vec4 inv_z_transform;
		};
		auto *temporal = cmd.allocate_typed_constant_data<Temporal>(2, 6, 1);
		temporal->old_projection = old_projection;
		temporal->inv_z_transform = vec4(
				context->get_render_parameters().inv_projection[2].zw(),
				context->get_render_parameters().inv_projection[3].zw());
	}

	if (flags & Renderer::POSITIONAL_LIGHT_ENABLE_BIT)
	{
		// Try to enable wave-optimizations.
		static const VkSubgroupFeatureFlags required_subgroup =
				VK_SUBGROUP_FEATURE_BALLOT_BIT |
				VK_SUBGROUP_FEATURE_ARITHMETIC_BIT;

		auto &subgroup = cmd.get_device().get_device_features().subgroup_properties;
		if ((subgroup.supportedStages & VK_SHADER_STAGE_FRAGMENT_BIT) != 0 &&
		    !ImplementationQuirks::get().force_no_subgroups &&
		    (subgroup.supportedOperations & required_subgroup) == required_subgroup)
		{
			defines.emplace_back("CLUSTERING_WAVE_UNIFORM", 1);
		}
	}

	if (flags & Renderer::SHADOW_CASCADE_ENABLE_BIT)
	{
		auto &subgroup = cmd.get_device().get_device_features().subgroup_properties;
		if ((subgroup.supportedStages & VK_SHADER_STAGE_FRAGMENT_BIT) != 0 &&
		    !ImplementationQuirks::get().force_no_subgroups &&
		    (subgroup.supportedOperations & VK_SUBGROUP_FEATURE_ARITHMETIC_BIT) != 0)
		{
			defines.emplace_back("SUBGROUP_ARITHMETIC", 1);
		}
	}

	old_projection = context->get_render_parameters().view_projection;

	if (floor.input_view)
	{
		defines.emplace_back("FLOOR_LIGHTING", 1);
		cmd.set_texture(2, 7, *floor.input_view, Vulkan::StockSampler::TrilinearWrap);
		*cmd.allocate_typed_constant_data<FloorLighting>(2, 8, 1) = floor.info;
	}

	cmd.set_program("builtin://shaders/lights/fog_light_density.comp", defines);
	Renderer::bind_global_parameters(cmd, *context);
	Renderer::bind_lighting_parameters(cmd, *context);

	cmd.push_constants(&push, 0, sizeof(push));
	cmd.set_storage_texture(2, 0, light_density);
	memcpy(cmd.allocate_typed_constant_data<float>(2, 1, depth),
	       slice_extents,
	       sizeof(float) * depth);
	cmd.set_texture(2, 2, dither_lut->get_view(), StockSampler::NearestWrap);
	cmd.set_texture(2, 3, fog_density, StockSampler::LinearWrap);
	cmd.set_texture(2, 4, fog_density_low_freq, StockSampler::LinearWrap);

	cmd.dispatch((width + 3) / 4, (height + 3) / 4, (depth + 3) / 4);
}

void VolumetricFog::set_floor_lighting(const std::string &input, const FloorLighting &info)
{
	floor.input = input;
	floor.info = info;
}

void VolumetricFog::build_fog(CommandBuffer &cmd, ImageView &fog, ImageView &light)
{
	cmd.set_program("builtin://shaders/lights/fog_accumulate.comp");
	struct Push
	{
		alignas(16) vec3 inv_resolution;
		alignas(16) uvec3 count;
	} push;

	push.inv_resolution.x = 1.0f / float(light.get_image().get_width());
	push.inv_resolution.y = 1.0f / float(light.get_image().get_height());
	push.inv_resolution.z = 1.0f / float(light.get_image().get_depth());
	push.count = uvec3(width, height, depth);

	cmd.push_constants(&push, 0, sizeof(push));
	cmd.set_storage_texture(0, 0, fog);
	cmd.set_texture(0, 1, light, StockSampler::NearestClamp);
	cmd.dispatch((width + 7) / 8, (height + 7) / 8, 1);
}

void VolumetricFog::add_render_passes(RenderGraph &graph)
{
	compute_slice_extents();
	dither_lut.reset();

	AttachmentInfo density;
	density.size_x = 32.0f;
	density.size_y = 32.0f;
	density.size_z = 32.0f;
	density.format = VK_FORMAT_R16_SFLOAT;
	density.aux_usage = VK_IMAGE_USAGE_SAMPLED_BIT;
	density.size_class = SizeClass::Absolute;

	AttachmentInfo volume;
	volume.size_x = float(width);
	volume.size_y = float(height);
	volume.size_z = float(depth);
	volume.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	volume.aux_usage = VK_IMAGE_USAGE_SAMPLED_BIT;
	volume.size_class = SizeClass::Absolute;

	pass = &graph.add_pass("volumetric-fog", RENDER_GRAPH_QUEUE_COMPUTE_BIT);

	auto &in_scatter_volume = pass->add_storage_texture_output("volumetric-fog-inscatter", volume);
	auto &density_volume = pass->add_storage_texture_output("volumetric-fog-density", density);
	auto &density_volume_low_freq = pass->add_storage_texture_output("volumetric-fog-density-low-freq", density);
	fog_volume = &pass->add_storage_texture_output("volumetric-fog-output", volume);
	pass->add_history_input("volumetric-fog-inscatter");

	pass->set_build_render_pass([&](CommandBuffer &cmd) {
		auto &d = graph.get_physical_texture_resource(density_volume);
		auto &d_low = graph.get_physical_texture_resource(density_volume_low_freq);
		auto &l = graph.get_physical_texture_resource(in_scatter_volume);
		auto &f = graph.get_physical_texture_resource(*fog_volume);
		auto *l_history = graph.get_physical_history_texture_resource(in_scatter_volume);

		build_density(cmd, d, 1.0f);
		build_density(cmd, d_low, 0.25f);
		cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
		            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
		build_light_density(cmd, l, d, d_low, l_history);
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

void VolumetricFog::set_base_renderer(const RendererSuite *)
{
}

void VolumetricFog::set_base_render_context(const RenderContext *context_)
{
	context = context_;
}

void VolumetricFog::setup_render_pass_dependencies(RenderGraph &graph, RenderPass &target)
{
	target.add_texture_input("volumetric-fog-output");
	for (auto &dep : texture_dependencies)
		pass->add_texture_input(dep);
	for (auto &dep : buffer_dependencies)
		pass->add_storage_read_only_input(dep);

	if (!dither_lut)
		build_dither_lut(graph.get_device());

	if (!floor.input.empty())
		floor.input_resource = &pass->add_texture_input(floor.input);
	else
		floor.input_resource = nullptr;
}

void VolumetricFog::setup_render_pass_resources(RenderGraph &graph)
{
	view = &graph.get_physical_texture_resource(*fog_volume);
	if (floor.input_resource)
		floor.input_view = &graph.get_physical_texture_resource(*floor.input_resource);
	else
		floor.input_view = nullptr;
}

void VolumetricFog::set_scene(Scene *)
{
}

void VolumetricFog::build_dither_lut(Device &device)
{
	auto info = ImageCreateInfo::immutable_3d_image(width / 4, height / 4, depth / 4, VK_FORMAT_A2B10G10R10_UNORM_PACK32);

	mt19937 rnd;
	uniform_int_distribution<uint32_t> dist(0, 1023);

	vector<uint32_t> buffer((width * height * depth) / (4 * 4 * 4));
	for (auto &elem : buffer)
	{
		uint32_t b = dist(rnd);
		uint32_t g = dist(rnd);
		uint32_t r = dist(rnd);
		elem = (b << 20) | (g << 10) | (r << 0);
	}

	ImageInitialData init = {};
	init.data = buffer.data();
	dither_lut = device.create_image(info, &init);
}
}