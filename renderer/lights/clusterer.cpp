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

#define NOMINMAX
#include "clusterer.hpp"
#include "render_graph.hpp"
#include "scene.hpp"
#include "render_context.hpp"
#include "renderer.hpp"
#include "application_wsi_events.hpp"
#include "quirks.hpp"
#include "muglm/matrix_helper.hpp"
#include "thread_group.hpp"
#include "simd.hpp"
#include <string.h>

using namespace Vulkan;

namespace Granite
{
LightClusterer::LightClusterer()
{
	EVENT_MANAGER_REGISTER_LATCH(LightClusterer, on_device_created, on_device_destroyed, DeviceCreatedEvent);

	bindless.allocator.reserve_max_resources_per_pool(256, MaxLightsBindless +
	                                                       MaxLightsGlobal +
	                                                       MaxLightsVolume * 2 +
	                                                       MaxFogRegions + MaxDecalsBindless);
	bindless.allocator.set_bindless_resource_type(BindlessResourceType::Image);
}

void LightClusterer::on_device_created(const Vulkan::DeviceCreatedEvent &)
{
}

void LightClusterer::on_device_destroyed(const Vulkan::DeviceCreatedEvent &)
{
	scratch_vsm_rt.reset();
	scratch_vsm_down.reset();
	bindless.allocator.reset();
	acquire_semaphore.reset();
	release_semaphores.clear();
}

void LightClusterer::set_scene(Scene *scene_)
{
	scene = scene_;
	lights = &scene->get_entity_pool().get_component_group<PositionalLightComponent, RenderInfoComponent>();
}

void LightClusterer::set_resolution(unsigned x, unsigned y, unsigned z)
{
	resolution_x = x;
	resolution_y = y;
	resolution_z = z;
}

void LightClusterer::set_shadow_resolution(unsigned res)
{
	shadow_resolution = res;
}

void LightClusterer::setup_render_pass_dependencies(RenderGraph &, RenderPass &target_,
                                                    RenderPassCreator::DependencyFlags dep_flags)
{
	if ((dep_flags & RenderPassCreator::LIGHTING_BIT) != 0)
	{
		target_.add_storage_read_only_input("cluster-bitmask");
		target_.add_storage_read_only_input("cluster-range");
		target_.add_storage_read_only_input("cluster-transforms");
		target_.add_external_lock("bindless-shadowmaps", VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
	}
}

void LightClusterer::setup_render_pass_dependencies(RenderGraph &)
{
}

void LightClusterer::set_base_render_context(const RenderContext *context_)
{
	context = context_;
}

void LightClusterer::setup_render_pass_resources(RenderGraph &graph)
{
	bindless.bitmask_buffer = &graph.get_physical_buffer_resource(graph.get_buffer_resource("cluster-bitmask"));
	bindless.range_buffer = &graph.get_physical_buffer_resource(graph.get_buffer_resource("cluster-range"));
	bindless.bitmask_buffer_decal = &graph.get_physical_buffer_resource(graph.get_buffer_resource("cluster-bitmask-decal"));
	bindless.range_buffer_decal = &graph.get_physical_buffer_resource(graph.get_buffer_resource("cluster-range-decal"));
	bindless.transforms_buffer = &graph.get_physical_buffer_resource(graph.get_buffer_resource("cluster-transforms"));
	bindless.transformed_spots = &graph.get_physical_buffer_resource(graph.get_buffer_resource("cluster-transformed-spot"));
	bindless.cull_data = &graph.get_physical_buffer_resource(graph.get_buffer_resource("cluster-cull-setup"));
}

const ClustererParametersBindless &LightClusterer::get_cluster_parameters_bindless() const
{
	return bindless.parameters;
}

const Vulkan::Buffer *LightClusterer::get_cluster_transform_buffer() const
{
	return bindless.transforms_buffer;
}

const Vulkan::Buffer *LightClusterer::get_cluster_bitmask_buffer() const
{
	return bindless.bitmask_buffer;
}

const Vulkan::Buffer *LightClusterer::get_cluster_range_buffer() const
{
	return bindless.range_buffer;
}

const Vulkan::Buffer *LightClusterer::get_cluster_bitmask_decal_buffer() const
{
	return bindless.bitmask_buffer_decal;
}

const Vulkan::Buffer *LightClusterer::get_cluster_range_decal_buffer() const
{
	return bindless.range_buffer_decal;
}

bool LightClusterer::clusterer_has_volumetric_decals() const
{
	return enable_volumetric_decals;
}

void LightClusterer::set_enable_volumetric_decals(bool enable)
{
	enable_volumetric_decals = enable;
}

Vulkan::BindlessDescriptorSet LightClusterer::get_cluster_bindless_set() const
{
	return bindless.desc_set;
}

void LightClusterer::set_shadow_type(ShadowType shadow_type_)
{
	shadow_type = shadow_type_;
}

LightClusterer::ShadowType LightClusterer::get_shadow_type() const
{
	return shadow_type;
}

void LightClusterer::set_enable_shadows(bool enable)
{
	enable_shadows = enable;
}

void LightClusterer::set_force_update_shadows(bool enable)
{
	force_update_shadows = enable;
}

void LightClusterer::setup_scratch_buffers_vsm(Vulkan::Device &device)
{
	auto image_info = ImageCreateInfo::render_target(shadow_resolution, shadow_resolution, VK_FORMAT_R32G32_SFLOAT);
	image_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	image_info.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
	if (!scratch_vsm_rt)
		scratch_vsm_rt = device.create_image(image_info, nullptr);
	if (!scratch_vsm_down)
	{
		image_info.width >>= 1;
		image_info.height >>= 1;
		scratch_vsm_down = device.create_image(image_info, nullptr);
	}
}

const Renderer &LightClusterer::get_shadow_renderer() const
{
	bool vsm = shadow_type == ShadowType::VSM;
	auto &depth_renderer = renderer_suite->get_renderer(
			vsm ? RendererSuite::Type::ShadowDepthPositionalVSM : RendererSuite::Type::ShadowDepthPositionalPCF);
	return depth_renderer;
}

void LightClusterer::render_shadow(Vulkan::CommandBuffer &cmd, const RenderContext &depth_context, const RenderQueue &queue,
                                   unsigned off_x, unsigned off_y, unsigned res_x, unsigned res_y,
                                   const Vulkan::ImageView &rt, unsigned layer, Renderer::RendererFlushFlags flags) const
{
	bool vsm = shadow_type == ShadowType::VSM;
	auto &depth_renderer = get_shadow_renderer();

	if (vsm)
	{
		RenderPassInfo rp;
		rp.op_flags = RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT;
		rp.clear_attachments = 1 << 0;
		rp.store_attachments = 1 << 1;

		auto msaa_att = cmd.get_device().get_transient_attachment(shadow_resolution, shadow_resolution, VK_FORMAT_R32G32_SFLOAT, 0, 4);
		auto depth_att = cmd.get_device().get_transient_attachment(shadow_resolution, shadow_resolution, VK_FORMAT_D16_UNORM, 0, 4);

		rp.color_attachments[0] = &msaa_att->get_view();
		rp.color_attachments[1] = &scratch_vsm_rt->get_view();
		rp.num_color_attachments = 2;
		rp.depth_stencil = &depth_att->get_view();
		rp.clear_depth_stencil.depth = 0.0f;
		rp.clear_depth_stencil.stencil = 0;

		float z_far = depth_context.get_render_parameters().z_far;
		rp.clear_color[0].float32[0] = z_far;
		rp.clear_color[0].float32[1] = z_far * z_far;

		RenderPassInfo::Subpass subpass = {};
		subpass.num_color_attachments = 1;
		subpass.num_resolve_attachments = 1;
		subpass.depth_stencil_mode = RenderPassInfo::DepthStencil::ReadWrite;
		subpass.color_attachments[0] = 0;
		subpass.resolve_attachments[0] = 1;
		rp.num_subpasses = 1;
		rp.subpasses = &subpass;

		cmd.image_barrier(*scratch_vsm_rt, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
		                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

		cmd.image_barrier(*scratch_vsm_down, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
		                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

		cmd.begin_region("shadow-map-vsm");
		cmd.begin_render_pass(rp);
		depth_renderer.flush_subset(cmd, queue, depth_context, flags, nullptr, 0, 1);
		cmd.end_render_pass();

		cmd.image_barrier(*scratch_vsm_rt, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

		cmd.begin_region("shadow-map-vsm-blur-down");
		{
			RenderPassInfo rp_vert = {};
			rp_vert.num_color_attachments = 1;
			rp_vert.store_attachments = 1 << 0;
			rp_vert.color_attachments[0] = &scratch_vsm_down->get_view();
			cmd.begin_render_pass(rp_vert);
			cmd.set_texture(0, 0, scratch_vsm_rt->get_view(), StockSampler::LinearClamp);
			vec2 inv_size(1.0f / scratch_vsm_rt->get_create_info().width,
			              1.0f / scratch_vsm_rt->get_create_info().height);
			cmd.push_constants(&inv_size, 0, sizeof(inv_size));
			CommandBufferUtil::draw_fullscreen_quad(cmd, "builtin://shaders/quad.vert",
			                                        "builtin://shaders/post/vsm_down_blur.frag");
			cmd.end_render_pass();
		}
		cmd.end_region();

		cmd.image_barrier(*scratch_vsm_down, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

		cmd.begin_region("shadow-map-vsm-blur-up");
		{
			RenderPassInfo rp_horiz = {};
			rp_horiz.num_color_attachments = 1;
			rp_horiz.store_attachments = 1 << 0;
			rp_horiz.color_attachments[0] = &rt;
			rp_horiz.render_area.offset.x = off_x;
			rp_horiz.render_area.offset.y = off_y;
			rp_horiz.render_area.extent.width = res_x;
			rp_horiz.render_area.extent.height = res_y;
			rp_horiz.base_layer = layer;

			cmd.begin_render_pass(rp_horiz);
			cmd.set_viewport({ float(off_x), float(off_y), float(res_x), float(res_y), 0.0f, 1.0f });
			cmd.set_scissor({{ int(off_x), int(off_y) }, { res_x, res_y }});
			vec2 inv_size(1.0f / scratch_vsm_down->get_create_info().width,
			              1.0f / scratch_vsm_down->get_create_info().height);
			cmd.push_constants(&inv_size, 0, sizeof(inv_size));
			cmd.set_texture(0, 0, scratch_vsm_down->get_view(), StockSampler::LinearClamp);
			CommandBufferUtil::draw_fullscreen_quad(cmd, "builtin://shaders/quad.vert",
			                                        "builtin://shaders/post/vsm_up_blur.frag");
			cmd.end_render_pass();
		}
		cmd.end_region();
	}
	else
	{
		RenderPassInfo rp;
		rp.op_flags = RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT |
		              RENDER_PASS_OP_STORE_DEPTH_STENCIL_BIT;
		rp.num_color_attachments = 0;
		rp.depth_stencil = &rt;
		rp.clear_depth_stencil.depth = 0.0f;
		rp.render_area.offset.x = off_x;
		rp.render_area.offset.y = off_y;
		rp.render_area.extent.width = res_x;
		rp.render_area.extent.height = res_y;
		rp.base_layer = layer;

		cmd.begin_region("shadow-map-pcf");
		cmd.begin_render_pass(rp);
		cmd.set_viewport({ float(off_x), float(off_y), float(res_x), float(res_y), 0.0f, 1.0f });
		cmd.set_scissor({{ int(off_x), int(off_y) }, { res_x, res_y }});
		depth_renderer.flush_subset(cmd, queue, depth_context, flags, nullptr, 0, 1);
		cmd.end_render_pass();
		cmd.end_region();
	}
}

void LightClusterer::begin_bindless_barriers(Vulkan::CommandBuffer &cmd)
{
	bool vsm = shadow_type == ShadowType::VSM;

	auto stage = vsm ?
	             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT :
	             (VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT);

	for (auto &sem : release_semaphores)
		if (sem->get_semaphore() && !sem->is_pending_wait())
			cmd.get_device().add_wait_semaphore(CommandBuffer::Type::Generic, std::move(sem), stage, false);

	release_semaphores.clear();
	bindless.shadow_barriers.clear();
	bindless.shadow_barriers.reserve(bindless.parameters.num_lights + bindless.global_transforms.num_lights);

	bindless.shadow_images.clear();
	bindless.shadow_images.resize(bindless.parameters.num_lights + bindless.global_transforms.num_lights);

	const auto add_barrier = [&](VkImage image) {
		VkImageMemoryBarrier2 barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
		barrier.image = image;
		barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barrier.newLayout = vsm ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = vsm ? (VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT) :
		                        (VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
		barrier.subresourceRange = {
				VkImageAspectFlags(vsm ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_DEPTH_BIT),
				0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS
		};
		barrier.srcStageMask = stage;
		barrier.dstStageMask = stage;
		bindless.shadow_barriers.push_back(barrier);
	};

	unsigned count = bindless.parameters.num_lights + bindless.global_transforms.num_lights;

	for (unsigned i = 0; i < count; i++)
	{
		if (!bindless.shadow_task_handles[i])
		{
			// To handle duplicate entries in local and global lists.
			bindless.shadow_images[i] = nullptr;
			continue;
		}

		bool point = bindless_light_is_point(i);
		auto cookie = bindless.handles[i]->get_cookie();
		auto &image = *bindless.shadow_map_cache.allocate(cookie,
		                                                  shadow_resolution * shadow_resolution *
		                                                  (point ? 6 : 1) *
		                                                  (vsm ? 8 : 2));

		Util::Hash current_transform_hash;
		if (point)
		{
			current_transform_hash = static_cast<const ShadowTaskContextPoint &>(*bindless.shadow_task_handles[i]).get_combined_hash(
					bindless.light_transform_hashes[i]);
		}
		else
		{
			current_transform_hash = static_cast<const ShadowTaskContextSpot &>(*bindless.shadow_task_handles[i]).get_combined_hash(
					bindless.light_transform_hashes[i]);
		}

		if (image && (!force_update_shadows && current_transform_hash == bindless.handles[i]->get_shadow_transform_hash()))
			continue;

		if (!image)
		{
			auto format = vsm ? VK_FORMAT_R32G32_SFLOAT : VK_FORMAT_D16_UNORM;
			ImageCreateInfo info = ImageCreateInfo::render_target(shadow_resolution, shadow_resolution, format);
			info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
			if (point)
			{
				info.layers = 6;
				info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
			}
			info.usage = vsm ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
			info.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
			image = cmd.get_device().create_image(info, nullptr);
		}

		bindless.handles[i]->set_shadow_transform_hash(current_transform_hash);
		bindless.shadow_images[i] = image.get();
		add_barrier(image->get_image());
	}

	if (!bindless.shadow_barriers.empty())
		cmd.image_barriers(uint32_t(bindless.shadow_barriers.size()), bindless.shadow_barriers.data());
}

void LightClusterer::end_bindless_barriers(Vulkan::CommandBuffer &cmd)
{
	bool vsm = shadow_type == ShadowType::VSM;
	auto src_stage = vsm ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT : VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	// We use semaphores to handle sync, so don't do access masks.

	for (auto &barrier : bindless.shadow_barriers)
	{
		if (vsm)
			barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		else
			barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		barrier.oldLayout = barrier.newLayout;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.dstAccessMask = 0;
		barrier.srcStageMask = src_stage;
		barrier.dstStageMask = VK_PIPELINE_STAGE_NONE;
	}

	if (!bindless.shadow_barriers.empty())
		cmd.image_barriers(uint32_t(bindless.shadow_barriers.size()), bindless.shadow_barriers.data());
}

LightClusterer::ShadowTaskContextSpotHandle
LightClusterer::gather_bindless_spot_shadow_renderables(unsigned index, TaskComposer &composer, bool requires_rendering)
{
	ShadowTaskContextSpotHandle data;
	if (requires_rendering)
		data = Util::make_handle<ShadowTaskContextSpot>();

	auto &setup_group = composer.begin_pipeline_stage();
	setup_group.set_desc("clusterer-spot-setup");
	setup_group.enqueue_task([this, data, index]() mutable {
		const PositionalFragmentInfo *light;
		mat4 *shadow;
		if (index >= bindless.parameters.num_lights)
		{
			light = &bindless.global_transforms.lights[index - bindless.parameters.num_lights];
			shadow = &bindless.global_transforms.shadow[index - bindless.parameters.num_lights];
		}
		else
		{
			light = &bindless.transforms.lights[index];
			shadow = &bindless.transforms.shadow[index];
		}

		float range = tan(static_cast<const SpotLight *>(bindless.handles[index])->get_xy_range());
		mat4 view = mat4_cast(look_at_arbitrary_up(light->direction)) *
		            translate(-light->position);
		mat4 proj = projection(range * 2.0f, 1.0f, 0.005f / light->inv_radius, 1.0f / light->inv_radius);

		*shadow = translate(vec3(0.5f, 0.5f, 0.0f)) *
		          scale(vec3(0.5f, 0.5f, 1.0f)) *
		          proj * view;

		if (data)
		{
			data->depth_context[0].set_camera(proj, view);
			auto &depth_renderer = get_shadow_renderer();
			for (auto &queue : data->queues[0])
				depth_renderer.begin(queue);
		}
	});

	if (requires_rendering)
	{
		Threaded::scene_gather_static_shadow_renderables(*scene, composer,
		                                                 data->depth_context[0].get_visibility_frustum(),
		                                                 data->visibility[0], data->hashes[0], MaxTasks);
	}

	return data;
}

LightClusterer::ShadowTaskContextPointHandle
LightClusterer::gather_bindless_point_shadow_renderables(unsigned index, TaskComposer &composer, bool requires_rendering)
{
	ShadowTaskContextPointHandle data;
	if (requires_rendering)
		data = Util::make_handle<ShadowTaskContextPoint>();

	auto &setup_group = composer.begin_pipeline_stage();
	setup_group.set_desc("clusterer-point-setup");
	setup_group.enqueue_task([data, index, this]() mutable {
		const PositionalFragmentInfo *light;
		mat4 *shadow;
		if (index >= bindless.parameters.num_lights)
		{
			light = &bindless.global_transforms.lights[index - bindless.parameters.num_lights];
			shadow = &bindless.global_transforms.shadow[index - bindless.parameters.num_lights];
		}
		else
		{
			light = &bindless.transforms.lights[index];
			shadow = &bindless.transforms.shadow[index];
		}

		mat4 view, proj;
		compute_cube_render_transform(light->position, 0, proj, view,
		                              0.005f / light->inv_radius, 1.0f / light->inv_radius);
		(*shadow)[0] = vec4(proj[2].zw(), proj[3].zw());

		for (unsigned face = 0; face < 6; face++)
		{
			compute_cube_render_transform(light->position, face, proj, view,
			                              0.005f / light->inv_radius, 1.0f / light->inv_radius);

			if (data)
			{
				data->depth_context[face].set_camera(proj, view);
				auto &depth_renderer = get_shadow_renderer();
				for (auto &queue : data->queues[face])
					depth_renderer.begin(queue);
			}
		}
	});

	if (requires_rendering)
	{
		auto &per_face_stage = composer.begin_pipeline_stage();

		for (unsigned face = 0; face < 6; face++)
		{
			TaskComposer face_composer(composer.get_thread_group());
			face_composer.set_incoming_task(composer.get_pipeline_stage_dependency());

			Threaded::scene_gather_static_shadow_renderables(*scene, face_composer,
			                                                 data->depth_context[face].get_visibility_frustum(),
			                                                 data->visibility[face], data->hashes[face], MaxTasks);

			composer.get_thread_group().add_dependency(per_face_stage, *face_composer.get_outgoing_task());
		}
	}

	return data;
}

void LightClusterer::render_bindless_spot(Vulkan::Device &device, unsigned index, TaskComposer &composer)
{
	auto data = bindless.shadow_task_handles[index];
	auto &spot_data = static_cast<ShadowTaskContextSpot &>(*data);

	Threaded::compose_parallel_push_renderables(composer, &spot_data.depth_context[0],
	                                            spot_data.queues[0], spot_data.visibility[0], MaxTasks,
	                                            Threaded::PushType::Depth, false);

	{
		auto &group = composer.begin_pipeline_stage();
		group.set_desc("render-shadow-map-spot");
		group.enqueue_task([&device, data, index, this]() {
			auto &spot = static_cast<const ShadowTaskContextSpot &>(*data);
			LOGI("Rendering shadow for spot light %u (%p)\n", index,
			     static_cast<const void *>(bindless.handles[index]));
			auto cmd = device.request_command_buffer();
			render_shadow(*cmd, spot.depth_context[0], spot.queues[0][0],
			              0, 0,
			              shadow_resolution, shadow_resolution,
			              bindless.shadow_images[index]->get_view(), 0,
			              Renderer::DEPTH_BIAS_BIT | Renderer::SKIP_SORTING_BIT);
			device.submit(cmd);
		});
	}
}

void LightClusterer::render_bindless_point(Vulkan::Device &device, unsigned index, TaskComposer &composer)
{
	auto data = bindless.shadow_task_handles[index];
	auto &point_data = static_cast<ShadowTaskContextPoint &>(*data);

	auto &per_face_stage = composer.begin_pipeline_stage();

	for (unsigned face = 0; face < 6; face++)
	{
		TaskComposer face_composer(composer.get_thread_group());
		face_composer.set_incoming_task(composer.get_pipeline_stage_dependency());

		Threaded::compose_parallel_push_renderables(face_composer, &point_data.depth_context[face],
		                                            point_data.queues[face], point_data.visibility[face], MaxTasks,
		                                            Threaded::PushType::Depth, false);

		auto &group = face_composer.begin_pipeline_stage();
		group.set_desc("render-shadow-map-point-face");
		group.enqueue_task([&device, data, index, face, this]() {
			auto &point = static_cast<const ShadowTaskContextPoint &>(*data);
			LOGI("Rendering shadow for point light %u (%p)\n", index, static_cast<const void *>(bindless.handles[index]));
			auto cmd = device.request_command_buffer();
			render_shadow(*cmd, point.depth_context[face], point.queues[face][0],
			              0, 0, shadow_resolution, shadow_resolution,
			              bindless.shadow_images[index]->get_view(),
			              face,
			              Renderer::FRONT_FACE_CLOCKWISE_BIT | Renderer::DEPTH_BIAS_BIT | Renderer::SKIP_SORTING_BIT);
			device.submit(cmd);
		});

		composer.get_thread_group().add_dependency(per_face_stage, *face_composer.get_outgoing_task());
	}
}

static void set_spot_model_transform(ClustererBindlessTransforms &transforms, unsigned index,
                                     const SpotLight &spot, const mat_affine &transform)
{
	transforms.model[index] = spot.build_model_matrix(transform);
}

static void set_spot_model_transform(ClustererGlobalTransforms &, unsigned, const SpotLight &, const mat_affine &)
{
}

static void set_point_model_transform(ClustererBindlessTransforms &transforms, unsigned index)
{
	transforms.model[index][0] = vec4(transforms.lights[index].position, 1.0f / transforms.lights[index].inv_radius);
}

static void set_point_model_transform(ClustererGlobalTransforms &, unsigned)
{
}

template <typename Transforms>
unsigned LightClusterer::scan_visible_positional_lights(const PositionalLightList &positional_lights,
                                                        Transforms &transforms,
                                                        unsigned max_lights, unsigned handle_offset)
{
	unsigned index = 0;

	for (auto &light : positional_lights)
	{
		auto &l = *light.light;
		auto *transform = light.transform;

		if (l.get_type() == PositionalLight::Type::Spot)
		{
			auto &spot = static_cast<SpotLight &>(l);
			spot.set_shadow_info(nullptr, {});
			if (index < max_lights)
			{
				transforms.lights[index] = spot.get_shader_info(transform->get_world_transform());
				set_spot_model_transform(transforms, index, spot, transform->get_world_transform());
				bindless.handles[index + handle_offset] = &l;
				bindless.light_transform_hashes.push_back(light.transform_hash);
				index++;
			}
		}
		else if (l.get_type() == PositionalLight::Type::Point)
		{
			auto &point = static_cast<PointLight &>(l);
			point.set_shadow_info(nullptr, {});
			if (index < max_lights)
			{
				transforms.lights[index] = point.get_shader_info(transform->get_world_transform());
				set_point_model_transform(transforms, index);
				transforms.type_mask[index >> 5] |= 1u << (index & 31u);
				bindless.handles[index + handle_offset] = &l;
				bindless.light_transform_hashes.push_back(light.transform_hash);
				index++;
			}
		}
	}

	return index;
}

float LightClusterer::get_z_slice_extent(const RenderContext &ctx) const
{
	return muglm::min(0.5f, ctx.get_render_parameters().z_far / float(resolution_z));
}

void LightClusterer::refresh_bindless_prepare(const RenderContext &context_)
{
	bindless.parameters.num_lights = 0;
	bindless.global_transforms.num_lights = 0;

	memset(bindless.transforms.type_mask, 0, sizeof(bindless.transforms.type_mask));
	memset(bindless.global_transforms.type_mask, 0, sizeof(bindless.global_transforms.type_mask));

	bindless.light_transform_hashes.clear();
	bindless.light_transform_hashes.reserve(light_sort_caches[0].size() + existing_global_lights.size());

	unsigned local_count = scan_visible_positional_lights(light_sort_caches[0],
	                                                      bindless.transforms,
	                                                      MaxLightsBindless, 0);
	unsigned global_count = scan_visible_positional_lights(existing_global_lights,
	                                                       bindless.global_transforms,
	                                                       MaxLightsGlobal, local_count);

	bindless.parameters.num_lights = local_count;
	bindless.parameters.num_lights_32 = (bindless.parameters.num_lights + 31) / 32;
	bindless.parameters.decals_texture_offset = 0;

	bindless.global_transforms.num_lights = global_count;
	bindless.global_transforms.descriptor_offset = local_count;

	float z_slice_size = get_z_slice_extent(context_);
	bindless.parameters.clip_scale =
			vec4(context_.get_render_parameters().projection[0][0],
			     -context_.get_render_parameters().projection[1][1],
			     context_.get_render_parameters().inv_projection[0][0],
			     -context_.get_render_parameters().inv_projection[1][1]);

	bindless.parameters.transform = translate(vec3(0.5f, 0.5f, 0.0f)) *
	                                scale(vec3(0.5f, 0.5f, 1.0f)) *
	                                context_.get_render_parameters().view_projection;
	bindless.parameters.camera_front = context_.get_render_parameters().camera_front;
	bindless.parameters.camera_base = context_.get_render_parameters().camera_position;
	bindless.parameters.xy_scale = vec2(resolution_x, resolution_y);
	bindless.parameters.resolution_xy = ivec2(resolution_x, resolution_y);
	bindless.parameters.inv_resolution_xy = vec2(1.0f / resolution_x, 1.0f / resolution_y);
	bindless.parameters.z_scale = 1.0f / z_slice_size;
	bindless.parameters.z_max_index = resolution_z - 1;

	bindless.shadow_map_cache.set_total_cost(64 * 1024 * 1024);
	uint64_t total_pruned = bindless.shadow_map_cache.prune();
	if (total_pruned)
		LOGI("Clusterer pruned a total of %llu bytes.\n", static_cast<unsigned long long>(total_pruned));

	bindless.volumetric.num_volumes = std::min<uint32_t>(visible_diffuse_lights.size(), CLUSTERER_MAX_VOLUMES);

	if (auto *light = context_.get_lighting_parameters())
	{
		bindless.volumetric.sun_color = light->directional.color;
		bindless.volumetric.sun_direction = light->directional.direction;
	}
	else
	{
		bindless.volumetric.sun_color = vec3(1.0f);
		bindless.volumetric.sun_direction = vec3(0.0, 1.0f, 0.0f);
	}

	for (uint32_t i = 0; i < bindless.volumetric.num_volumes; i++)
	{
		auto &light  = visible_diffuse_lights[i];
		auto &volume = bindless.volumetric.volumes[i];

		for (unsigned j = 0; j < 3; j++)
			volume.world_to_texture[j] = light.light->world_to_texture[j];
		volume.world_lo = light.light->world_lo;
		volume.world_hi = light.light->world_hi;

		float half_inv_width = 0.5f / float(light.light->light.get_resolution().x);
		volume.lo_tex_coord_x = half_inv_width;
		volume.hi_tex_coord_x = 1.0f - half_inv_width;

		// FIXME: Hardcoded for now.
		volume.guard_band_factor = 1.0f / VolumetricDiffuseLight::get_guard_band_factor();
		volume.guard_band_sharpen = 200.0f;
	}

	bindless.fog_regions.num_regions = std::min<uint32_t>(visible_fog_regions.size(), CLUSTERER_MAX_FOG_REGIONS);
	for (uint32_t i = 0; i < bindless.fog_regions.num_regions; i++)
	{
		auto &fog = visible_fog_regions[i];
		auto &region = bindless.fog_regions.regions[i];

		for (unsigned j = 0; j < 3; j++)
			region.world_to_texture[j] = fog.region->world_to_texture[j];
		region.world_lo = fog.region->world_lo;
		region.world_hi = fog.region->world_hi;
	}

	bindless.parameters.num_decals = std::min<uint32_t>(visible_decals.size(), CLUSTERER_MAX_DECALS_BINDLESS);
	bindless.parameters.num_decals_32 = (bindless.parameters.num_decals + 31) / 32;
	if (enable_volumetric_decals)
	{
		for (uint32_t i = 0; i < bindless.parameters.num_decals; i++)
		{
			auto &decal = visible_decals[i];
			auto &region = bindless.transforms.decals[i];
			for (unsigned j = 0; j < 3; j++)
				region.world_to_texture[j] = decal.decal->world_to_texture[j];
		}
	}
}

const ClustererParametersVolumetric &LightClusterer::get_cluster_volumetric_diffuse_data() const
{
	return bindless.volumetric;
}

const ClustererParametersFogRegions &LightClusterer::get_cluster_volumetric_fog_data() const
{
	return bindless.fog_regions;
}

const ClustererGlobalTransforms &LightClusterer::get_cluster_global_transforms_bindless() const
{
	return bindless.global_transforms;
}

void LightClusterer::set_enable_volumetric_diffuse(bool enable)
{
	enable_volumetric_diffuse = enable;
}

void LightClusterer::set_enable_volumetric_fog(bool enable)
{
	enable_volumetric_fog = enable;
}

bool LightClusterer::clusterer_has_volumetric_diffuse() const
{
	return enable_volumetric_diffuse;
}

bool LightClusterer::clusterer_has_volumetric_fog() const
{
	return enable_volumetric_fog;
}

size_t LightClusterer::get_cluster_volumetric_diffuse_size() const
{
	if (!enable_volumetric_diffuse)
		return 0;

	return bindless.volumetric.num_volumes * sizeof(DiffuseVolumeParameters) +
	       offsetof(ClustererParametersVolumetric, volumes);
}

size_t LightClusterer::get_cluster_volumetric_fog_size() const
{
	if (!enable_volumetric_fog)
		return 0;

	return bindless.fog_regions.num_regions * sizeof(FogRegionParameters) +
	       offsetof(ClustererParametersFogRegions, regions);
}

void LightClusterer::refresh_bindless(const RenderContext &context_, TaskComposer &composer)
{
	auto &device = context_.get_device();
	auto &thread_group = composer.get_thread_group();

	// Single task, prepare the lights.
	{
		auto &group = composer.begin_pipeline_stage();
		group.set_desc("clusterer-bindless-prepare");
		group.enqueue_task([this, &context_]() {
			bindless.shadow_task_handles.clear();
			refresh_bindless_prepare(context_);
			if (enable_shadows)
				bindless.shadow_task_handles.reserve(bindless.parameters.num_lights + bindless.global_transforms.num_lights);
		});
	}

	if (enable_shadows)
	{
		auto &group = composer.begin_pipeline_stage();
		group.set_desc("clusterer-bindless-setup");
		group.enqueue_task([this, gather_indirect_task = composer.get_deferred_enqueue_handle(), &thread_group]() mutable {
			unsigned count = bindless.parameters.num_lights + bindless.global_transforms.num_lights;

			// Gather renderables and compute the visiblity hash.
			for (unsigned i = 0; i < count; i++)
			{
				// Check for duplicates. Could probably just use a hashmap as well I guess ...
				// We know there can only be duplicates across local / global sets, not internally.
				bool requires_rendering = true;
				if (i < bindless.parameters.num_lights)
				{
					auto *global_handles = bindless.handles + bindless.parameters.num_lights;
					auto itr = std::find_if(global_handles, global_handles + bindless.global_transforms.num_lights,
											[light = bindless.handles[i]](const PositionalLight *global_light) {
												return light == global_light;
											});
					requires_rendering = itr == global_handles + bindless.global_transforms.num_lights;
				}

				TaskComposer per_light_composer(thread_group);
				if (bindless_light_is_point(i))
				{
					bindless.shadow_task_handles.emplace_back(
							gather_bindless_point_shadow_renderables(i, per_light_composer, requires_rendering));
				}
				else
				{
					bindless.shadow_task_handles.emplace_back(
							gather_bindless_spot_shadow_renderables(i, per_light_composer, requires_rendering));
				}
				per_light_composer.add_outgoing_dependency(*gather_indirect_task);
			}
		});
	}

	// Submit barriers from UNDEFINED -> COLOR/DEPTH.
	if (enable_shadows)
	{
		auto &group = composer.begin_pipeline_stage();

		group.enqueue_task([this, &device, indirect_task = composer.get_deferred_enqueue_handle(), &thread_group]() mutable {
			auto cmd = device.request_command_buffer();
			cmd->begin_region("shadow-map-begin-barriers");
			begin_bindless_barriers(*cmd);
			cmd->end_region();
			device.submit(cmd);

			unsigned count = bindless.parameters.num_lights + bindless.global_transforms.num_lights;

			// Run all shadow map renderings in parallel in separate composers.
			for (unsigned i = 0; i < count; i++)
			{
				if (!bindless.shadow_images[i])
					continue;

				TaskComposer per_light_composer(thread_group);
				if (bindless_light_is_point(i))
					render_bindless_point(device, i, per_light_composer);
				else
					render_bindless_spot(device, i, per_light_composer);
				per_light_composer.add_outgoing_dependency(*indirect_task);
			}
		});
	}

	// Submit barriers from COLOR/DEPTH -> SHADER_READ_ONLY
	{
		auto &group = composer.begin_pipeline_stage();
		group.enqueue_task([this, &device]() {
			if (enable_shadows)
			{
				auto cmd = device.request_command_buffer();
				cmd->begin_region("shadow-map-end-barriers");
				end_bindless_barriers(*cmd);
				cmd->end_region();

				device.submit(cmd, nullptr, 1, &acquire_semaphore);
			}

			acquire_semaphore.reset();
			update_bindless_descriptors(device);
		});
	}
}

static void sort_decals(VolumetricDecalList &list, const vec3 &front)
{
	std::sort(list.begin(), list.end(), [&front](const VolumetricDecalInfo &a, const VolumetricDecalInfo &b) -> bool {
		float a_dist = dot(front, a.transform->get_aabb().get_center());
		float b_dist = dot(front, b.transform->get_aabb().get_center());
		return a_dist < b_dist;
	});
}

void LightClusterer::refresh(const RenderContext &context_, TaskComposer &incoming_composer)
{
	if (enable_shadows && shadow_type == ShadowType::VSM)
		setup_scratch_buffers_vsm(context_.get_device());

	for (auto &cache : light_sort_caches)
		cache.clear();

	TaskComposer composer(incoming_composer.get_thread_group());
	composer.set_incoming_task(incoming_composer.get_pipeline_stage_dependency());

	// Gather lights in parallel.
	Threaded::scene_gather_positional_light_renderables_sorted(*scene, composer, context_,
	                                                           light_sort_caches, MaxTasks);

	composer.get_group().enqueue_task([this, &context_]() {
		visible_diffuse_lights.clear();
		visible_fog_regions.clear();
		visible_decals.clear();
		existing_global_lights.clear();

		if (enable_volumetric_diffuse)
		{
			scene->gather_visible_volumetric_diffuse_lights(context_.get_visibility_frustum(),
			                                                visible_diffuse_lights);
			scene->gather_irradiance_affecting_positional_lights(existing_global_lights);
		}

		if (enable_volumetric_fog)
		{
			scene->gather_visible_volumetric_fog_regions(context_.get_visibility_frustum(),
			                                             visible_fog_regions);
		}

		if (enable_volumetric_decals)
		{
			scene->gather_visible_volumetric_decals(context_.get_visibility_frustum(), visible_decals);
			sort_decals(visible_decals, context_.get_render_parameters().camera_front);
		}
	});

	refresh_bindless(context_, composer);
	incoming_composer.get_thread_group().add_dependency(incoming_composer.get_group(), *composer.get_outgoing_task());
}

void LightClusterer::update_bindless_data(Vulkan::CommandBuffer &cmd)
{
	uint32_t count = bindless.parameters.num_lights;

	memcpy(cmd.update_buffer(*bindless.transforms_buffer, offsetof(ClustererBindlessTransforms, lights),
	                         count * sizeof(bindless.transforms.lights[0])),
	       bindless.transforms.lights,
	       count * sizeof(bindless.transforms.lights[0]));

	memcpy(cmd.update_buffer(*bindless.transforms_buffer, offsetof(ClustererBindlessTransforms, shadow),
	                         count * sizeof(bindless.transforms.shadow[0])),
	       bindless.transforms.shadow,
	       count * sizeof(bindless.transforms.shadow[0]));

	memcpy(cmd.update_buffer(*bindless.transforms_buffer, offsetof(ClustererBindlessTransforms, model),
	                         count * sizeof(bindless.transforms.model[0])),
	       bindless.transforms.model, count * sizeof(bindless.transforms.model[0]));

	memcpy(cmd.update_buffer(*bindless.transforms_buffer, offsetof(ClustererBindlessTransforms, type_mask),
	                         bindless.parameters.num_lights_32 * sizeof(bindless.transforms.type_mask[0])),
	       bindless.transforms.type_mask,
	       bindless.parameters.num_lights_32 * sizeof(bindless.transforms.type_mask[0]));

	if (enable_volumetric_decals)
	{
		memcpy(cmd.update_buffer(*bindless.transforms_buffer, offsetof(ClustererBindlessTransforms, decals),
		                         bindless.parameters.num_decals * sizeof(bindless.transforms.decals[0])),
		       bindless.transforms.decals, bindless.parameters.num_decals * sizeof(bindless.transforms.decals[0]));
	}
}

void LightClusterer::update_bindless_descriptors(Vulkan::Device &device)
{
	if (!enable_shadows)
	{
		bindless.desc_set = {};
		return;
	}

	unsigned local_count = bindless.parameters.num_lights;
	unsigned global_count = bindless.global_transforms.num_lights;
	unsigned shadow_count = local_count + global_count;

	bindless.allocator.begin();

	if (enable_shadows)
	{
		for (unsigned i = 0; i < shadow_count; i++)
		{
			auto *image = bindless.shadow_map_cache.find_and_mark_as_recent(bindless.handles[i]->get_cookie());
			assert(image);
			bindless.allocator.push((*image)->get_view());
		}
	}

	bindless.volumetric.bindless_index_offset = bindless.allocator.get_next_offset();

	for (unsigned i = 0; i < bindless.volumetric.num_volumes; i++)
		bindless.allocator.push(*visible_diffuse_lights[i].light->light.get_volume_view());
	for (unsigned i = 0; i < bindless.volumetric.num_volumes; i++)
		bindless.allocator.push(*visible_diffuse_lights[i].light->light.get_prev_volume_view());

	bindless.fog_regions.bindless_index_offset = bindless.allocator.get_next_offset();
	for (unsigned i = 0; i < bindless.fog_regions.num_regions; i++)
		bindless.allocator.push(*visible_fog_regions[i].region->region.get_volume_view());

	if (enable_volumetric_decals)
	{
		bindless.parameters.decals_texture_offset = bindless.allocator.get_next_offset();
		for (unsigned i = 0; i < bindless.parameters.num_decals; i++)
			bindless.allocator.push(*visible_decals[i].decal->decal.get_decal_view(device));
	}

	bindless.desc_set = bindless.allocator.commit(device);
}

bool LightClusterer::bindless_light_is_point(unsigned index) const
{
	if (index >= bindless.parameters.num_lights)
	{
		index -= bindless.parameters.num_lights;
		return (bindless.global_transforms.type_mask[index >> 5] & (1u << (index & 31))) != 0;
	}
	else
		return (bindless.transforms.type_mask[index >> 5] & (1u << (index & 31))) != 0;
}

uvec2 LightClusterer::compute_uint_range(vec2 range) const
{
	range = range / get_z_slice_extent(*context);
	if (range.y < 0.0f)
		return uvec2(0xffffffffu, 0u);
	range.x = muglm::max(range.x, 0.0f);

	uvec2 urange(range);
	urange.y = muglm::min(urange.y, resolution_z - 1);
	return urange;
}

void LightClusterer::update_bindless_range_buffer_gpu(Vulkan::CommandBuffer &cmd, const Vulkan::Buffer &range_buffer,
                                                      const std::vector<uvec2> &index_range)
{
	BufferCreateInfo info;
	info.domain = BufferDomain::LinkedDeviceHost;
	info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	info.size = index_range.size() * sizeof(uvec2);
	auto buffer = cmd.get_device().create_buffer(info, index_range.data());

	cmd.set_storage_buffer(0, 0, *buffer);
	cmd.set_storage_buffer(0, 1, range_buffer);

	assert((resolution_z & 63) == 0);

	struct Registers
	{
		uint32_t num_volumes;
		uint32_t num_volumes_128;
		uint32_t num_ranges;
	} push;
	push.num_volumes = uint32_t(index_range.size());
	push.num_volumes_128 = (push.num_volumes + 127) / 128;
	push.num_ranges = resolution_z;
	cmd.push_constants(&push, 0, sizeof(push));

	auto &features = cmd.get_device().get_device_features();
	constexpr VkSubgroupFeatureFlags required =
		VK_SUBGROUP_FEATURE_SHUFFLE_BIT |
		VK_SUBGROUP_FEATURE_BASIC_BIT;
	if ((features.vk11_props.subgroupSupportedOperations & required) == required &&
	    cmd.get_device().supports_subgroup_size_log2(true, 5, 7))
	{
		cmd.set_program("builtin://shaders/lights/clusterer_bindless_z_range_opt.comp");
		cmd.set_subgroup_size_log2(true, 5, 7);
		cmd.enable_subgroup_size_control(true);
		cmd.dispatch((resolution_z + 127) / 128, 1, 1);
		cmd.enable_subgroup_size_control(false);
	}
	else
	{
		cmd.set_program("builtin://shaders/lights/clusterer_bindless_z_range.comp");
		cmd.dispatch(resolution_z / 64, 1, 1);
	}
}

void LightClusterer::update_bindless_range_buffer_gpu(Vulkan::CommandBuffer &cmd)
{
	uint32_t count = bindless.parameters.num_lights;
	bindless.volume_index_range.resize(count);

	for (unsigned i = 0; i < count; i++)
	{
		vec2 range;
		if (bindless_light_is_point(i))
		{
			range = point_light_z_range(*context, bindless.transforms.lights[i].position,
			                            1.0f / bindless.transforms.lights[i].inv_radius);
		}
		else
			range = spot_light_z_range(*context, bindless.transforms.model[i]);

		bindless.volume_index_range[i] = compute_uint_range(range);
	}

	// Still need to run this algorithm to make sure we get a cleared out Z-range buffer.
	if (bindless.volume_index_range.empty())
		bindless.volume_index_range.push_back(uvec2(~0u, 0u));

	update_bindless_range_buffer_gpu(cmd, *bindless.range_buffer, bindless.volume_index_range);
}

static vec2 decal_z_range(const RenderContext &context, const mat_affine &transform)
{
	float lo = std::numeric_limits<float>::infinity();
	float hi = -lo;

	auto &pos = context.get_render_parameters().camera_position;
	auto &front = context.get_render_parameters().camera_front;
	auto &aabb = VolumetricDecal::get_static_aabb();

	// Not the most efficient approach ever ... :V
	for (unsigned i = 0; i < 8; i++)
	{
		vec4 world_space;
		vec4 texel_space = vec4(aabb.get_corner(i), 1.0f);
		SIMD::mul(world_space, transform, texel_space);
		float z = dot(world_space.xyz() - pos, front);
		lo = std::min(lo, z);
		hi = std::max(hi, z);
	}

	return vec2(lo, hi);
}

void LightClusterer::update_bindless_range_buffer_decal_gpu(Vulkan::CommandBuffer &cmd)
{
	if (!enable_volumetric_decals)
		return;
	uint32_t count = bindless.parameters.num_decals;
	bindless.volume_index_range.resize(count);

	for (unsigned i = 0; i < count; i++)
	{
		vec2 range = decal_z_range(*context, visible_decals[i].transform->get_world_transform());
		bindless.volume_index_range[i] = compute_uint_range(range);
	}

	// Still need to run this algorithm to make sure we get a cleared out Z-range buffer.
	if (bindless.volume_index_range.empty())
		bindless.volume_index_range.push_back(uvec2(~0u, 0u));

	update_bindless_range_buffer_gpu(cmd, *bindless.range_buffer_decal, bindless.volume_index_range);
}

void LightClusterer::update_bindless_mask_buffer_decal_gpu(Vulkan::CommandBuffer &cmd)
{
	uint32_t count = bindless.parameters.num_decals;
	if (count == 0 || !enable_volumetric_decals)
		return;

	cmd.set_storage_buffer(0, 0, *bindless.bitmask_buffer_decal);
	*cmd.allocate_typed_constant_data<ClustererParametersBindless>(1, 0, 1) = bindless.parameters;

	Vulkan::BufferCreateInfo info = {};
	info.size = count * sizeof(mat4);
	info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	info.domain = Vulkan::BufferDomain::LinkedDeviceHost;
	auto mvps = cmd.get_device().create_buffer(info);
	auto *mapped_mvps = static_cast<mat4 *>(cmd.get_device().map_host_buffer(*mvps, MEMORY_ACCESS_WRITE_BIT));
	for (uint32_t i = 0; i < count; i++)
	{
		SIMD::mul(mapped_mvps[i], context->get_render_parameters().view_projection,
		          visible_decals[i].transform->get_world_transform().to_mat4());
	}
	cmd.get_device().unmap_host_buffer(*mvps, MEMORY_ACCESS_WRITE_BIT);
	cmd.set_storage_buffer(2, 0, *mvps);

	assert((resolution_x & 7) == 0);
	assert((resolution_y & 7) == 0);

	bool use_subgroups = false;
	auto &features = cmd.get_device().get_device_features();
	unsigned tile_width = 1;
	unsigned tile_height = 1;

	constexpr VkSubgroupFeatureFlags required = VK_SUBGROUP_FEATURE_BALLOT_BIT | VK_SUBGROUP_FEATURE_BASIC_BIT |
	                                            VK_SUBGROUP_FEATURE_SHUFFLE_BIT;

	if ((features.vk11_props.subgroupSupportedOperations & required) == required &&
	    (features.vk11_props.subgroupSupportedStages & VK_SHADER_STAGE_COMPUTE_BIT) != 0)
	{
		// Our desired range is either 32 threads or 64 threads, 32 threads is preferred.

		if (cmd.get_device().supports_subgroup_size_log2(true, 5, 5))
		{
			// We can lock in 32 thread subgroups!
			cmd.enable_subgroup_size_control(true);
			cmd.set_subgroup_size_log2(true, 5, 5);
			cmd.set_specialization_constant_mask(1);
			cmd.set_specialization_constant(0, 32);
			tile_width = 8;
			tile_height = 4;
			use_subgroups = true;
		}
		else if (cmd.get_device().supports_subgroup_size_log2(true, 5, 6))
		{
			// We can use varying size, 32 or 64 sizes need to be handled.
			cmd.enable_subgroup_size_control(true);
			cmd.set_subgroup_size_log2(true, 5, 6);
			cmd.set_specialization_constant_mask(1);
			cmd.set_specialization_constant(0, 64);
			tile_width = 8;
			tile_height = 8;
			use_subgroups = true;
		}
	}

	cmd.set_program("builtin://shaders/lights/clusterer_bindless_binning_decal.comp", {{ "SUBGROUPS", use_subgroups ? 1 : 0 }});
	cmd.dispatch(bindless.parameters.num_decals_32,
	             resolution_x / tile_width,
	             resolution_y / tile_height);

	cmd.set_specialization_constant_mask(0);
	cmd.enable_subgroup_size_control(false);
}

void LightClusterer::update_bindless_mask_buffer_gpu(Vulkan::CommandBuffer &cmd)
{
	uint32_t local_count = bindless.parameters.num_lights;
	if (local_count == 0)
		return;

	cmd.set_storage_buffer(0, 0, *bindless.transforms_buffer);
	cmd.set_storage_buffer(0, 1, *bindless.transformed_spots);
	cmd.set_storage_buffer(0, 2, *bindless.cull_data);
	cmd.set_storage_buffer(0, 3, *bindless.bitmask_buffer);
	*cmd.allocate_typed_constant_data<ClustererParametersBindless>(1, 0, 1) = bindless.parameters;

	cmd.set_program("builtin://shaders/lights/clusterer_bindless_spot_transform.comp");
	{
		struct Registers
		{
			mat4 vp;
			vec3 camera_pos;
			uint num_lights;
			vec3 camera_front;
			float z_near;
			float z_far;
		} push;

		push.vp = context->get_render_parameters().view_projection;
		push.camera_pos = context->get_render_parameters().camera_position;
		push.num_lights = bindless.parameters.num_lights;
		push.camera_front = context->get_render_parameters().camera_front;
		push.z_near = context->get_render_parameters().z_near;
		push.z_far = context->get_render_parameters().z_far;
		cmd.push_constants(&push, 0, sizeof(push));
	}
	cmd.dispatch((bindless.parameters.num_lights + 63) / 64, 1, 1);

	cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
	            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);

	cmd.set_program("builtin://shaders/lights/clusterer_bindless_setup.comp");
	{
		struct Registers
		{
			mat4 view;
			uint num_lights;
		} push;
		push.view = context->get_render_parameters().view;
		push.num_lights = bindless.parameters.num_lights;
		cmd.push_constants(&push, 0, sizeof(push));
	}
	cmd.dispatch((bindless.parameters.num_lights + 63) / 64, 1, 1);

	cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
	            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);

	assert((resolution_x & 7) == 0);
	assert((resolution_y & 7) == 0);

	bool use_subgroups = false;
	auto &features = cmd.get_device().get_device_features();
	unsigned tile_width = 1;
	unsigned tile_height = 1;

	constexpr VkSubgroupFeatureFlags required = VK_SUBGROUP_FEATURE_BALLOT_BIT | VK_SUBGROUP_FEATURE_BASIC_BIT;

	if ((features.vk11_props.subgroupSupportedOperations & required) == required &&
	    (features.vk11_props.subgroupSupportedStages & VK_SHADER_STAGE_COMPUTE_BIT) != 0)
	{
		// Our desired range is either 32 threads or 64 threads, 32 threads is preferred.

		if (cmd.get_device().supports_subgroup_size_log2(true, 5, 5))
		{
			// We can lock in 32 thread subgroups!
			cmd.enable_subgroup_size_control(true);
			cmd.set_subgroup_size_log2(true, 5, 5);
			cmd.set_specialization_constant_mask(1);
			cmd.set_specialization_constant(0, 32);
			tile_width = 8;
			tile_height = 4;
			use_subgroups = true;
		}
		else if (cmd.get_device().supports_subgroup_size_log2(true, 5, 6))
		{
			// We can use varying size, 32 or 64 sizes need to be handled.
			cmd.enable_subgroup_size_control(true);
			cmd.set_subgroup_size_log2(true, 5, 6);
			cmd.set_specialization_constant_mask(1);
			cmd.set_specialization_constant(0, 64);
			tile_width = 8;
			tile_height = 8;
			use_subgroups = true;
		}
	}

	cmd.set_program("builtin://shaders/lights/clusterer_bindless_binning.comp", {{ "SUBGROUPS", use_subgroups ? 1 : 0 }});
	cmd.dispatch(bindless.parameters.num_lights_32,
	             resolution_x / tile_width,
	             resolution_y / tile_height);

	cmd.set_specialization_constant_mask(0);
	cmd.enable_subgroup_size_control(false);
}

void LightClusterer::build_cluster_bindless_gpu(Vulkan::CommandBuffer &cmd)
{
	update_bindless_data(cmd);
	cmd.barrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
	            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
	update_bindless_mask_buffer_gpu(cmd);
	update_bindless_mask_buffer_decal_gpu(cmd);
	update_bindless_range_buffer_gpu(cmd);
	update_bindless_range_buffer_decal_gpu(cmd);
}

void LightClusterer::add_render_passes_bindless(RenderGraph &graph)
{
	BufferInfo att;
	att.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	auto &pass = graph.add_pass("clustering-bindless", RENDER_GRAPH_QUEUE_COMPUTE_BIT);

	{
		att.size = resolution_x * resolution_y * (MaxLightsBindless / 8);
		pass.add_storage_output("cluster-bitmask", att);
		att.size = resolution_x * resolution_y * (MaxDecalsBindless / 8);
		pass.add_storage_output("cluster-bitmask-decal", att);
	}

	{
		att.size = resolution_z * sizeof(ivec2);
		pass.add_storage_output("cluster-range", att);
		pass.add_storage_output("cluster-range-decal", att);
	}

	{
		att.size = sizeof(ClustererBindlessTransforms);
		pass.add_transfer_output("cluster-transforms", att);
	}

	{
		att.size = sizeof(vec4) * 4 * 8 * MaxLightsBindless;
		pass.add_storage_output("cluster-cull-setup", att);
	}

	{
		att.size = sizeof(vec4) * 6 * MaxLightsBindless;
		pass.add_storage_output("cluster-transformed-spot", att);
	}

	pass.set_build_render_pass([&](Vulkan::CommandBuffer &cmd) {
		build_cluster_bindless_gpu(cmd);
	});
}

void LightClusterer::add_render_passes(RenderGraph &graph)
{
	add_render_passes_bindless(graph);
	graph.add_external_lock_interface("bindless-shadowmaps", this);
}

void LightClusterer::set_base_renderer(const RendererSuite *suite)
{
	renderer_suite = suite;
}

Vulkan::Semaphore LightClusterer::external_acquire()
{
	return acquire_semaphore;
}

void LightClusterer::external_release(Vulkan::Semaphore sem)
{
	release_semaphores.push_back(std::move(sem));
}
}
