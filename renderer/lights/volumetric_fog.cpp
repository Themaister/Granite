/* Copyright (c) 2017-2024 Hans-Kristian Arntzen
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
#include "volumetric_fog_region.hpp"
#include "render_graph.hpp"
#include "render_context.hpp"
#include <random>
#include <cmath>

using namespace Vulkan;

static constexpr unsigned NumDitherIterations = 64;

namespace Granite
{
VolumetricFogRegion::VolumetricFogRegion()
{
	EVENT_MANAGER_REGISTER_LATCH(VolumetricFogRegion, on_device_created, on_device_destroyed, DeviceCreatedEvent);
}

void VolumetricFogRegion::set_volume(Vulkan::ImageHandle handle_)
{
	handle = std::move(handle_);
}

const Vulkan::ImageView *VolumetricFogRegion::get_volume_view() const
{
	return handle ? &handle->get_view() : nullptr;
}

const AABB &VolumetricFogRegion::get_static_aabb()
{
	static AABB aabb(vec3(-0.5f), vec3(0.5f));
	return aabb;
}

void VolumetricFogRegion::on_device_destroyed(const Vulkan::DeviceCreatedEvent &)
{
	handle.reset();
}

void VolumetricFogRegion::on_device_created(const Vulkan::DeviceCreatedEvent &e)
{
	auto info = Vulkan::ImageCreateInfo::immutable_3d_image(1, 1, 1, VK_FORMAT_R8_UNORM);
	const uint8_t one = 0x0f;
	Vulkan::ImageInitialData initial = { &one, 0, 0 };
	handle = e.get_device().create_image(info, &initial);
}

VolumetricFog::VolumetricFog()
{
	set_z_range(z_range);
	EVENT_MANAGER_REGISTER_LATCH(VolumetricFog, on_device_created, on_device_destroyed, DeviceCreatedEvent);
}

void VolumetricFog::on_device_created(const DeviceCreatedEvent &)
{
}

void VolumetricFog::on_device_destroyed(const DeviceCreatedEvent &)
{
	dither_lut.reset();
	slice_depth_lut.reset();
	slice_depth_view.reset();
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

void VolumetricFog::add_texture_dependency(std::string name)
{
	texture_dependencies.push_back(std::move(name));
}

void VolumetricFog::add_storage_buffer_dependency(std::string name)
{
	buffer_dependencies.push_back(std::move(name));
}

void VolumetricFog::compute_slice_extents(Vulkan::Device &device)
{
	std::vector<float> slice_extents;
	slice_extents.reserve(depth);

	for (unsigned z = 0; z < depth; z++)
	{
		float end_z = exp2((float(z) + 1.0f) / (float(depth) * get_slice_z_log2_scale())) - 1.0f;
		float start_z = exp2(float(z) / (float(depth) * get_slice_z_log2_scale())) - 1.0f;
		slice_extents.push_back(end_z - start_z);
	}

	Vulkan::BufferCreateInfo info = {};
	info.size = depth * sizeof(float);
	info.usage = VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
	info.domain = Vulkan::BufferDomain::Device;
	slice_depth_lut = device.create_buffer(info, slice_extents.data());
	device.set_name(*slice_depth_lut, "slice-depth-lut");

	Vulkan::BufferViewCreateInfo view_info = {};
	view_info.buffer = slice_depth_lut.get();
	view_info.offset = 0;
	view_info.range = VK_WHOLE_SIZE;
	view_info.format = VK_FORMAT_R32_SFLOAT;
	slice_depth_view = device.create_buffer_view(view_info);
}

void VolumetricFog::build_light_density(CommandBuffer &cmd, ImageView &light_density,
                                        ImageView *light_density_history)
{
	struct Push
	{
		alignas(16) mat4 inv_view_projection;
		alignas(16) vec4 z_transform;
		alignas(16) uvec3 count;
		alignas(4) int32_t dither_offset;
		alignas(16) vec3 inv_resolution;
		alignas(4) float inscatter_strength;
		alignas(8) vec2 xy_scale;
		alignas(4) float slice_z_log2_scale;
		alignas(4) float density_mod;
	};

	auto &push = *cmd.allocate_typed_constant_data<Push>(3, 0, 1);
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
	push.dither_offset = int(dither_offset);

	dither_offset = (dither_offset + 1) % NumDitherIterations;

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

	Renderer::add_subgroup_defines(cmd.get_device(), defines, VK_SHADER_STAGE_COMPUTE_BIT);
	if (cmd.get_device().supports_subgroup_size_log2(true, 3, 6))
	{
		defines.emplace_back("SUBGROUP_COMPUTE_FULL", 1);
		cmd.set_subgroup_size_log2(true, 3, 6);
		cmd.enable_subgroup_size_control(true);
	}

	old_projection = context->get_render_parameters().view_projection;

	if (floor.input_view)
	{
		defines.emplace_back("FLOOR_LIGHTING", 1);
		cmd.set_texture(2, 7, *floor.input_view, Vulkan::StockSampler::TrilinearWrap);
		*cmd.allocate_typed_constant_data<FloorLighting>(2, 8, 1) = floor.info;
	}

	if (context->get_lighting_parameters()->cluster &&
	    context->get_lighting_parameters()->cluster->clusterer_has_volumetric_fog())
	{
		defines.emplace_back("FOG_REGIONS", 1);
	}

	cmd.set_program("builtin://shaders/lights/fog_light_density.comp", defines);
	Renderer::bind_global_parameters(cmd, *context);
	Renderer::bind_lighting_parameters(cmd, *context);

	cmd.push_constants(&push, 0, sizeof(push));
	cmd.set_storage_texture(2, 0, light_density);
	cmd.set_buffer_view(2, 1, *slice_depth_view);
	cmd.set_texture(2, 2, dither_lut->get_view(), StockSampler::NearestWrap);

	cmd.dispatch((width + 3) / 4, (height + 3) / 4, (depth + 3) / 4);
	cmd.enable_subgroup_size_control(false);
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
	slice_depth_view.reset();
	slice_depth_lut.reset();
	dither_lut.reset();

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
	pass->add_history_input("volumetric-fog-inscatter");

	pass->set_build_render_pass([&](CommandBuffer &cmd) {
		auto &l = graph.get_physical_texture_resource(in_scatter_volume);
		auto &f = graph.get_physical_texture_resource(*fog_volume);
		auto *l_history = graph.get_physical_history_texture_resource(in_scatter_volume);

		build_light_density(cmd, l, l_history);
		cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
		            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
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

void VolumetricFog::setup_render_pass_dependencies(RenderGraph &, RenderPass &target,
                                                   RenderPassCreator::DependencyFlags dep_flags)
{
	if ((dep_flags & RenderPassCreator::LIGHTING_BIT) != 0)
		target.add_texture_input("volumetric-fog-output");
}

void VolumetricFog::setup_render_pass_dependencies(RenderGraph &graph)
{
	for (auto &dep : texture_dependencies)
		pass->add_texture_input(dep);
	for (auto &dep : buffer_dependencies)
		pass->add_storage_read_only_input(dep);

	if (!dither_lut)
		build_dither_lut(graph.get_device());
	if (!slice_depth_view)
		compute_slice_extents(graph.get_device());

	if (!floor.input.empty())
		floor.input_resource = &pass->add_texture_input(floor.input);
	else
		floor.input_resource = nullptr;

	if (graph.find_pass("probe-light"))
		pass->add_proxy_input("probe-light-proxy", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
	pass->add_external_lock("bindless-shadowmaps", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
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

namespace blue
{
// Blue Noise Sampler by Eric Heitz. Returns a value in the range [0, 1].
#include "utils/blue/samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_1spp.hpp"
}

void VolumetricFog::build_dither_lut(Device &device)
{
	constexpr int W = 128;
	constexpr int H = 128;
	auto info = ImageCreateInfo::immutable_2d_image(W, H, VK_FORMAT_R8G8B8A8_UNORM);
	info.layers = NumDitherIterations;
	info.levels = 1;

	const auto encode = [](float x, float y, float z, float offset) -> uint32_t {
		x += offset;
		y += offset;
		z += offset;
		x = fract(x);
		y = fract(y);
		z = fract(z);
		auto ix = uint32_t(x * 255.0f + 0.5f);
		auto iy = uint32_t(y * 255.0f + 0.5f);
		auto iz = uint32_t(z * 255.0f + 0.5f);
		return ix | (iy << 8) | (iz << 16);
	};

	constexpr float GOLDEN_RATIO = 1.61803398875f;

	// From https://github.com/GPUOpen-Effects/FidelityFX-SSSR/blob/master/sample/src/Shaders/PrepareBlueNoiseTexture.hlsl.
	std::vector<uint32_t> buffer(W * H * NumDitherIterations);
	for (int z = 0; z < int(NumDitherIterations); z++)
		for (int y = 0; y < H; y++)
			for (int x = 0; x < W; x++)
				buffer[z * W * H + y * W + x] = encode(
						blue::samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_1spp(x, y, 0, 0),
						blue::samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_1spp(x, y, 0, 1),
						blue::samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_1spp(x, y, 0, 2),
						GOLDEN_RATIO * float(z));

	ImageInitialData init[NumDitherIterations] = {};
	for (unsigned i = 0; i < NumDitherIterations; i++)
		init[i].data = buffer.data() + i * W * H;
	dither_lut = device.create_image(info, init);
	device.set_name(*dither_lut, "blue-noise-lut");
}
}