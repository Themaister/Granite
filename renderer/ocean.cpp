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

#include "fft/glfft_granite_interface.hpp"
#include "ocean.hpp"
#include "device.hpp"
#include "renderer.hpp"
#include "render_context.hpp"
#include "render_graph.hpp"
#include "muglm/matrix_helper.hpp"
#include <random>
#include "timer.hpp"

using namespace std;

namespace Granite
{
static constexpr unsigned MaxLODIndirect = 8;
static constexpr float G = 9.81f;

struct OceanVertex
{
	uint8_t pos[4];
	uint8_t weights[4];
};

Ocean::Ocean(const OceanConfig &config_)
	: config(config_)
{
	for (auto &f : frequency_bands)
		f = 1.0f;

	wind_direction = normalize(config.wind_velocity);
	phillips_L = dot(config.wind_velocity, config.wind_velocity) / G;

	// Normalize amplitude based on how dense the FFT frequency space is.
	vec2 base_freq = 1.0f / heightmap_world_size();

	// We're modelling noise, so assume we're integrating energy, not amplitude.
	this->config.amplitude *= muglm::sqrt(base_freq.x * base_freq.y);

	EVENT_MANAGER_REGISTER_LATCH(Ocean, on_device_created, on_device_destroyed, Vulkan::DeviceCreatedEvent);
	EVENT_MANAGER_REGISTER(Ocean, on_frame_tick, FrameTickEvent);
}

void Ocean::set_frequency_band_amplitude(unsigned band, float amplitude)
{
	assert(band < FrequencyBands);
	frequency_bands[band] = amplitude;
}

void Ocean::set_frequency_band_modulation(bool enable)
{
	freq_band_modulation = enable;
}

Ocean::Handles Ocean::add_to_scene(Scene &scene, const OceanConfig &config)
{
	Handles handles;
	handles.entity = scene.create_entity();

	auto ocean = Util::make_handle<Ocean>(config);

	auto *update_component = handles.entity->allocate_component<PerFrameUpdateComponent>();
	update_component->refresh = ocean.get();

	auto *rp = handles.entity->allocate_component<RenderPassComponent>();
	rp->creator = ocean.get();

	auto *renderable = handles.entity->allocate_component<RenderableComponent>();
	renderable->renderable = ocean;

	handles.entity->allocate_component<OpaqueComponent>();
	handles.entity->allocate_component<UnboundedComponent>();
	handles.ocean = ocean.get();

	return handles;
}

bool Ocean::on_frame_tick(const Granite::FrameTickEvent &e)
{
	current_time = e.get_elapsed_time();
	return true;
}

void Ocean::on_device_created(const Vulkan::DeviceCreatedEvent &e)
{
	fft_iface = FFTInterface(&e.get_device());
	GLFFT::FFTOptions options = {};
	options.type.fp16 = true;
	options.type.input_fp16 = true;
	options.type.output_fp16 = true;

	options.performance =
			GLFFT::FFTWisdom::get_static_performance_options_from_renderer(&fft_iface);

	Util::Timer timer;
	timer.start();

	auto cache = make_shared<GLFFT::ProgramCache>();

	height_fft.reset(new GLFFT::FFT(&fft_iface,
	                                config.fft_resolution, config.fft_resolution,
	                                GLFFT::ComplexToReal, GLFFT::Inverse,
	                                GLFFT::SSBO, GLFFT::ImageReal,
	                                cache, options));

	displacement_fft.reset(new GLFFT::FFT(&fft_iface,
	                                      config.fft_resolution >> config.displacement_downsample,
	                                      config.fft_resolution >> config.displacement_downsample,
	                                      GLFFT::ComplexToComplex, GLFFT::Inverse,
	                                      GLFFT::SSBO, GLFFT::Image,
	                                      cache, options));

	normal_fft.reset(new GLFFT::FFT(&fft_iface,
	                                config.fft_resolution, config.fft_resolution,
	                                GLFFT::ComplexToComplex, GLFFT::Inverse,
	                                GLFFT::SSBO, GLFFT::Image,
	                                cache, options));

	LOGI("Creating GLFFT took %.3f ms!\n", timer.end() * 1000.0);

	build_buffers(e.get_device());
	init_distributions(e.get_device());
}

void Ocean::on_device_destroyed(const Vulkan::DeviceCreatedEvent &)
{
	vertex_mip_views.clear();
	fragment_mip_views.clear();
	normal_mip_views.clear();

	height_fft.reset();
	normal_fft.reset();
	displacement_fft.reset();
	distribution_buffer.reset();
	distribution_buffer_displacement.reset();
	distribution_buffer_normal.reset();

	quad_lod.clear();
	border_vbo.reset();
	border_ibo.reset();
}

void Ocean::refresh(const RenderContext &context_, TaskComposer &)
{
	last_camera_position = context_.get_render_parameters().camera_position;
}

void Ocean::set_base_renderer(const RendererSuite *)
{
}

void Ocean::set_base_render_context(const RenderContext *context_)
{
	context = context_;
}

void Ocean::set_scene(Scene *)
{
}

void Ocean::setup_render_pass_dependencies(RenderGraph &, RenderPass &target)
{
	target.add_indirect_buffer_input("ocean-lod-counter");
	target.add_uniform_input("ocean-lod-data", VK_PIPELINE_STAGE_VERTEX_SHADER_BIT);
	target.add_texture_input("ocean-lods", VK_PIPELINE_STAGE_VERTEX_SHADER_BIT);

	target.add_texture_input("ocean-height-displacement-output", VK_PIPELINE_STAGE_VERTEX_SHADER_BIT);
	target.add_texture_input("ocean-gradient-jacobian-output", VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
	target.add_texture_input("ocean-normal-fft-output", VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

	if (!config.refraction.input.empty() && config.refraction.input_is_render_graph)
		refraction_resource = &target.add_texture_input(config.refraction.input);
	else
		refraction_resource = nullptr;
}

void Ocean::setup_render_pass_resources(RenderGraph &graph_)
{
	if (vertex_mip_views.empty() && fragment_mip_views.empty() && normal_mip_views.empty())
	{
		auto &vertex = graph_.get_physical_texture_resource(*height_displacement_output);
		auto &fragment = graph_.get_physical_texture_resource(*gradient_jacobian_output);
		auto &normal = graph_.get_physical_texture_resource(*normal_fft_output);

		unsigned vertex_lods = muglm::min(unsigned(quad_lod.size()), vertex.get_image().get_create_info().levels);
		unsigned fragment_lods = fragment.get_image().get_create_info().levels;
		unsigned normal_lods = normal.get_image().get_create_info().levels;

		for (unsigned i = 0; i < vertex_lods; i++)
		{
			Vulkan::ImageViewCreateInfo view;
			view.image = &vertex.get_image();
			view.format = vertex.get_format();
			view.layers = 1;
			view.levels = 1;
			view.base_level = i;
			vertex_mip_views.push_back(graph_.get_device().create_image_view(view));
		}

		for (unsigned i = 0; i < fragment_lods; i++)
		{
			Vulkan::ImageViewCreateInfo view;
			view.image = &fragment.get_image();
			view.format = fragment.get_format();
			view.layers = 1;
			view.levels = 1;
			view.base_level = i;
			fragment_mip_views.push_back(graph_.get_device().create_image_view(view));
		}

		for (unsigned i = 0; i < normal_lods; i++)
		{
			Vulkan::ImageViewCreateInfo view;
			view.image = &normal.get_image();
			view.format = normal.get_format();
			view.layers = 1;
			view.levels = 1;
			view.base_level = i;
			normal_mip_views.push_back(graph_.get_device().create_image_view(view));
		}

		// Prebuild the FFT commands and sort so we can avoid most barriers.
		deferred_cmd.reset();

		deferred_cmd.reset_command_counter();
		FFTTexture height_output(&graph_.get_physical_texture_resource(*height_fft_output));
		FFTBuffer height_input(&graph_.get_physical_buffer_resource(*height_fft_input));
		height_fft->process(&deferred_cmd, &height_output, &height_input);

		deferred_cmd.reset_command_counter();
		FFTTexture normal_output(normal_mip_views.front().get());
		FFTBuffer normal_input(&graph_.get_physical_buffer_resource(*normal_fft_input));
		normal_fft->process(&deferred_cmd, &normal_output, &normal_input);

		deferred_cmd.reset_command_counter();
		FFTTexture displacement_output(&graph_.get_physical_texture_resource(*displacement_fft_output));
		FFTBuffer displacement_input(&graph_.get_physical_buffer_resource(*displacement_fft_input));
		displacement_fft->process(&deferred_cmd, &displacement_output, &displacement_input);
	}

	refraction = nullptr;
	if (!config.refraction.input.empty())
	{
		if (config.refraction.input_is_render_graph)
		{
			refraction = &graph_.get_physical_texture_resource(*refraction_resource);
		}
		else
		{
			auto *texture = graph_.get_device().get_texture_manager().request_texture(config.refraction.input);
			if (texture)
				refraction = &texture->get_image()->get_view();
		}
	}
}

vec2 Ocean::get_grid_size() const
{
	return config.ocean_size / vec2(config.grid_count);
}

vec2 Ocean::get_snapped_grid_center() const
{
	vec2 inv_grid_size = vec2(config.grid_count) / config.ocean_size;
	vec2 grid_center = round(last_camera_position.xz() * inv_grid_size);
	return grid_center;
}

ivec2 Ocean::get_grid_base_coord() const
{
	return ivec2(get_snapped_grid_center()) - (ivec2(config.grid_count) >> 1);
}

void Ocean::build_lod_map(Vulkan::CommandBuffer &cmd)
{
	auto &lod = graph->get_physical_texture_resource(*ocean_lod);
	cmd.set_storage_texture(0, 0, lod);

	vec2 grid_center = get_snapped_grid_center();
	vec2 grid_base = grid_center * get_grid_size() - 0.5f * config.ocean_size;

	struct Push
	{
		alignas(16) vec3 camera_pos;
		alignas(4) float max_lod;
		alignas(8) ivec2 image_offset;
		alignas(8) ivec2 num_threads;
		alignas(8) vec2 grid_base;
		alignas(8) vec2 grid_size;
	} push;
	push.camera_pos = last_camera_position;
	push.image_offset = get_grid_base_coord();
	push.num_threads = ivec2(config.grid_count);
	push.grid_base = grid_base;
	push.grid_size = get_grid_size();
	push.max_lod = float(quad_lod.size()) - 1.0f;
	cmd.push_constants(&push, 0, sizeof(push));

	cmd.set_program("builtin://shaders/ocean/update_lod.comp");
	cmd.dispatch((config.grid_count + 7) / 8, (config.grid_count + 7) / 8, 1);
}

void Ocean::init_counter_buffer(Vulkan::CommandBuffer &cmd)
{
	cmd.set_storage_buffer(0, 0, graph->get_physical_buffer_resource(*lod_data_counters));
	uint32_t *vertex_counts = cmd.allocate_typed_constant_data<uint32_t>(0, 1, 16);
	for (unsigned i = 0; i < 16; i++)
		vertex_counts[i] = (i < quad_lod.size()) ? quad_lod[i].count : 0;

	cmd.set_program("builtin://shaders/ocean/init_counter_buffer.comp",
	                {{ "NUM_COUNTERS", int(MaxLODIndirect) }});
	cmd.dispatch(1, 1, 1);
}

void Ocean::cull_blocks(Vulkan::CommandBuffer &cmd)
{
	struct Push
	{
		alignas(8) ivec2 image_offset;
		alignas(8) ivec2 num_threads;
		alignas(8) vec2 inv_num_threads;
		alignas(8) vec2 grid_base;
		alignas(8) vec2 grid_size;
		alignas(8) vec2 grid_resolution;
		alignas(8) vec2 heightmap_range;
		alignas(4) float guard_band;
		alignas(4) uint lod_stride;
		alignas(4) float max_lod;
	} push;

	vec2 grid_center = get_snapped_grid_center();
	vec2 grid_base = grid_center * get_grid_size() - 0.5f * config.ocean_size;

	memcpy(cmd.allocate_typed_constant_data<vec4>(0, 3, 6),
	       context->get_visibility_frustum().get_planes(),
	       sizeof(vec4) * 6);

	push.image_offset = get_grid_base_coord();
	push.num_threads = ivec2(config.grid_count);
	push.inv_num_threads = 1.0f / vec2(push.num_threads);
	push.grid_base = grid_base;
	push.grid_size = get_grid_size();
	push.lod_stride = config.grid_count * config.grid_count;
	push.heightmap_range = vec2(-10.0f, 10.0f);
	push.guard_band = 5.0f;
	push.grid_resolution = vec2(config.grid_resolution);
	push.max_lod = float(quad_lod.size()) - 1.0f;

	cmd.push_constants(&push, 0, sizeof(push));

	auto &lod = graph->get_physical_texture_resource(*ocean_lod);
	auto &lod_buffer = graph->get_physical_buffer_resource(*lod_data);
	cmd.set_storage_buffer(0, 0, lod_buffer);
	auto &lod_counter_buffer = graph->get_physical_buffer_resource(*lod_data_counters);
	cmd.set_storage_buffer(0, 1, lod_counter_buffer);
	cmd.set_texture(0, 2, lod, Vulkan::StockSampler::NearestWrap);

	cmd.set_program("builtin://shaders/ocean/cull_blocks.comp");
	cmd.dispatch((config.grid_count + 7) / 8, (config.grid_count + 7) / 8, 1);
}

void Ocean::update_lod_pass(Vulkan::CommandBuffer &cmd)
{
	build_lod_map(cmd);
	init_counter_buffer(cmd);

	cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
	            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT);

	cull_blocks(cmd);
}

vec2 Ocean::heightmap_world_size() const
{
	return get_grid_size() * float(config.fft_resolution) / float(config.grid_resolution);
}

vec2 Ocean::normalmap_world_size() const
{
	return heightmap_world_size() / config.normal_mod;
}

void Ocean::update_fft_input(Vulkan::CommandBuffer &cmd)
{
	auto *program = cmd.get_device().get_shader_manager().register_compute("builtin://shaders/ocean/generate_fft.comp");
	auto *height_variant = program->register_variant({
			                                                 { "FREQ_BAND_MODULATION", freq_band_modulation ? 1 : 0 }
	                                                 });

	auto *normal_variant = program->register_variant({
			                                                 { "GRADIENT_NORMAL", 1 },
			                                                 { "FREQ_BAND_MODULATION", freq_band_modulation ? 1 : 0 }
	                                                 });

	auto *displacement_variant = program->register_variant({
			                                                       { "GRADIENT_DISPLACEMENT", 1 },
			                                                       { "FREQ_BAND_MODULATION", freq_band_modulation ? 1 : 0}
	                                                       });

	struct Push
	{
		vec2 mod;
		uvec2 N;
		float freq_to_band_mod;
		float time;
	};
	Push push;
	push.mod = vec2(2.0f * pi<float>()) / heightmap_world_size();
	push.time = float(current_time);
	push.freq_to_band_mod = (float(FrequencyBands - 1) * 2.0f) / float(config.fft_resolution);

	if (freq_band_modulation)
	{
		memcpy(cmd.allocate_typed_constant_data<float>(1, 0, FrequencyBands),
		       frequency_bands, sizeof(frequency_bands));
	}

	cmd.set_program(height_variant->get_program());
	push.N = uvec2(config.fft_resolution);
	cmd.set_storage_buffer(0, 0, *distribution_buffer);
	cmd.set_storage_buffer(0, 1, graph->get_physical_buffer_resource(*height_fft_input));
	cmd.push_constants(&push, 0, sizeof(push));
	cmd.dispatch(config.fft_resolution / 64, config.fft_resolution, 1);

	cmd.set_program(displacement_variant->get_program());
	push.N = uvec2(config.fft_resolution >> config.displacement_downsample);
	cmd.set_storage_buffer(0, 0, *distribution_buffer_displacement);
	cmd.set_storage_buffer(0, 1, graph->get_physical_buffer_resource(*displacement_fft_input));
	cmd.push_constants(&push, 0, sizeof(push));
	cmd.dispatch((config.fft_resolution >> config.displacement_downsample) / 64,
	             config.fft_resolution >> config.displacement_downsample,
	             1);

	push.mod = vec2(2.0f * pi<float>()) / normalmap_world_size();
	cmd.set_program(normal_variant->get_program());
	push.N = uvec2(config.fft_resolution);
	cmd.set_storage_buffer(0, 0, *distribution_buffer_normal);
	cmd.set_storage_buffer(0, 1, graph->get_physical_buffer_resource(*normal_fft_input));
	cmd.push_constants(&push, 0, sizeof(push));
	cmd.dispatch(config.fft_resolution / 64, config.fft_resolution, 1);
}

void Ocean::compute_fft(Vulkan::CommandBuffer &cmd)
{
	deferred_cmd.build(cmd);
}

void Ocean::bake_maps(Vulkan::CommandBuffer &cmd)
{
	cmd.set_program("builtin://shaders/ocean/bake_maps.comp");

	struct Push
	{
		vec4 inv_size;
		vec4 scale;
	} push;

	push.inv_size = vec4(1.0f / vec2(config.fft_resolution),
	                     1.0f / vec2(config.fft_resolution >> config.displacement_downsample));

	vec2 delta_heightmap = get_grid_size() / float(config.grid_resolution);
	vec2 delta_displacement = delta_heightmap * float(1u << config.displacement_downsample);
	push.scale = vec4(1.0f / delta_heightmap, 1.0f / delta_displacement);

	cmd.push_constants(&push, 0, sizeof(push));

	cmd.set_texture(0, 0,
	                graph->get_physical_texture_resource(*height_fft_output),
	                Vulkan::StockSampler::LinearWrap);
	cmd.set_texture(0, 1,
	                graph->get_physical_texture_resource(*displacement_fft_output),
	                Vulkan::StockSampler::LinearWrap);
	cmd.set_storage_texture(0, 2, *vertex_mip_views.front());
	cmd.set_storage_texture(0, 3, *fragment_mip_views.front());

	cmd.dispatch((config.fft_resolution + 7) / 8, (config.fft_resolution + 7) / 8, 1);
}

void Ocean::generate_mipmaps(Vulkan::CommandBuffer &cmd)
{
	auto &normal = graph->get_physical_texture_resource(*normal_fft_output);
	auto num_passes = unsigned(muglm::max(vertex_mip_views.size(), fragment_mip_views.size()));
	num_passes = muglm::max(num_passes, normal.get_image().get_create_info().levels);

	struct Push
	{
		vec4 filter_mod;
		vec2 inv_resolution;
		uvec2 count;
		float lod;
	} push;

	for (unsigned i = 1; i < num_passes; i++)
	{
		cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
		            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

		cmd.set_program("builtin://shaders/ocean/mipmap.comp",
		                {{ "MIPMAP_RGBA16F", 1 }, { "MIPMAP_TEXEL_CENTER", 1 }});

		push.lod = float(i - 1);

		if (i < vertex_mip_views.size())
		{
			// Last heightmap level should go towards 0 to make padding edges transition cleaner.
			if (i + 1 == vertex_mip_views.size())
				push.filter_mod = vec4(0.0f, 1.0f, 1.0f, 1.0f);
			else
				push.filter_mod = vec4(1.0f);

			push.inv_resolution.x = 1.0f / vertex_mip_views.front()->get_image().get_width(i - 1);
			push.inv_resolution.y = 1.0f / vertex_mip_views.front()->get_image().get_height(i - 1);
			push.count.x = vertex_mip_views.front()->get_image().get_width(i);
			push.count.y = vertex_mip_views.front()->get_image().get_height(i);

			cmd.push_constants(&push, 0, sizeof(push));
			cmd.set_storage_texture(0, 0, *vertex_mip_views[i]);
			cmd.set_texture(0, 1, *vertex_mip_views[i - 1], Vulkan::StockSampler::LinearWrap);
			cmd.dispatch((push.count.x + 7) / 8, (push.count.y + 7) / 8, 1);
		}

		cmd.set_program("builtin://shaders/ocean/mipmap.comp",
		                {{ "MIPMAP_RGBA16F", 1 }});

		if (i < fragment_mip_views.size())
		{
			push.filter_mod = vec4(1.0f);
			push.inv_resolution.x = 1.0f / fragment_mip_views.front()->get_image().get_width(i - 1);
			push.inv_resolution.y = 1.0f / fragment_mip_views.front()->get_image().get_height(i - 1);
			push.count.x = fragment_mip_views.front()->get_image().get_width(i);
			push.count.y = fragment_mip_views.front()->get_image().get_height(i);

			cmd.push_constants(&push, 0, sizeof(push));
			cmd.set_storage_texture(0, 0, *fragment_mip_views[i]);
			cmd.set_texture(0, 1, *fragment_mip_views[i - 1], Vulkan::StockSampler::LinearWrap);
			cmd.dispatch((push.count.x + 7) / 8, (push.count.y + 7) / 8, 1);
		}

		cmd.set_program("builtin://shaders/ocean/mipmap.comp",
		                {{ "MIPMAP_RG16F", 1 }});

		if (i < normal.get_image().get_create_info().levels)
		{
			push.filter_mod = vec4(1.0f);
			push.inv_resolution.x = 1.0f / normal_mip_views.front()->get_image().get_width(i - 1);
			push.inv_resolution.y = 1.0f / normal_mip_views.front()->get_image().get_height(i - 1);
			push.count.x = normal_mip_views.front()->get_image().get_width(i);
			push.count.y = normal_mip_views.front()->get_image().get_height(i);

			cmd.push_constants(&push, 0, sizeof(push));
			cmd.set_storage_texture(0, 0, *normal_mip_views[i]);
			cmd.set_texture(0, 1, *normal_mip_views[i - 1], Vulkan::StockSampler::LinearWrap);
			cmd.dispatch((push.count.x + 7) / 8, (push.count.y + 7) / 8, 1);
		}
	}
}

void Ocean::update_fft_pass(Vulkan::CommandBuffer &cmd)
{
	update_fft_input(cmd);

	cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
	            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

	compute_fft(cmd);
	bake_maps(cmd);
	generate_mipmaps(cmd);
}

void Ocean::add_lod_update_pass(RenderGraph &graph_)
{
	auto &update_lod = graph_.add_pass("ocean-update-lods", RENDER_GRAPH_QUEUE_COMPUTE_BIT);
	AttachmentInfo lod_attachment;
	lod_attachment.format = VK_FORMAT_R16_SFLOAT;
	lod_attachment.size_x = float(config.grid_count);
	lod_attachment.size_y = float(config.grid_count);
	lod_attachment.size_class = SizeClass::Absolute;
	ocean_lod = &update_lod.add_storage_texture_output("ocean-lods", lod_attachment);

	BufferInfo lod_info_counter;
	lod_info_counter.size = MaxLODIndirect * (8 * sizeof(uint32_t));
	lod_data_counters = &update_lod.add_storage_output("ocean-lod-counter", lod_info_counter);

	BufferInfo lod_info;
	lod_info.size = config.grid_count * config.grid_count * MaxLODIndirect * (2 * sizeof(uvec4));
	lod_data = &update_lod.add_storage_output("ocean-lod-data", lod_info);

	update_lod.set_build_render_pass([&](Vulkan::CommandBuffer &cmd) {
		update_lod_pass(cmd);
	});
}

void Ocean::add_fft_update_pass(RenderGraph &graph_)
{
	BufferInfo normal_info, height_info, displacement_info;
	normal_info.size = config.fft_resolution * config.fft_resolution * sizeof(uint32_t);
	height_info.size = config.fft_resolution * config.fft_resolution * sizeof(uint32_t);
	displacement_info.size =
			(config.fft_resolution >> config.displacement_downsample) *
			(config.fft_resolution >> config.displacement_downsample) *
			sizeof(uint32_t);

	AttachmentInfo normal_map;
	AttachmentInfo displacement_map;
	AttachmentInfo height_map;

	normal_map.size_class = SizeClass::Absolute;
	normal_map.size_x = float(config.fft_resolution);
	normal_map.size_y = float(config.fft_resolution);
	normal_map.format = VK_FORMAT_R16G16_SFLOAT;

	displacement_map.size_class = SizeClass::Absolute;
	displacement_map.size_x = float(config.fft_resolution >> config.displacement_downsample);
	displacement_map.size_y = float(config.fft_resolution >> config.displacement_downsample);
	displacement_map.format = VK_FORMAT_R16G16_SFLOAT;

	height_map.size_class = SizeClass::Absolute;
	height_map.size_x = float(config.fft_resolution);
	height_map.size_y = float(config.fft_resolution);
	height_map.format = VK_FORMAT_R16_SFLOAT;

	height_map.aux_usage = VK_IMAGE_USAGE_SAMPLED_BIT;
	displacement_map.aux_usage = VK_IMAGE_USAGE_SAMPLED_BIT;
	normal_map.aux_usage = VK_IMAGE_USAGE_SAMPLED_BIT;
	normal_map.levels = 0;

	auto &update_fft = graph_.add_pass("ocean-update-fft", RENDER_GRAPH_QUEUE_COMPUTE_BIT);

	height_fft_input = &update_fft.add_storage_output("ocean-height-fft-input",
	                                                  height_info);
	normal_fft_input = &update_fft.add_storage_output("ocean-normal-fft-input",
	                                                  normal_info);
	displacement_fft_input = &update_fft.add_storage_output("ocean-displacement-fft-input",
	                                                        displacement_info);

	height_fft_output = &update_fft.add_storage_texture_output("ocean-height-fft-output",
	                                                           height_map);
	normal_fft_output = &update_fft.add_storage_texture_output("ocean-normal-fft-output",
	                                                           normal_map);
	displacement_fft_output = &update_fft.add_storage_texture_output("ocean-displacement-fft-output",
	                                                                 displacement_map);

	AttachmentInfo height_displacement;
	height_displacement.size_class = SizeClass::Absolute;
	height_displacement.size_x = float(config.fft_resolution);
	height_displacement.size_y = float(config.fft_resolution);
	height_displacement.format = VK_FORMAT_R16G16B16A16_SFLOAT;

	height_displacement.levels = unsigned(quad_lod.size());

	height_displacement_output =
			&update_fft.add_storage_texture_output("ocean-height-displacement-output",
			                                       height_displacement);

	height_displacement.levels = 0;

	gradient_jacobian_output =
			&update_fft.add_storage_texture_output("ocean-gradient-jacobian-output",
			                                       height_displacement);

	update_fft.set_build_render_pass([&](Vulkan::CommandBuffer &cmd) {
		update_fft_pass(cmd);
	});
}

void Ocean::add_render_passes(RenderGraph &graph_)
{
	normal_mip_views.clear();
	vertex_mip_views.clear();
	fragment_mip_views.clear();

	graph = &graph_;
	add_lod_update_pass(graph_);
	add_fft_update_pass(graph_);
}

struct OceanData
{
	vec2 inv_heightmap_size;
	vec2 inv_ocean_grid_size;
	vec2 normal_uv_scale;
	vec2 integer_to_world_mod;
	vec2 heightmap_range;
	vec2 base_position;
};

struct RefractionData
{
	vec4 texture_size;
	vec4 depths;
	float uv_scale;
	float emissive_mod;
	uint32_t layers;
};

struct OceanInfo
{
	Vulkan::Program *program;
	const Vulkan::Buffer *ubo;
	const Vulkan::Buffer *indirect;
	const Vulkan::Buffer *vbos[MaxLODIndirect];
	const Vulkan::Buffer *ibos[MaxLODIndirect];

	const Vulkan::ImageView *heightmap;
	const Vulkan::ImageView *lod_map;
	const Vulkan::ImageView *grad_jacobian;
	const Vulkan::ImageView *normal;

	Vulkan::Program *border_program;
	const Vulkan::Buffer *border_vbo;
	const Vulkan::Buffer *border_ibo;
	VkIndexType index_type;
	unsigned border_count;

	unsigned lods;
	unsigned lod_stride;
	OceanData data;

	const Vulkan::ImageView *refraction;
	RefractionData refraction_data;
};

namespace RenderFunctions
{
static void ocean_render(Vulkan::CommandBuffer &cmd, const RenderQueueData *infos, unsigned num_instances)
{
	auto &ocean_info = *static_cast<const OceanInfo *>(infos->render_info);

	cmd.set_primitive_restart(true);
	//cmd.set_wireframe(true);
	cmd.set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);

	for (unsigned instance = 0; instance < num_instances; instance++)
	{
		cmd.set_program(ocean_info.program);
		cmd.set_vertex_attrib(0, 0, VK_FORMAT_R8G8B8A8_UINT, offsetof(OceanVertex, pos));
		cmd.set_vertex_attrib(1, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(OceanVertex, weights));
		cmd.push_constants(&ocean_info.data, 0, sizeof(ocean_info.data));
		cmd.set_texture(2, 0, *ocean_info.heightmap, Vulkan::StockSampler::LinearWrap);
		cmd.set_texture(2, 1, *ocean_info.lod_map, Vulkan::StockSampler::LinearWrap);
		cmd.set_texture(2, 2, *ocean_info.grad_jacobian, Vulkan::StockSampler::TrilinearWrap);
		cmd.set_texture(2, 3, *ocean_info.normal, Vulkan::StockSampler::TrilinearWrap);

		if (ocean_info.refraction)
		{
			cmd.set_texture(2, 4, *ocean_info.refraction, Vulkan::StockSampler::TrilinearWrap);
			*cmd.allocate_typed_constant_data<RefractionData>(2, 5, 1) = ocean_info.refraction_data;
		}

		for (unsigned lod = 0; lod < ocean_info.lods; lod++)
		{
			cmd.set_uniform_buffer(3, 0, *ocean_info.ubo,
			                       ocean_info.lod_stride * lod,
			                       ocean_info.lod_stride);

			cmd.set_vertex_binding(0, *ocean_info.vbos[lod], 0, 8);
			cmd.set_index_buffer(*ocean_info.ibos[lod], 0, ocean_info.index_type);
			cmd.draw_indexed_indirect(*ocean_info.indirect, 8 * sizeof(uint32_t) * lod, 1, 8 * sizeof(uint32_t));
		}

		cmd.set_program(ocean_info.border_program);
		cmd.set_vertex_attrib(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0);
		cmd.set_vertex_binding(0, *ocean_info.border_vbo, 0, sizeof(vec3));
		cmd.set_index_buffer(*ocean_info.border_ibo, 0, ocean_info.index_type);
		cmd.draw_indexed(ocean_info.border_count);
	}
}
}

void Ocean::get_render_info(const RenderContext &,
                            const RenderInfoComponent *,
                            RenderQueue &queue) const
{
	Util::Hasher hasher;

	auto &ubo = graph->get_physical_buffer_resource(*lod_data);
	auto &indirect = graph->get_physical_buffer_resource(*lod_data_counters);
	auto &lod = graph->get_physical_texture_resource(*ocean_lod);
	auto &normal = graph->get_physical_texture_resource(*normal_fft_output);
	auto &height_displacement = graph->get_physical_texture_resource(*height_displacement_output);
	auto &grad_jacobian = graph->get_physical_texture_resource(*gradient_jacobian_output);

	hasher.string("ocean");
	hasher.u64(lod.get_cookie());
	hasher.u64(normal.get_cookie());
	hasher.u64(height_displacement.get_cookie());
	hasher.u64(grad_jacobian.get_cookie());
	hasher.u64(ubo.get_cookie());
	hasher.u64(indirect.get_cookie());

	if (refraction)
		hasher.u64(refraction->get_cookie());
	else
		hasher.u32(0);

	auto instance_key = hasher.get();

	auto *patch_data = queue.push<OceanInfo>(refraction ?
	                                         Queue::OpaqueEmissive : Queue::Opaque,
	                                         instance_key, 1,
	                                         RenderFunctions::ocean_render,
	                                         nullptr);

	if (patch_data)
	{
		uint32_t refraction_flag = refraction ? 2 : 0;
		if (config.refraction.bandlimited_pixel)
			refraction_flag |= 4;

		patch_data->program =
				queue.get_shader_suites()[Util::ecast(RenderableType::Ocean)].get_program(DrawPipeline::Opaque,
				                                                                          MESH_ATTRIBUTE_POSITION_BIT,
				                                                                          MATERIAL_TEXTURE_BASE_COLOR_BIT,
				                                                                          0 | refraction_flag);

		patch_data->border_program =
				queue.get_shader_suites()[Util::ecast(RenderableType::Ocean)].get_program(DrawPipeline::Opaque,
				                                                                          MESH_ATTRIBUTE_POSITION_BIT,
				                                                                          MATERIAL_TEXTURE_BASE_COLOR_BIT,
				                                                                          1 | refraction_flag);

		patch_data->heightmap = &height_displacement;
		patch_data->lod_map = &lod;
		patch_data->grad_jacobian = &grad_jacobian;
		patch_data->normal = &normal;

		patch_data->ubo = &ubo;
		patch_data->indirect = &indirect;
		patch_data->lod_stride = config.grid_count * config.grid_count * 2 * sizeof(vec4);
		patch_data->lods = unsigned(quad_lod.size());
		patch_data->data.inv_heightmap_size = 1.0f / vec2(config.fft_resolution);
		patch_data->data.inv_ocean_grid_size = 1.0f / vec2(config.grid_count * config.grid_resolution);
		patch_data->data.integer_to_world_mod = get_grid_size() / vec2(config.grid_resolution);
		patch_data->data.normal_uv_scale = vec2(config.normal_mod);
		patch_data->data.heightmap_range = vec2(-10.0f, 10.0f);
		patch_data->data.base_position = vec2(get_grid_base_coord() * int(config.grid_resolution));

		patch_data->border_vbo = border_vbo.get();
		patch_data->border_ibo = border_ibo.get();
		patch_data->index_type = index_type;
		patch_data->border_count = border_count;

		patch_data->refraction = refraction;
		if (refraction)
		{
			patch_data->refraction_data.texture_size = vec4(
					float(refraction->get_image().get_width()),
					float(refraction->get_image().get_height()),
					1.0f / float(refraction->get_image().get_width()),
					1.0f / float(refraction->get_image().get_height()));

			patch_data->refraction_data.uv_scale = config.refraction.uv_scale;

			for (unsigned i = 0; i < MaxOceanLayers; i++)
				patch_data->refraction_data.depths[i] = config.refraction.depth[i];

			patch_data->refraction_data.emissive_mod = config.refraction.emissive_mod;
			patch_data->refraction_data.layers = std::min(4u, refraction->get_create_info().layers);
		}

		for (unsigned i = 0; i < unsigned(quad_lod.size()); i++)
		{
			patch_data->vbos[i] = quad_lod[i].vbo.get();
			patch_data->ibos[i] = quad_lod[i].ibo.get();
		}
	}
}

void Ocean::build_border(vector<vec3> &positions, vector<uint16_t> &indices, ivec2 base,
                         ivec2 dx, ivec2 dy)
{
	unsigned base_index = unsigned(positions.size());
	unsigned position_count = (config.grid_count * 2 + 1) * 2;

	for (unsigned i = 0; i < position_count; i++)
	{
		ivec2 x = int(i >> 1) * dx;
		ivec2 y = int(i & 1) * dy;
		positions.push_back(vec3(ivec3(base + x + y, (i & 1) ^ 1)));
		indices.push_back(uint16_t(base_index++));
	}
	indices.push_back(0xffffu);
}

void Ocean::build_corner(vector<vec3> &positions, vector<uint16_t> &indices,
                         ivec2 base, ivec2 dx, ivec2 dy)
{
	unsigned base_index = unsigned(positions.size());
	for (unsigned i = 0; i < 4; i++)
		indices.push_back(base_index++);
	indices.push_back(0xffffu);

	positions.push_back(vec3(ivec3(base, 1)));
	positions.push_back(vec3(ivec3(base + dx, 0)));
	positions.push_back(vec3(ivec3(base + dy, 0)));
	positions.push_back(vec3(ivec3(base + dx + dy, 0)));
}

void Ocean::build_fill_edge(vector<vec3> &positions, vector<uint16_t> &indices,
                            vec2 base_outer, vec2 end_outer,
                            ivec2 base_inner, ivec2 delta, ivec2 corner_delta)
{
	unsigned base_index = unsigned(positions.size());
	unsigned count = config.grid_count * 2 + 3;

	for (unsigned i = 0; i < count; i++)
	{
		if (i == 0)
			positions.push_back(vec3(vec2(base_inner - corner_delta), 0.0f));
		else if (i + 1 == count)
			positions.push_back(vec3(vec2(base_inner + corner_delta), 0.0f));
		else
			positions.push_back(vec3(vec2(base_inner), 0.0f));

		float outer_lerp = float(i) / float(count - 1);
		vec2 outer_pos = muglm::round(mix(base_outer, end_outer, vec2(outer_lerp)));
		positions.push_back(vec3(outer_pos, 0.0f));

		if ((i + 2 < count) && (i != 0))
			base_inner += delta;

		indices.push_back(uint16_t(base_index++));
		indices.push_back(uint16_t(base_index++));
	}
	indices.push_back(0xffffu);
}

void Ocean::build_lod(Vulkan::Device &device, unsigned size, unsigned stride)
{
	unsigned size_1 = size + 1;
	vector<OceanVertex> vertices;
	vertices.reserve(size_1 * size_1);
	vector<uint16_t> indices;
	indices.reserve(size * (2 * size_1 + 1));

	unsigned half_size = config.grid_resolution >> 1;

	for (unsigned y = 0; y <= config.grid_resolution; y += stride)
	{
		for (unsigned x = 0; x <= config.grid_resolution; x += stride)
		{
			OceanVertex v = {};
			v.pos[0] = uint8_t(x);
			v.pos[1] = uint8_t(y);
			v.pos[2] = uint8_t(x < half_size);
			v.pos[3] = uint8_t(y < half_size);

			if (x == 0)
				v.weights[0] = 255;
			else if (x == config.grid_resolution)
				v.weights[1] = 255;
			else if (y == 0)
				v.weights[2] = 255;
			else if (y == config.grid_resolution)
				v.weights[3] = 255;

			vertices.push_back(v);
		}
	}

	unsigned slices = size;
	for (unsigned slice = 0; slice < slices; slice++)
	{
		unsigned base = slice * size_1;
		for (unsigned x = 0; x <= size; x++)
		{
			indices.push_back(base + x);
			indices.push_back(base + size_1 + x);
		}
		indices.push_back(0xffffu);
	}

	Vulkan::BufferCreateInfo info = {};
	info.size = vertices.size() * sizeof(OceanVertex);
	info.domain = Vulkan::BufferDomain::Device;
	info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

	LOD lod;
	lod.vbo = device.create_buffer(info, vertices.data());

	info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	if (device.get_gpu_properties().vendorID == Vulkan::VENDOR_ID_ARM)
	{
		// Workaround driver bug with primitive restart + 16-bit indices + indirect on some versions.
		// Pad to 32-bit indices to work around this.
		vector<uint32_t> padded_indices;
		padded_indices.reserve(indices.size());
		for (auto &i : indices)
			padded_indices.push_back(i == 0xffffu ? 0xffffffffu : i);
		info.size = padded_indices.size() * sizeof(uint32_t);
		lod.ibo = device.create_buffer(info, padded_indices.data());
	}
	else
	{
		info.size = indices.size() * sizeof(uint16_t);
		lod.ibo = device.create_buffer(info, indices.data());
	}

	lod.count = indices.size();

	quad_lod.push_back(lod);
}

void Ocean::build_buffers(Vulkan::Device &device)
{
	unsigned size = config.grid_resolution;
	unsigned stride = 1;
	while (size >= 2)
	{
		build_lod(device, size, stride);
		size >>= 1;
		stride <<= 1;
	}

	// Build a border mesh.
	vector<vec3> positions;
	vector<uint16_t> indices;

	int outer_delta = int(config.grid_count * config.grid_resolution) >> 3;
	int inner_delta = config.grid_resolution >> 1;

	// Top border
	build_border(positions, indices,
	             ivec2(config.grid_count * config.grid_resolution, 0),
	             ivec2(-inner_delta, 0),
	             ivec2(0, -outer_delta));

	// Left border
	build_border(positions, indices,
	             ivec2(0, 0),
	             ivec2(0, inner_delta),
	             ivec2(-outer_delta, 0));

	// Bottom border
	build_border(positions, indices,
	             ivec2(0, config.grid_count * config.grid_resolution),
	             ivec2(inner_delta, 0),
	             ivec2(0, outer_delta));

	// Right border
	build_border(positions, indices,
	             ivec2(config.grid_count * config.grid_resolution),
	             ivec2(0, -inner_delta),
	             ivec2(outer_delta, 0));

	// Top-left corner
	build_corner(positions, indices,
	             ivec2(0, 0),
	             ivec2(0, -outer_delta),
	             ivec2(-outer_delta, 0));

	// Bottom-left corner
	build_corner(positions, indices,
	             ivec2(0, config.grid_count * config.grid_resolution),
	             ivec2(-outer_delta, 0),
	             ivec2(0, outer_delta));

	// Top-right
	build_corner(positions, indices,
	             ivec2(config.grid_count * config.grid_resolution, 0),
	             ivec2(outer_delta, 0),
	             ivec2(0, -outer_delta));

	// Bottom-right
	build_corner(positions, indices,
	             ivec2(config.grid_count * config.grid_resolution),
	             ivec2(0, outer_delta),
	             ivec2(outer_delta, 0));

	const float neg_edge_size = float(-32 * 1024);
	const float pos_edge_size = float(32 * 1024) + config.grid_count * config.grid_resolution;

	// Top outer ring
	build_fill_edge(positions, indices,
	                vec2(pos_edge_size, neg_edge_size),
	                vec2(neg_edge_size, neg_edge_size),
	                ivec2(config.grid_count * config.grid_resolution, -outer_delta),
	                ivec2(-inner_delta, 0),
	                ivec2(-outer_delta, 0));

	// Left outer ring
	build_fill_edge(positions, indices,
	                vec2(neg_edge_size, neg_edge_size),
	                vec2(neg_edge_size, pos_edge_size),
	                ivec2(-outer_delta, 0),
	                ivec2(0, inner_delta),
	                ivec2(0, outer_delta));

	// Bottom outer ring
	build_fill_edge(positions, indices,
	                vec2(neg_edge_size, pos_edge_size),
	                vec2(pos_edge_size, pos_edge_size),
	                ivec2(0, outer_delta + config.grid_count * config.grid_resolution),
	                ivec2(inner_delta, 0),
	                ivec2(outer_delta, 0));

	// Right outer ring
	build_fill_edge(positions, indices,
	                vec2(pos_edge_size, pos_edge_size),
	                vec2(pos_edge_size, neg_edge_size),
	                ivec2(outer_delta + config.grid_count * config.grid_resolution,
	                      config.grid_count * config.grid_resolution),
	                ivec2(0, -inner_delta),
	                ivec2(0, -outer_delta));

	Vulkan::BufferCreateInfo border_vbo_info;
	border_vbo_info.size = positions.size() * sizeof(ivec3);
	border_vbo_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	border_vbo_info.domain = Vulkan::BufferDomain::Device;
	border_vbo = device.create_buffer(border_vbo_info, positions.data());

	Vulkan::BufferCreateInfo border_ibo_info;
	border_ibo_info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	border_ibo_info.domain = Vulkan::BufferDomain::Device;
	if (device.get_gpu_properties().vendorID == Vulkan::VENDOR_ID_ARM)
	{
		// Workaround driver bug with primitive restart + 16-bit indices + indirect on some versions.
		// Pad to 32-bit indices to work around this.
		vector<uint32_t> padded_indices;
		padded_indices.reserve(indices.size());
		for (auto &i : indices)
			padded_indices.push_back(i == 0xffffu ? 0xffffffffu : i);
		border_ibo_info.size = padded_indices.size() * sizeof(uint32_t);
		border_ibo = device.create_buffer(border_ibo_info, padded_indices.data());
		index_type = VK_INDEX_TYPE_UINT32;
	}
	else
	{
		border_ibo_info.size = indices.size() * sizeof(uint16_t);
		border_ibo = device.create_buffer(border_ibo_info, indices.data());
		index_type = VK_INDEX_TYPE_UINT16;
	}

	border_count = unsigned(indices.size());
}

static inline unsigned square(unsigned x)
{
	return x * x;
}

static inline int alias(int x, int N)
{
	if (x > N / 2)
		x -= N;
	return x;
}

static float phillips(const vec2 &k, float max_l, const vec2 &wind_dir, float L)
{
	float k_len = length(k);
	if (k_len == 0.0f)
		return 0.0f;

	float kL = k_len * L;
	vec2 k_dir = normalize(k);
	float kw = dot(k_dir, wind_dir);

	return
		muglm::pow(kw * kw, 1.0f) *
		muglm::exp(-1.0f * k_len  * k_len * max_l * max_l) *
		muglm::exp(-1.0f / (kL * kL)) *
		muglm::pow(k_len, -4.0f);
}

static void downsample_distribution(vec2 *output, const vec2 *input,
                                    unsigned Nx, unsigned Nz, unsigned rate_log2)
{
	unsigned out_width = Nx >> rate_log2;
	unsigned out_height = Nz >> rate_log2;

	for (unsigned z = 0; z < out_height; z++)
	{
		for (unsigned x = 0; x < out_width; x++)
		{
			int alias_x = alias(x, out_width);
			int alias_z = alias(z, out_height);

			if (alias_x < 0)
				alias_x += Nx;
			if (alias_z < 0)
				alias_z += Nz;

			output[z * out_width + x] = input[alias_z * Nx + alias_x];
		}
	}
}

static void generate_distribution(vec2 *output, const vec2 &mod, unsigned Nx, unsigned Nz,
                                  float amplitude, float max_l, const vec2 &wind_dir, float L)
{
	normal_distribution<float> normal_dist(0.0f, 1.0f);
	default_random_engine engine;

	for (unsigned z = 0; z < Nz; z++)
	{
		for (unsigned x = 0; x < Nx; x++)
		{
			auto &v = output[z * Nx + x];
			vec2 k = mod * vec2(alias(x, Nx), alias(z, Nz));

			vec2 dist;
			dist.x = normal_dist(engine);
			dist.y = normal_dist(engine);

			v = dist * amplitude * muglm::sqrt(0.5f * phillips(k, max_l, wind_dir, L));
		}
	}
}

void Ocean::init_distributions(Vulkan::Device &device)
{
	Vulkan::BufferCreateInfo height_distribution = {};
	height_distribution.domain = Vulkan::BufferDomain::Device;
	height_distribution.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

	auto displacement_distribution = height_distribution;
	auto normal_distribution = height_distribution;

	vector<vec2> init_height(square(config.fft_resolution));
	vector<vec2> init_displacement(square(config.fft_resolution >> config.displacement_downsample));
	vector<vec2> init_normal(square(config.fft_resolution));

	generate_distribution(init_height.data(),
	                      vec2(2.0f * pi<float>()) / heightmap_world_size(),
	                      config.fft_resolution, config.fft_resolution,
	                      config.amplitude, 0.02f, wind_direction, phillips_L);

	generate_distribution(init_normal.data(),
	                      vec2(2.0f * pi<float>()) / normalmap_world_size(),
	                      config.fft_resolution, config.fft_resolution,
	                      config.amplitude * config.normal_mod, 0.02f, wind_direction, phillips_L);

	downsample_distribution(init_displacement.data(), init_height.data(),
	                        config.fft_resolution, config.fft_resolution,
	                        config.displacement_downsample);

	height_distribution.size = init_height.size() * sizeof(vec2);
	normal_distribution.size = init_normal.size() * sizeof(vec2);
	displacement_distribution.size = init_displacement.size() * sizeof(vec2);

	distribution_buffer = device.create_buffer(height_distribution,
	                                           init_height.data());
	distribution_buffer_displacement = device.create_buffer(displacement_distribution,
	                                                        init_displacement.data());
	distribution_buffer_normal = device.create_buffer(normal_distribution,
	                                                  init_normal.data());
}

}