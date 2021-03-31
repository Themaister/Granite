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

TaskGroupHandle VolumetricDiffuseLightManager::create_probe_gbuffer(TaskComposer &composer, TaskGroup &incoming,
                                                                    const RenderContext &context,
                                                                    VolumetricDiffuseLightComponent &light)
{
	auto &device = context.get_device();
	uvec3 res = light.light.get_resolution();

	VolumetricDiffuseLight::GBuffer allocated_gbuffer;

	auto info = Vulkan::ImageCreateInfo::render_target(ProbeResolution * res.x * 6, ProbeResolution * res.y * res.z, VK_FORMAT_R8G8B8A8_SRGB);
	info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	allocated_gbuffer.albedo = device.create_image(info);

	bool supports_32bpp =
			device.image_format_is_supported(VK_FORMAT_B10G11R11_UFLOAT_PACK32,
			                                 VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT);
	info.format = supports_32bpp ? VK_FORMAT_B10G11R11_UFLOAT_PACK32 : VK_FORMAT_R16G16B16A16_SFLOAT;
	allocated_gbuffer.emissive = device.create_image(info);

	info.format = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
	allocated_gbuffer.normal = device.create_image(info);

	info.format = VK_FORMAT_R8G8_UNORM;
	allocated_gbuffer.pbr = device.create_image(info);

	info.format = device.get_default_depth_stencil_format();
	info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	allocated_gbuffer.depth = device.create_image(info);

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

	for (unsigned z = 0; z < res.z; z++)
	{
		for (unsigned y = 0; y < res.y; y++)
		{
			for (unsigned x = 0; x < res.x; x++)
			{
				for (unsigned face = 0; face < 6; face++)
				{
					auto &context_setup = probe_composer.begin_pipeline_stage();
					context_setup.enqueue_task([x, y, z, face, res, shared_context, &light]() {
						vec3 tex = (vec3(x, y, z) + 0.5f) / vec3(res);
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
					task.enqueue_task([x, y, z, face, res, renderer, rp, &light, &device]() {
						if (face == 0)
						{
							// We're going to be consuming a fair bit of memory,
							// so make sure to pump frame contexts through.
							// This code is not assumed to be hot (should be pre-baked).
							device.next_frame_context();
						}

						auto cmd = device.request_command_buffer();

						if (x == 0 && y == 0 && z == 0 && face == 0)
							transition_gbuffer(*cmd, light.light.get_gbuffer(), TransitionMode::Discard);

						auto slice_rp = rp;
						slice_rp.render_area.offset.x = (6 * x + face) * ProbeResolution;
						slice_rp.render_area.offset.y = (z * res.y + y) * ProbeResolution;
						slice_rp.render_area.extent.width = ProbeResolution;
						slice_rp.render_area.extent.height = ProbeResolution;

						cmd->begin_render_pass(slice_rp);
						renderer->build_render_pass(*cmd);
						cmd->end_render_pass();
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
		if (light->light.get_volume_view())
			continue;

		if (!light->light.get_gbuffer().emissive)
		{
			auto task = create_probe_gbuffer(composer, group, context, *light);
			composer.get_thread_group().add_dependency(group, *task);
		}
	}
}
}