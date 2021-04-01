/* Copyright (c) 2017-2021 Hans-Kristian Arntzen
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
#include "scene_renderer.hpp"

namespace Granite
{
void VolumetricDiffuseLightManager::set_scene(Scene *scene_)
{
	scene = scene_;
	volumetric_diffuse = &scene->get_entity_pool().get_component_group<VolumetricDiffuseLightComponent>();
}

void VolumetricDiffuseLightManager::set_render_suite(const RendererSuite *suite_)
{
	suite = suite_;
}

static constexpr unsigned ProbeResolution = 32;

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

	VkPipelineStageFlags src_color, src_depth, dst_color, dst_depth;
	VkAccessFlags src_access_color, src_access_depth, dst_access_color, dst_access_depth;
	VkImageLayout old_color, new_color, old_depth, new_depth;

	if (mode == TransitionMode::Discard)
	{
		src_color = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		src_depth = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		dst_color = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dst_depth = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;

		src_access_color = 0;
		src_access_depth = 0;
		dst_access_color = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
		dst_access_depth = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

		old_color = VK_IMAGE_LAYOUT_UNDEFINED;
		old_depth = VK_IMAGE_LAYOUT_UNDEFINED;
		new_color = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		new_depth = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	}
	else
	{
		src_color = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		src_depth = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		src_access_color = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		src_access_depth = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

		dst_color = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		dst_depth = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		dst_access_color = VK_ACCESS_SHADER_READ_BIT;
		dst_access_depth = VK_ACCESS_SHADER_READ_BIT;

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

TaskGroupHandle VolumetricDiffuseLightManager::light_probe_buffer(TaskGroupHandle incoming,
                                                                  const RenderContext &context,
                                                                  VolumetricDiffuseLightComponent &light)
{
	auto &device = context.get_device();

	struct Push
	{
		uint32_t patch_resolution;
		uint32_t face_resolution;
		uint32_t num_iterations_per_patch;
		float inv_patch_resolution;
		float inv_patch_resolution2;
	};

	TaskComposer probe_composer(*incoming->get_thread_group());
	probe_composer.set_incoming_task(std::move(incoming));

	auto &context_light = probe_composer.begin_pipeline_stage();

	context_light.enqueue_task([&device, &light]() {
		auto cmd = device.request_command_buffer();
		cmd->begin_region("probe-light");
		uvec3 res = light.light.get_resolution();

		// TODO: replace with external render graph semaphores.
		cmd->barrier(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
		             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0);

		Push push = {};
		push.patch_resolution = ProbeResolution / 2;
		push.face_resolution = ProbeResolution;
		push.inv_patch_resolution = 1.0f / float(push.patch_resolution);
		push.inv_patch_resolution2 = push.inv_patch_resolution * push.inv_patch_resolution;
		push.num_iterations_per_patch = push.patch_resolution / 8;

		cmd->set_program("builtin://shaders/util/volumetric_hemisphere_integral.comp");
		cmd->push_constants(&push, 0, sizeof(push));
		cmd->set_texture(0, 0, light.light.get_gbuffer().albedo->get_view());
		cmd->set_storage_texture(0, 1, *light.light.get_volume_view());
		cmd->dispatch(res.x, res.y, res.z);

		cmd->barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
		             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		             VK_ACCESS_SHADER_READ_BIT);

		cmd->end_region();
		device.submit(cmd);
	});

	return probe_composer.get_outgoing_task();
}

TaskGroupHandle VolumetricDiffuseLightManager::create_probe_gbuffer(TaskComposer &composer, TaskGroup &incoming,
                                                                    const RenderContext &context,
                                                                    VolumetricDiffuseLightComponent &light)
{
	auto &device = context.get_device();

	VolumetricDiffuseLight::GBuffer allocated_gbuffer;

	uvec3 resolution = light.light.get_resolution();
	auto gbuffer_info = Vulkan::ImageCreateInfo::render_target(ProbeResolution * resolution.x * 6,
	                                                           ProbeResolution * resolution.y * resolution.z,
	                                                           VK_FORMAT_R8G8B8A8_SRGB);
	gbuffer_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	gbuffer_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	allocated_gbuffer.albedo = device.create_image(gbuffer_info);

	bool supports_32bpp =
			device.image_format_is_supported(VK_FORMAT_B10G11R11_UFLOAT_PACK32,
			                                 VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT);
	gbuffer_info.format = supports_32bpp ? VK_FORMAT_B10G11R11_UFLOAT_PACK32 : VK_FORMAT_R16G16B16A16_SFLOAT;
	allocated_gbuffer.emissive = device.create_image(gbuffer_info);

	gbuffer_info.format = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
	allocated_gbuffer.normal = device.create_image(gbuffer_info);

	gbuffer_info.format = VK_FORMAT_R8G8_UNORM;
	allocated_gbuffer.pbr = device.create_image(gbuffer_info);

	gbuffer_info.format = device.get_default_depth_stencil_format();
	gbuffer_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	allocated_gbuffer.depth = device.create_image(gbuffer_info);

	device.set_name(*allocated_gbuffer.emissive, "probe-emissive");
	device.set_name(*allocated_gbuffer.albedo, "probe-albedo");
	device.set_name(*allocated_gbuffer.normal, "probe-normal");
	device.set_name(*allocated_gbuffer.pbr, "probe-pbr");
	device.set_name(*allocated_gbuffer.depth, "probe-depth");

	light.light.set_probe_gbuffer(std::move(allocated_gbuffer));

	auto shared_context = std::make_shared<RenderContext>(context);

	auto renderer = Util::make_handle<RenderPassSceneRenderer>();
	RenderPassSceneRenderer::Setup setup = {};
	setup.context = shared_context.get();
	setup.flags = SCENE_RENDERER_DEFERRED_GBUFFER_BIT;
	setup.deferred_lights = nullptr;
	setup.suite = suite;
	setup.scene = scene;
	renderer->init(setup);
	renderer->set_extra_flush_flags(Renderer::FRONT_FACE_CLOCKWISE_BIT);

	TaskComposer probe_composer(*incoming.get_thread_group());
	probe_composer.set_incoming_task(composer.get_pipeline_stage_dependency());

	Vulkan::RenderPassInfo rp;
	memset(rp.clear_color, 0, sizeof(rp.clear_color));
	rp.clear_depth_stencil.depth = 1.0f;
	rp.clear_depth_stencil.stencil = 0;
	rp.clear_attachments = 0xf;
	rp.store_attachments = 0xf;
	rp.op_flags = Vulkan::RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT | Vulkan::RENDER_PASS_OP_STORE_DEPTH_STENCIL_BIT;
	rp.num_color_attachments = 4;
	auto &gbuffer = light.light.get_gbuffer();
	rp.color_attachments[0] = &gbuffer.emissive->get_view();
	rp.color_attachments[1] = &gbuffer.albedo->get_view();
	rp.color_attachments[2] = &gbuffer.normal->get_view();
	rp.color_attachments[3] = &gbuffer.pbr->get_view();
	rp.depth_stencil = &gbuffer.depth->get_view();

	for (unsigned z = 0; z < resolution.z; z++)
	{
		for (unsigned y = 0; y < resolution.y; y++)
		{
			for (unsigned x = 0; x < resolution.x; x++)
			{
				for (unsigned face = 0; face < 6; face++)
				{
					auto &context_setup = probe_composer.begin_pipeline_stage();
					context_setup.enqueue_task([x, y, z, face, resolution, shared_context, &light]() {
						vec3 tex = (vec3(x, y, z) + 0.5f) / vec3(resolution);
						vec3 center = vec3(
								dot(light.texture_to_world[0], vec4(tex, 1.0f)),
								dot(light.texture_to_world[1], vec4(tex, 1.0f)),
								dot(light.texture_to_world[2], vec4(tex, 1.0f)));

						mat4 proj, view;
						compute_cube_render_transform(center, face, proj, view, 0.1f, 500.0f);
						shared_context->set_camera(proj, view);
					});

					VkSubpassContents contents = VK_SUBPASS_CONTENTS_INLINE;
					renderer->enqueue_prepare_render_pass(probe_composer, rp, 0, contents);

					auto &task = probe_composer.begin_pipeline_stage();
					task.enqueue_task([x, y, z, face, renderer, rp, &light, &device]() {
						if (face == 0)
						{
							// We're going to be consuming a fair bit of memory,
							// so make sure to pump frame contexts through.
							// This code is not assumed to be hot (should be pre-baked).
							device.next_frame_context();
						}

						auto cmd = device.request_command_buffer();
						cmd->begin_region("render-probe-gbuffer");

						if (x == 0 && y == 0 && z == 0 && face == 0)
							transition_gbuffer(*cmd, light.light.get_gbuffer(), TransitionMode::Discard);

						auto slice_rp = rp;
						slice_rp.render_area.offset.x = (6 * x + face) * ProbeResolution;
						slice_rp.render_area.offset.y = (z * light.light.get_resolution().y + y) * ProbeResolution;
						slice_rp.render_area.extent.width = ProbeResolution;
						slice_rp.render_area.extent.height = ProbeResolution;

						cmd->begin_render_pass(slice_rp);
						renderer->build_render_pass(*cmd);
						cmd->end_render_pass();
						cmd->end_region();
						device.submit(cmd);
					});
				}
			}
		}
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
			image->set_layout(Vulkan::Layout::General);

			cmd->image_barrier(*image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
			                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
			                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
			cmd->clear_image(*image, {});
			cmd->image_barrier(*image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
			                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
			                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			                   VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
			light.light.set_volume(std::move(image));
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

		TaskGroupHandle task;
		if (!light->light.get_gbuffer().emissive)
			task = create_probe_gbuffer(composer, group, context, *light);

		task = light_probe_buffer(task ? task : composer.get_pipeline_stage_dependency(), context, *light);
		composer.get_thread_group().add_dependency(group, *task);
	}
}
}