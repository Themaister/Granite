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

#include "volumetric_diffuse.hpp"
#include "device.hpp"
#include "scene.hpp"
#include "task_composer.hpp"
#include "render_context.hpp"
#include "muglm/matrix_helper.hpp"
#include "quirks.hpp"
#include "clusterer.hpp"

namespace Granite
{
static constexpr float ZNear = 0.1f;
static constexpr float ZFar = 200.0f;
static constexpr unsigned NumProbeLayers = 4;

// Works well with 8x8 workgroup. Each partial face is 4x4,
// which fits well with clustered add per quadrant,
// and even Intel iGPU can work well here with SIMD16.
static constexpr unsigned ProbeResolution = 8;

static constexpr unsigned ProbeDownsamplingFactor = 16;

VolumetricDiffuseLightManager::VolumetricDiffuseLightManager()
{
	mat4 proj, view;
	compute_cube_render_transform(vec3(0.0f), 0, proj, view, ZNear, ZFar);
	mat2 inv_projection = inverse(mat2(proj[2].zw(), proj[3].zw()));
	inv_projection_zw = vec4(inv_projection[0], inv_projection[1]);

	probe_pos_jitter[0] = vec4(-3.0f / 16.0f, +1.0f / 16.0f, +5.0f / 16.0f, 0.0f);
	probe_pos_jitter[1] = vec4(+1.0f / 16.0f, -3.0f / 16.0f, +3.0f / 16.0f, 0.0f);
	probe_pos_jitter[2] = vec4(-1.0f / 16.0f, +3.0f / 16.0f, -5.0f / 16.0f, 0.0f);
	probe_pos_jitter[3] = vec4(+3.0f / 16.0f, -1.0f / 16.0f, -3.0f / 16.0f, 0.0f);

	EVENT_MANAGER_REGISTER_LATCH(VolumetricDiffuseLightManager, on_device_created, on_device_destroyed, Vulkan::DeviceCreatedEvent);
}

void VolumetricDiffuseLightManager::on_device_created(const Vulkan::DeviceCreatedEvent &e)
{
	auto &device = e.get_device();

	auto info = Vulkan::ImageCreateInfo::immutable_2d_image(128, 128, VK_FORMAT_R16G16B16A16_SFLOAT);
	info.layers = 6;
	info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
	info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
	info.initial_layout = VK_IMAGE_LAYOUT_GENERAL;
	sky_light = device.create_image(info);
	sky_light->set_layout(Vulkan::Layout::General);
	device.set_name(*sky_light, "sky-light");

	Vulkan::ImageViewCreateInfo view = {};
	view.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	view.layers = 6;
	view.levels = 1;
	view.image = sky_light.get();
	view.view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
	sky_light_2d_array = device.create_image_view(view);

	Vulkan::BufferCreateInfo buf_info = {};
	buf_info.size = sizeof(uint16_t) * 4 * 6;
	buf_info.usage = VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;
	buf_info.domain = Vulkan::BufferDomain::Device;
	fallback_volume = device.create_buffer(buf_info);
	Vulkan::BufferViewCreateInfo view_info = {};
	view_info.buffer = fallback_volume.get();
	view_info.range = VK_WHOLE_SIZE;
	view_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	fallback_volume_view = device.create_buffer_view(view_info);
}

void VolumetricDiffuseLightManager::on_device_destroyed(const Vulkan::DeviceCreatedEvent &)
{
	sky_light.reset();
	sky_light_2d_array.reset();
	fallback_volume.reset();
	fallback_volume_view.reset();
}

void VolumetricDiffuseLightManager::set_scene(Scene *scene_)
{
	scene = scene_;
	volumetric_diffuse = &scene->get_entity_pool().get_component_group<VolumetricDiffuseLightComponent>();
}

void VolumetricDiffuseLightManager::set_base_renderer(const RendererSuite *suite_)
{
	suite = suite_;
}

enum class TransitionMode { Discard, Read };

static void transition_gbuffer(Vulkan::CommandBuffer &cmd,
                               const VolumetricDiffuseLight::GBuffer &gbuffer,
                               TransitionMode mode)
{
	const Vulkan::Image *colors[] = {
		gbuffer.emissive.get(),
		gbuffer.albedo.get(),
		gbuffer.normal.get(),
		gbuffer.pbr.get(),
	};

	VkPipelineStageFlags2 src_color, src_depth, dst_color, dst_depth;
	VkAccessFlags2 src_access_color, src_access_depth, dst_access_color, dst_access_depth;
	VkImageLayout old_color, new_color, old_depth, new_depth;

	bool compute = (gbuffer.emissive->get_create_info().usage & VK_IMAGE_USAGE_STORAGE_BIT) != 0;

	if (mode == TransitionMode::Read && compute)
	{
		cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
		            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		            VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
		return;
	}

	if (mode == TransitionMode::Discard)
	{
		if (compute)
		{
			src_color = VK_PIPELINE_STAGE_NONE;
			src_depth = VK_PIPELINE_STAGE_NONE;
			dst_color = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
			dst_depth = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		}
		else
		{
			src_color = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
			src_depth = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
			dst_color = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dst_depth = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		}

		src_access_color = 0;
		src_access_depth = 0;

		if (compute)
		{
			dst_access_color = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT |
			                   VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
			dst_access_depth = dst_access_color;
		}
		else
		{
			dst_access_color = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
			dst_access_depth = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
			                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
		}

		old_color = VK_IMAGE_LAYOUT_UNDEFINED;
		old_depth = VK_IMAGE_LAYOUT_UNDEFINED;

		if (compute)
		{
			new_color = VK_IMAGE_LAYOUT_GENERAL;
			new_depth = VK_IMAGE_LAYOUT_GENERAL;
		}
		else
		{
			new_color = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			new_depth = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		}
	}
	else
	{
		src_color = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		src_depth = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		src_access_color = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		src_access_depth = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

		dst_color = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		dst_depth = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		dst_access_color = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
		dst_access_depth = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;

		old_color = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		old_depth = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		new_color = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		new_depth = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}

	for (auto *image : colors)
	{
		cmd.image_barrier(*image, old_color, new_color,
		                  src_color, src_access_color,
		                  dst_color, dst_access_color);
	}

	cmd.image_barrier(*gbuffer.depth, old_depth, new_depth,
	                  src_depth, src_access_depth,
	                  dst_depth, dst_access_depth);
}

static unsigned layer_to_probe_jitter(unsigned layer, unsigned x, unsigned y)
{
	return (layer + (y & 1) * 2 + (x & 1)) % NumProbeLayers;
}

void VolumetricDiffuseLightManager::average_probe_buffer(Vulkan::CommandBuffer &cmd,
                                                         VolumetricDiffuseLightComponent &light)
{
	cmd.set_program("builtin://shaders/lights/volumetric_light_average.comp");
	cmd.set_storage_texture(0, 0, *light.light.get_volume_view());
	for (unsigned i = 0; i < NumProbeLayers; i++)
		cmd.set_storage_texture(0, 1 + i, *light.light.get_accumulation_view(i));

	uvec3 resolution = light.light.get_resolution();
	resolution.x *= 6;
	cmd.push_constants(&resolution, 0, sizeof(resolution));

	cmd.dispatch((resolution.x + 3) / 4, (resolution.y + 3) / 4, (resolution.z + 3) / 4);
}

void VolumetricDiffuseLightManager::light_probe_buffer(Vulkan::CommandBuffer &cmd,
                                                       VolumetricDiffuseLightComponent &light)
{
	struct Push
	{
		uint32_t gbuffer_layer;
		uint32_t face_resolution;
		float inv_orig_face_resolution;
		float inv_patch_resolution2;
		uint32_t hash_range;
	} push = {};

	struct ProbeTransform
	{
		alignas(16) vec4 texture_to_world[3];
		alignas(16) vec4 world_to_texture[3];
		alignas(16) vec3 inv_resolution;
		alignas(8) uvec2 probe_size_xy;
	};

	push.gbuffer_layer = light.update_iteration % NumProbeLayers;
	light.update_iteration++;

	auto *probe_transform = cmd.allocate_typed_constant_data<ProbeTransform>(3, 1, 1);
	memcpy(probe_transform->texture_to_world, light.texture_to_world, sizeof(light.texture_to_world));
	memcpy(probe_transform->world_to_texture, light.world_to_texture, sizeof(light.world_to_texture));
	probe_transform->inv_resolution = vec3(1.0f) / vec3(light.light.get_resolution());
	probe_transform->probe_size_xy = light.light.get_resolution().xy();

	push.face_resolution = ProbeResolution;
	push.inv_orig_face_resolution = 1.0f / float(ProbeResolution * ProbeDownsamplingFactor);
	float inv_patch_resolution = 2.0f / float(ProbeResolution);
	push.inv_patch_resolution2 = inv_patch_resolution * inv_patch_resolution;
	push.hash_range = ProbeDownsamplingFactor;

	auto flags = Renderer::get_mesh_renderer_options_from_lighting(*fallback_render_context->get_lighting_parameters());
	flags &= ~(Renderer::VOLUMETRIC_FOG_ENABLE_BIT |
	           Renderer::AMBIENT_OCCLUSION_BIT |
	           Renderer::SHADOW_CASCADE_ENABLE_BIT);
	auto defines = Renderer::build_defines_from_renderer_options(RendererType::GeneralForward, flags);

	Renderer::add_subgroup_defines(cmd.get_device(), defines, VK_SHADER_STAGE_COMPUTE_BIT);

	// Need at least SIMD16 to ensure that we can use ClusteredAdd without having to go through shared memory.
	if (cmd.get_device().supports_subgroup_size_log2(true, 4, 6))
	{
		defines.emplace_back("SUBGROUP_COMPUTE_FULL", 1);
		cmd.set_subgroup_size_log2(true, 4, 6);
		cmd.enable_subgroup_size_control(true);
	}

	cmd.set_program("builtin://shaders/lights/volumetric_hemisphere_integral.comp", defines);

	cmd.push_constants(&push, 0, sizeof(push));
	cmd.set_storage_texture(2, 0, *light.light.get_accumulation_view(push.gbuffer_layer));
	cmd.set_texture(2, 1, light.light.get_gbuffer().emissive->get_view());
	cmd.set_texture(2, 2, light.light.get_gbuffer().albedo->get_view());
	cmd.set_texture(2, 3, light.light.get_gbuffer().normal->get_view());
	cmd.set_texture(2, 4, light.light.get_gbuffer().depth->get_view());
	cmd.set_storage_buffer(2, 5, *light.light.get_worklist_buffer());
	cmd.set_texture(2, 6, sky_light->get_view(), Vulkan::StockSampler::LinearClamp);
	cmd.dispatch_indirect(*light.light.get_atomic_buffer(), 0);
	cmd.enable_subgroup_size_control(false);
}

struct VolumetricDiffuseLightManager::ContextRenderers
{
	RenderContext contexts;
	RenderPassSceneRenderer renderers;
	VolumetricDiffuseLight::GBuffer gbuffer;
};

static VolumetricDiffuseLight::GBuffer allocate_gbuffer(Vulkan::Device &device, unsigned width, unsigned height,
                                                        unsigned layers, bool compute)
{
	VolumetricDiffuseLight::GBuffer allocated_gbuffer;
	auto gbuffer_info = Vulkan::ImageCreateInfo::render_target(width, height, VK_FORMAT_R8G8B8A8_SRGB);
	gbuffer_info.layers = layers;
	gbuffer_info.usage = (compute ? VK_IMAGE_USAGE_STORAGE_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) |
	                     VK_IMAGE_USAGE_SAMPLED_BIT;
	gbuffer_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	gbuffer_info.flags = compute ? VK_IMAGE_CREATE_EXTENDED_USAGE_BIT : 0;
	gbuffer_info.misc = Vulkan::IMAGE_MISC_MUTABLE_SRGB_BIT;
	allocated_gbuffer.albedo = device.create_image(gbuffer_info);
	gbuffer_info.flags = 0;
	gbuffer_info.misc = 0;

	gbuffer_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	allocated_gbuffer.emissive = device.create_image(gbuffer_info);

	gbuffer_info.format = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
	allocated_gbuffer.normal = device.create_image(gbuffer_info);

	gbuffer_info.format = VK_FORMAT_R8G8_UNORM;
	allocated_gbuffer.pbr = device.create_image(gbuffer_info);

	gbuffer_info.format = compute ? VK_FORMAT_R16_SFLOAT : device.get_default_depth_stencil_format();
	gbuffer_info.usage = (compute ? VK_IMAGE_USAGE_STORAGE_BIT : VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) |
	                     VK_IMAGE_USAGE_SAMPLED_BIT;
	allocated_gbuffer.depth = device.create_image(gbuffer_info);

	device.set_name(*allocated_gbuffer.emissive, "probe-emissive");
	device.set_name(*allocated_gbuffer.albedo, "probe-albedo");
	device.set_name(*allocated_gbuffer.normal, "probe-normal");
	device.set_name(*allocated_gbuffer.pbr, "probe-pbr");
	device.set_name(*allocated_gbuffer.depth, "probe-depth");

	if (compute)
	{
		allocated_gbuffer.emissive->set_layout(Vulkan::Layout::General);
		allocated_gbuffer.depth->set_layout(Vulkan::Layout::General);
		allocated_gbuffer.albedo->set_layout(Vulkan::Layout::General);
		allocated_gbuffer.normal->set_layout(Vulkan::Layout::General);
		allocated_gbuffer.pbr->set_layout(Vulkan::Layout::General);
	}

	return allocated_gbuffer;
}

void VolumetricDiffuseLightManager::setup_cube_renderer(ContextRenderers &renderers,
                                                        Vulkan::Device &device,
                                                        const RenderPassSceneRenderer::Setup &base,
                                                        unsigned layers)
{
	RenderPassSceneRenderer::Setup setup = base;
	setup.context = &renderers.contexts;
	renderers.renderers.init(setup);
	renderers.renderers.set_extra_flush_flags(Renderer::FRONT_FACE_CLOCKWISE_BIT);

	renderers.gbuffer = allocate_gbuffer(device, ProbeResolution * ProbeDownsamplingFactor * 6,
	                                     ProbeResolution * ProbeDownsamplingFactor, layers,
	                                     false);
}

static void copy_gbuffer(Vulkan::CommandBuffer &cmd,
                         const VolumetricDiffuseLight::GBuffer &dst, const VolumetricDiffuseLight::GBuffer &src,
                         unsigned resolution_x, unsigned y, unsigned layer)
{
	struct Push
	{
		uint32_t y, layer, res, downsampling;
	} push = { y, layer, ProbeResolution, ProbeDownsamplingFactor };

	cmd.set_program("builtin://shaders/lights/volumetric_gbuffer_copy.comp");

	cmd.push_constants(&push, 0, sizeof(push));
	cmd.set_specialization_constant_mask(1);

	cmd.set_storage_texture(0, 0, dst.emissive->get_view());
	cmd.set_texture(0, 1, src.emissive->get_view());
	cmd.set_specialization_constant(0, 0u);
	cmd.dispatch((6 * ProbeResolution) / 8, ProbeResolution / 8, resolution_x);

	cmd.set_unorm_storage_texture(0, 0, dst.albedo->get_view());
	cmd.set_unorm_texture(0, 1, src.albedo->get_view());
	cmd.set_specialization_constant(0, 0u);
	cmd.dispatch((6 * ProbeResolution) / 8, ProbeResolution / 8, resolution_x);

	cmd.set_storage_texture(0, 0, dst.normal->get_view());
	cmd.set_texture(0, 1, src.normal->get_view());
	cmd.set_specialization_constant(0, 0u);
	cmd.dispatch((6 * ProbeResolution) / 8, ProbeResolution / 8, resolution_x);

	cmd.set_storage_texture(0, 0, dst.pbr->get_view());
	cmd.set_texture(0, 1, src.pbr->get_view());
	cmd.set_specialization_constant(0, 0u);
	cmd.dispatch((6 * ProbeResolution) / 8, ProbeResolution / 8, resolution_x);

	cmd.set_storage_texture(0, 0, dst.depth->get_view());
	cmd.set_texture(0, 1, src.depth->get_view());
	cmd.set_specialization_constant(0, 1u);
	cmd.dispatch((6 * ProbeResolution) / 8, ProbeResolution / 8, resolution_x);

	cmd.set_specialization_constant_mask(0);
}

static std::atomic_uint32_t probe_render_count;

void VolumetricDiffuseLightManager::render_probe_gbuffer_slice(VolumetricDiffuseLightComponent &light,
                                                               Vulkan::Device &device,
                                                               ContextRenderers &renderers, unsigned z)
{
	auto resolution = light.light.get_resolution();

	for (unsigned layer = 0; layer < NumProbeLayers; layer++)
	{
		for (unsigned y = 0; y < resolution.y; y++)
		{
			auto cmd = device.request_command_buffer();
			transition_gbuffer(*cmd, renderers.gbuffer, TransitionMode::Discard);

			Vulkan::RenderPassInfo rp;
			memset(rp.clear_color, 0, sizeof(rp.clear_color));
			rp.clear_depth_stencil.depth = 1.0f;
			rp.clear_depth_stencil.stencil = 0;
			rp.clear_attachments = 0xf;
			rp.store_attachments = 0xf;
			rp.op_flags = Vulkan::RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT |
			              Vulkan::RENDER_PASS_OP_STORE_DEPTH_STENCIL_BIT;
			rp.num_color_attachments = 4;

			auto &gbuffer = renderers.gbuffer;
			rp.color_attachments[0] = &gbuffer.emissive->get_view();
			rp.color_attachments[1] = &gbuffer.albedo->get_view();
			rp.color_attachments[2] = &gbuffer.normal->get_view();
			rp.color_attachments[3] = &gbuffer.pbr->get_view();
			rp.depth_stencil = &gbuffer.depth->get_view();

			for (unsigned x = 0; x < resolution.x; x++)
			{
				cmd->begin_region("render-probe-gbuffer");

				rp.render_area.offset.x = 0;
				rp.render_area.offset.y = 0;
				rp.render_area.extent.width = ProbeDownsamplingFactor * ProbeResolution * 6;
				rp.render_area.extent.height = ProbeDownsamplingFactor * ProbeResolution;
				rp.base_layer = x;

				cmd->begin_render_pass(rp);
				rp.render_area.extent.width = ProbeResolution * ProbeDownsamplingFactor;

				for (unsigned face = 0; face < 6; face++)
				{
					vec3 tex = (vec3(x, y, z) + 0.5f + probe_pos_jitter[layer_to_probe_jitter(layer, x, y)].xyz()) / vec3(resolution);
					vec3 center = vec3(
							dot(light.texture_to_world[0], vec4(tex, 1.0f)),
							dot(light.texture_to_world[1], vec4(tex, 1.0f)),
							dot(light.texture_to_world[2], vec4(tex, 1.0f)));

					mat4 proj, view;
					compute_cube_render_transform(center, face, proj, view, ZNear, ZFar);
					renderers.contexts.set_camera(proj, view);
					renderers.renderers.prepare_render_pass();

					const VkViewport vp = {
						float(rp.render_area.offset.x),
						float(rp.render_area.offset.y),
						float(rp.render_area.extent.width),
						float(rp.render_area.extent.height),
						0.0f, 1.0f,
					};
					cmd->set_viewport(vp);
					cmd->set_scissor(rp.render_area);
					renderers.renderers.build_render_pass(*cmd);
					rp.render_area.offset.x += ProbeResolution * ProbeDownsamplingFactor;
				}

				cmd->end_render_pass();
				cmd->end_region();

#ifdef VULKAN_DEBUG
				LOGI("Rendering gbuffer probe ... X = %u, Y = %u, Z = %u, layer = %u.\n",
				     x, y, z, layer);
#endif
			}

			transition_gbuffer(*cmd, renderers.gbuffer, TransitionMode::Read);

			*cmd->allocate_typed_constant_data<vec4>(0, 2, 1) = inv_projection_zw;
			copy_gbuffer(*cmd, light.light.get_gbuffer(), renderers.gbuffer,
			             resolution.x, z * resolution.y + y, layer);

			device.submit(cmd);

			if ((probe_render_count.fetch_add(1, std::memory_order_relaxed) & 7) == 7)
			{
				// We're going to be consuming a fair bit of memory,
				// so make sure to pump frame contexts through.
				// This code is not assumed to be hot (should be pre-baked).
				device.next_frame_context();
			}
		}
	}

	device.next_frame_context();
}

TaskGroupHandle VolumetricDiffuseLightManager::create_probe_gbuffer(TaskComposer &composer, TaskGroup &incoming,
                                                                    const RenderContext &context,
                                                                    VolumetricDiffuseLightComponent &light)
{
	auto &device = context.get_device();

	uvec3 resolution = light.light.get_resolution();
	auto allocated_gbuffer = allocate_gbuffer(device, ProbeResolution * resolution.x * 6,
	                                          ProbeResolution * resolution.y * resolution.z,
	                                          NumProbeLayers,
	                                          true);

	light.light.set_probe_gbuffer(std::move(allocated_gbuffer));

	Vulkan::BufferCreateInfo atomics_info = {};
	atomics_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
	atomics_info.size = 16;
	atomics_info.domain = Vulkan::BufferDomain::Device;

	Vulkan::BufferCreateInfo list_info = {};
	list_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	list_info.size = sizeof(uint32_t) * resolution.x * resolution.y * resolution.z;
	list_info.domain = Vulkan::BufferDomain::Device;
	light.light.set_buffers(device.create_buffer(atomics_info), device.create_buffer(list_info));

	auto setup = std::make_shared<RenderPassSceneRenderer::Setup>();
	setup->flags = SCENE_RENDERER_DEFERRED_GBUFFER_BIT |
	               SCENE_RENDERER_SKIP_UNBOUNDED_BIT |
	               SCENE_RENDERER_SKIP_OPAQUE_FLOATING_BIT;
	setup->deferred_lights = nullptr;
	setup->suite = suite;
	setup->scene = scene;

	TaskComposer probe_composer(*incoming.get_thread_group());
	probe_composer.set_incoming_task(composer.get_pipeline_stage_dependency());

	auto &discard_stage = probe_composer.begin_pipeline_stage();
	discard_stage.enqueue_task([&device, &light]() {
		auto cmd = device.request_command_buffer();
		transition_gbuffer(*cmd, light.light.get_gbuffer(), TransitionMode::Discard);
		device.submit(cmd);
	});

	auto &render_stage = probe_composer.begin_pipeline_stage();
	render_stage.set_desc("probe-render-stage");

	for (unsigned z = 0; z < resolution.z; z++)
	{
		render_stage.enqueue_task([this, z, &light, setup, &device]() {
			ContextRenderers renderers;
			setup_cube_renderer(renderers, device, *setup, light.light.get_resolution().x);
			render_probe_gbuffer_slice(light, device, renderers, z);
		});
	}

	auto &task = probe_composer.begin_pipeline_stage();
	task.enqueue_task([&device, &light]() {
		auto cmd = device.request_command_buffer();
		transition_gbuffer(*cmd, light.light.get_gbuffer(), TransitionMode::Read);
		uvec3 res = light.light.get_resolution();

		if (!light.light.get_volume_view())
		{
			auto info = Vulkan::ImageCreateInfo::immutable_3d_image(6 * res.x, res.y, res.z, VK_FORMAT_R16G16B16A16_SFLOAT);
			info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
			info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

			auto image = device.create_image(info);
			auto prev_image = device.create_image(info);
			device.set_name(*image, "probe-light-1");
			device.set_name(*prev_image, "probe-light-2");
			image->set_layout(Vulkan::Layout::General);
			prev_image->set_layout(Vulkan::Layout::General);

			Util::SmallVector<Vulkan::ImageHandle> layer_accums(NumProbeLayers);
			{
				unsigned counter = 0;
				for (auto &layer : layer_accums)
				{
					layer = device.create_image(info);
					device.set_name(*layer, (std::string("probe-accum-") + std::to_string(counter++)).c_str());
					layer->set_layout(Vulkan::Layout::General);
				}
			}

			const auto clear = [](Vulkan::CommandBuffer &clear_cmd, Vulkan::Image &clear_image) {
				clear_cmd.image_barrier(clear_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
				                        VK_PIPELINE_STAGE_NONE, 0,
				                        VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
				clear_cmd.clear_image(clear_image, {});
				clear_cmd.image_barrier(clear_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
				                        VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
				                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				                        VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
			};

			clear(*cmd, *image);
			clear(*cmd, *prev_image);
			for (auto &layer : layer_accums)
				clear(*cmd, *layer);

			light.light.set_volumes(std::move(image), std::move(prev_image));
			light.light.set_accumulation_volumes(std::move(layer_accums));
		}

		device.submit(cmd);
		device.next_frame_context();
	});

	return probe_composer.get_outgoing_task();
}

void VolumetricDiffuseLightManager::refresh(const RenderContext &context, TaskComposer &composer)
{
	if (!volumetric_diffuse)
		return;
	auto &group = composer.begin_pipeline_stage();

	for (auto &light_tuple : *volumetric_diffuse)
	{
		auto *light = get_component<VolumetricDiffuseLightComponent>(light_tuple);
		light->light.swap_volumes();

		if (!light->light.get_gbuffer().emissive)
		{
			auto task = create_probe_gbuffer(composer, group, context, *light);
			composer.get_thread_group().add_dependency(group, *task);
		}
	}
}

void VolumetricDiffuseLightManager::message(const std::string &, uint32_t, uint32_t x, uint32_t y, uint32_t z,
                                            uint32_t word_count, const Vulkan::DebugChannelInterface::Word *words)
{
	LOGI("Probe: (%u, %u, %u)\n", x, y, z);
	for (uint32_t i = 0; i < word_count; i++)
	{
		LOGI("  %f\n", words[i].f32);
	}
}

void VolumetricDiffuseLightManager::cull_probe_buffer(Vulkan::CommandBuffer &cmd,
                                                      VolumetricDiffuseLightComponent &light)
{
	cmd.set_storage_buffer(0, 0, *light.light.get_atomic_buffer());
	cmd.set_storage_buffer(0, 1, *light.light.get_worklist_buffer());
	cmd.set_storage_texture(0, 2, *light.light.get_volume_view());
	cmd.set_texture(0, 3, *light.light.get_prev_volume_view());
	uvec3 res = light.light.get_resolution();

	struct VolumeParameters
	{
		vec4 tex_to_world[3];
		vec3 inv_resolution;
		float radius;
		uvec3 resolution;
		uint32_t iteration;
	};
	auto *params = cmd.allocate_typed_constant_data<VolumeParameters>(1, 0, 1);
	memcpy(params->tex_to_world, light.texture_to_world, sizeof(light.texture_to_world));

	vec3 inv_resolution = vec3(1.0f) / vec3(res);
	params->inv_resolution = inv_resolution;

	vec3 radius;
	for (int i = 0; i < 3; i++)
	{
		radius[i] = inv_resolution[i] *
		            length(vec3(light.texture_to_world[0][i],
		                        light.texture_to_world[1][i],
		                        light.texture_to_world[2][i]));
	}
	params->radius = length(radius);
	params->resolution = res;
	params->iteration = light.update_iteration;

	memcpy(cmd.allocate_typed_constant_data<vec4>(1, 1, 6),
	       base_render_context->get_visibility_frustum().get_planes(),
	       6 * sizeof(vec4));

	cmd.dispatch((res.x + 3) / 4, (res.y + 3) / 4, (res.z + 3) / 4);
}

void VolumetricDiffuseLightManager::update_fallback_volume(Vulkan::CommandBuffer &cmd)
{
	cmd.set_program("builtin://shaders/lights/volumetric_light_compute_fallback.comp");
	cmd.set_storage_buffer_view(0, 0, *fallback_volume_view);
	cmd.set_texture(0, 1, *sky_light_2d_array, Vulkan::StockSampler::NearestClamp);

	struct Constants
	{
		alignas(4) uint32_t num_iterations;
		alignas(4) float inv_resolution;
		alignas(4) float inv_resolution2;
	} constants = {};

	constants.num_iterations = sky_light->get_width() / (2 * 8);
	constants.inv_resolution = 2.0f / float(sky_light->get_width());
	constants.inv_resolution2 = constants.inv_resolution * constants.inv_resolution;
	cmd.push_constants(&constants, 0, sizeof(constants));
	cmd.dispatch(6, 1, 1);
}

void VolumetricDiffuseLightManager::update_sky_cube(Vulkan::CommandBuffer &cmd)
{
	cmd.set_program("builtin://shaders/lights/volumetric_light_setup_sky.comp");
	cmd.set_storage_texture(0, 0, *sky_light_2d_array);

	struct Constants
	{
		alignas(16) vec3 sun_color;
		alignas(4) float camera_y;
		alignas(16) vec3 sun_direction;
		alignas(4) float inv_resolution;
	};

	auto *constants = cmd.allocate_typed_constant_data<Constants>(1, 0, 1);
	constants->sun_color = fallback_render_context->get_lighting_parameters()->directional.color;
	constants->camera_y = fallback_render_context->get_render_parameters().camera_position.y;
	constants->sun_direction = fallback_render_context->get_lighting_parameters()->directional.direction;
	constants->inv_resolution = 1.0f / float(sky_light->get_width());

	cmd.dispatch(sky_light->get_width() / 8, sky_light->get_height() / 8, 6);
}

const Vulkan::BufferView &VolumetricDiffuseLightManager::get_fallback_volume_view() const
{
	return *fallback_volume_view;
}

void VolumetricDiffuseLightManager::add_render_passes(RenderGraph &graph)
{
	auto &light_pass = graph.add_pass("probe-light", RENDER_GRAPH_QUEUE_COMPUTE_BIT);
	light_pass.add_proxy_output("probe-light-proxy", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
	light_pass.set_build_render_pass([this](Vulkan::CommandBuffer &cmd) {
		// Clear atomic counters to 0.
		cmd.set_program("builtin://shaders/lights/volumetric_light_clear_atomic.comp");
		for (auto &light_tuple : *volumetric_diffuse)
		{
			auto *light = get_component<VolumetricDiffuseLightComponent>(light_tuple);
			cmd.set_storage_buffer(0, 0, *light->light.get_atomic_buffer());
			cmd.dispatch(1, 1, 1);
		}

		// In parallel, light the sky cube.
		update_sky_cube(cmd);

		cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
		            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT);

		// In parallel with culling, update the fallback volume.
		update_fallback_volume(cmd);

		// Cull
		cmd.set_program("builtin://shaders/lights/volumetric_light_cull_texels.comp");
		for (auto &light_tuple : *volumetric_diffuse)
		{
			auto *light = get_component<VolumetricDiffuseLightComponent>(light_tuple);
			cull_probe_buffer(cmd, *light);
		}

		cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
		            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
		            VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
		            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT |
		            VK_ACCESS_INDIRECT_COMMAND_READ_BIT);

		// Relight probes.

		Renderer::bind_global_parameters(cmd, *fallback_render_context);
		Renderer::bind_lighting_parameters(cmd, *fallback_render_context);

		if (fallback_render_context->get_lighting_parameters() &&
		    fallback_render_context->get_lighting_parameters()->cluster)
		{
			auto *global_transforms = cmd.allocate_typed_constant_data<ClustererGlobalTransforms>(3, 2, 1);
			memcpy(global_transforms,
			       &fallback_render_context->get_lighting_parameters()->cluster->get_cluster_global_transforms_bindless(),
			       sizeof(*global_transforms));
		}

		struct GlobalTransform
		{
			vec4 probe_pos_jitter[NumProbeLayers];
		};

		auto *transforms = cmd.allocate_typed_constant_data<GlobalTransform>(3, 0, 1);
		memcpy(transforms->probe_pos_jitter, probe_pos_jitter, sizeof(probe_pos_jitter));

		for (auto &light_tuple : *volumetric_diffuse)
		{
			auto *light = get_component<VolumetricDiffuseLightComponent>(light_tuple);
			light_probe_buffer(cmd, *light);
		}

		cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
		            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT);

		for (auto &light_tuple : *volumetric_diffuse)
		{
			auto *light = get_component<VolumetricDiffuseLightComponent>(light_tuple);
			average_probe_buffer(cmd, *light);
		}
	});

	light_pass.add_texture_input("shadow-fallback");
}

void VolumetricDiffuseLightManager::set_base_render_context(const RenderContext *context)
{
	base_render_context = context;
}

void VolumetricDiffuseLightManager::set_fallback_render_context(const RenderContext *context)
{
	fallback_render_context = context;
}

void VolumetricDiffuseLightManager::setup_render_pass_dependencies(RenderGraph &, RenderPass &target,
                                                                   RenderPassCreator::DependencyFlags dep_flags)
{
	if ((dep_flags & RenderPassCreator::LIGHTING_BIT) != 0)
		target.add_proxy_input("probe-light-proxy", VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}

void VolumetricDiffuseLightManager::setup_render_pass_dependencies(RenderGraph &graph)
{
	auto *light_pass = graph.find_pass("probe-light");
	assert(light_pass);
	if (graph.find_pass("clustering-bindless"))
		light_pass->add_external_lock("bindless-shadowmaps", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

	if (graph.find_pass("shadow-fallback"))
		light_pass->add_texture_input("shadow-fallback");
}

void VolumetricDiffuseLightManager::setup_render_pass_resources(RenderGraph &)
{
}
}
