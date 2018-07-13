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

#include "fft/glfft_granite_interface.hpp"
#include "ocean.hpp"
#include "device.hpp"
#include "renderer.hpp"
#include "render_context.hpp"
#include "render_graph.hpp"
#include "muglm/matrix_helper.hpp"

using namespace std;

namespace Granite
{
Ocean::Ocean()
{
	EVENT_MANAGER_REGISTER_LATCH(Ocean, on_device_created, on_device_destroyed, Vulkan::DeviceCreatedEvent);
	EVENT_MANAGER_REGISTER(Ocean, on_frame_tick, FrameTickEvent);
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
	options.performance.shared_banked = true;
	options.performance.workgroup_size_x = 64;
	options.performance.workgroup_size_y = 1;
	options.performance.vector_size = 2;

	auto cache = make_shared<GLFFT::ProgramCache>();

	height_fft.reset(new GLFFT::FFT(&fft_iface, height_fft_size, height_fft_size,
	                                GLFFT::ComplexToReal, GLFFT::Inverse,
	                                GLFFT::SSBO, GLFFT::ImageReal,
	                                cache, options));

	displacement_fft.reset(new GLFFT::FFT(&fft_iface, displacement_fft_size, displacement_fft_size,
	                                      GLFFT::ComplexToComplex, GLFFT::Inverse,
	                                      GLFFT::SSBO, GLFFT::Image,
	                                      cache, options));

	normal_fft.reset(new GLFFT::FFT(&fft_iface, normal_fft_size, normal_fft_size,
	                                GLFFT::ComplexToComplex, GLFFT::Inverse,
	                                GLFFT::SSBO, GLFFT::Image,
	                                cache, options));
}

void Ocean::on_device_destroyed(const Vulkan::DeviceCreatedEvent &)
{
	vertex_mip_views.clear();
	fragment_mip_views.clear();

	height_fft.reset();
	normal_fft.reset();
	displacement_fft.reset();
	distribution_buffer.reset();
}

void Ocean::refresh(RenderContext &)
{
}

void Ocean::set_base_renderer(Renderer *,
                              Renderer *,
                              Renderer *)
{
}

void Ocean::set_base_render_context(const RenderContext *context)
{
	this->context = context;
}

void Ocean::set_scene(Scene *)
{
}

void Ocean::setup_render_pass_dependencies(RenderGraph &graph, RenderPass &target)
{

}

void Ocean::setup_render_pass_resources(RenderGraph &graph)
{

}

void Ocean::update_lod_pass(Vulkan::CommandBuffer &cmd)
{

}

void Ocean::update_fft_input(Vulkan::CommandBuffer &cmd)
{
	auto *program = cmd.get_device().get_shader_manager().register_compute("builtin://shaders/ocean/generate_fft.comp");
	unsigned height_variant = program->register_variant({});
	unsigned normal_variant = program->register_variant({{ "GRADIENT_NORMAL", 1 }});
	unsigned displacement_variant = program->register_variant({{ "GRADIENT_DISPLACEMENT", 1 }});

	cmd.set_storage_buffer(0, 0, *distribution_buffer);

	struct Push
	{
		vec2 mod;
		uvec2 N;
		float time;
	};
	Push push;
	push.mod = vec2(2.0f * pi<float>()) / size;
	push.time = float(current_time);

	cmd.set_program(*program->get_program(height_variant));
	push.N = uvec2(height_fft_size, height_fft_size);
	cmd.set_storage_buffer(0, 1, graph->get_physical_buffer_resource(*height_fft_input));
	cmd.push_constants(&push, 0, sizeof(push));
	cmd.dispatch(height_fft_size / 64, 1, 1);

	cmd.set_program(*program->get_program(displacement_variant));
	push.N = uvec2(normal_fft_size, normal_fft_size);
	cmd.set_storage_buffer(0, 1, graph->get_physical_buffer_resource(*displacement_fft_input));
	cmd.push_constants(&push, 0, sizeof(push));
	cmd.dispatch(displacement_fft_size / 64, 1, 1);

	push.mod = vec2(2.0f * pi<float>()) / size_normal;
	cmd.set_program(*program->get_program(normal_variant));
	push.N = uvec2(normal_fft_size, normal_fft_size);
	cmd.set_storage_buffer(0, 1, graph->get_physical_buffer_resource(*normal_fft_input));
	cmd.push_constants(&push, 0, sizeof(push));
	cmd.dispatch(normal_fft_size / 64, 1, 1);
}

void Ocean::compute_fft(Vulkan::CommandBuffer &cmd)
{
	FFTCommandBuffer cmd_wrapper(&cmd);

	FFTTexture height_output(&graph->get_physical_texture_resource(*height_fft_output));
	FFTBuffer height_input(&graph->get_physical_buffer_resource(*height_fft_input));
	height_fft->process(&cmd_wrapper, &height_output, &height_input);

	FFTTexture normal_output(&graph->get_physical_texture_resource(*normal_fft_output));
	FFTBuffer normal_input(&graph->get_physical_buffer_resource(*normal_fft_input));
	normal_fft->process(&cmd_wrapper, &normal_output, &normal_input);

	FFTTexture displacement_output(&graph->get_physical_texture_resource(*displacement_fft_output));
	FFTBuffer displacement_input(&graph->get_physical_buffer_resource(*displacement_fft_input));
	displacement_fft->process(&cmd_wrapper, &displacement_output, &displacement_input);
}

void Ocean::update_fft_pass(Vulkan::CommandBuffer &cmd)
{
	update_fft_input(cmd);

	cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
	            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

	compute_fft(cmd);

	cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
	            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
}

void Ocean::add_render_passes(RenderGraph &graph)
{
	this->graph = &graph;

	auto &update_lod = graph.add_pass("ocean-update-lods", RENDER_GRAPH_QUEUE_COMPUTE_BIT);
	AttachmentInfo lod_attachment;
	lod_attachment.format = VK_FORMAT_R16_SFLOAT;
	lod_attachment.size_x = float(grid_width);
	lod_attachment.size_y = float(grid_height);
	lod_attachment.size_class = SizeClass::Absolute;
	ocean_lod = &update_lod.add_storage_texture_output("ocean-lods", lod_attachment);

	update_lod.set_build_render_pass([&](Vulkan::CommandBuffer &cmd) {
		update_lod_pass(cmd);
	});

	BufferInfo normal_info, height_info, displacement_info;
	normal_info.size = normal_fft_size * normal_fft_size * sizeof(uint32_t);
	height_info.size = height_fft_size * height_fft_size * sizeof(uint32_t);
	displacement_info.size = displacement_fft_size * displacement_fft_size * sizeof(uint32_t);

	AttachmentInfo normal_map;
	AttachmentInfo height_map;
	AttachmentInfo displacement_map;

	normal_map.size_class = SizeClass::Absolute;
	normal_map.size_x = float(normal_fft_size);
	normal_map.size_y = float(normal_fft_size);
	normal_map.format = VK_FORMAT_R16G16_SFLOAT;

	displacement_map.size_class = SizeClass::Absolute;
	displacement_map.size_x = float(displacement_fft_size);
	displacement_map.size_y = float(displacement_fft_size);
	displacement_map.format = VK_FORMAT_R16G16_SFLOAT;

	height_map.size_class = SizeClass::Absolute;
	height_map.size_x = float(height_fft_size);
	height_map.size_y = float(height_fft_size);
	height_map.format = VK_FORMAT_R16_SFLOAT;

	auto &update_fft = graph.add_pass("ocean-update-fft", RENDER_GRAPH_QUEUE_COMPUTE_BIT);

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
	height_displacement.size_x = float(height_fft_size);
	height_displacement.size_y = float(height_fft_size);
	height_displacement.format = VK_FORMAT_R16G16B16A16_SFLOAT;

	height_displacement_output =
			&update_fft.add_storage_texture_output("ocean-height-displacement-output",
			                                       height_displacement);
	gradient_jacobian_output =
			&update_fft.add_storage_texture_output("ocean-gradient-jacobian-output",
			                                       height_displacement);

	update_fft.set_build_render_pass([&](Vulkan::CommandBuffer &cmd) {
		update_fft_pass(cmd);
	});
}

void Ocean::get_render_info(const RenderContext &context,
                            const CachedSpatialTransformComponent *,
                            RenderQueue &queue) const
{

}

}