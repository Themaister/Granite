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
	EVENT_MANAGER_REGISTER_LATCH(LightClusterer, on_pipeline_created, on_pipeline_destroyed, DevicePipelineReadyEvent);
	for (unsigned i = 0; i < MaxLights; i++)
	{
		legacy.points.index_remap[i] = i;
		legacy.spots.index_remap[i] = i;
	}

	bindless.allocator.reserve_max_resources_per_pool(256, MaxLightsBindless +
	                                                       MaxLightsGlobal +
	                                                       MaxLightsVolume * 2 +
	                                                       MaxFogRegions + MaxDecalsBindless);
	bindless.allocator.set_bindless_resource_type(BindlessResourceType::Image);
}

void LightClusterer::on_pipeline_created(const Vulkan::DevicePipelineReadyEvent &e)
{
	auto &shader_manager = e.get_device().get_shader_manager();
	legacy.program = shader_manager.register_compute("builtin://shaders/lights/clustering.comp");
	legacy.inherit_variant = legacy.program->register_variant({{ "INHERIT", 1 }});
	legacy.cull_variant = legacy.program->register_variant({});
}

void LightClusterer::on_pipeline_destroyed(const Vulkan::DevicePipelineReadyEvent &)
{
	legacy.program = nullptr;
	legacy.inherit_variant = nullptr;
	legacy.cull_variant = nullptr;

	legacy.spots.atlas.reset();
	legacy.points.atlas.reset();
	scratch_vsm_rt.reset();
	scratch_vsm_down.reset();

	std::fill(std::begin(legacy.spots.cookie), std::end(legacy.spots.cookie), 0);
	std::fill(std::begin(legacy.points.cookie), std::end(legacy.points.cookie), 0);

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
		if (enable_bindless)
		{
			target_.add_storage_read_only_input("cluster-bitmask");
			target_.add_storage_read_only_input("cluster-range");
			target_.add_storage_read_only_input("cluster-transforms");
			target_.add_external_lock("bindless-shadowmaps", VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
		}
		else
		{
			target_.add_texture_input("light-cluster");
		}
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
	if (enable_bindless)
	{
		bindless.bitmask_buffer = &graph.get_physical_buffer_resource(graph.get_buffer_resource("cluster-bitmask"));
		bindless.range_buffer = &graph.get_physical_buffer_resource(graph.get_buffer_resource("cluster-range"));
		bindless.bitmask_buffer_decal = &graph.get_physical_buffer_resource(graph.get_buffer_resource("cluster-bitmask-decal"));
		bindless.range_buffer_decal = &graph.get_physical_buffer_resource(graph.get_buffer_resource("cluster-range-decal"));
		bindless.transforms_buffer = &graph.get_physical_buffer_resource(graph.get_buffer_resource("cluster-transforms"));
		bindless.transformed_spots = &graph.get_physical_buffer_resource(graph.get_buffer_resource("cluster-transformed-spot"));
		bindless.cull_data = &graph.get_physical_buffer_resource(graph.get_buffer_resource("cluster-cull-setup"));
	}
	else
	{
		legacy.target = &graph.get_physical_texture_resource(graph.get_texture_resource("light-cluster").get_physical_index());
		legacy.pre_cull_target = &graph.get_physical_texture_resource(graph.get_texture_resource("light-cluster-prepass").get_physical_index());
	}
}

unsigned LightClusterer::get_active_point_light_count() const
{
	return legacy.points.count;
}

unsigned LightClusterer::get_active_spot_light_count() const
{
	return legacy.spots.count;
}

const PositionalFragmentInfo *LightClusterer::get_active_point_lights() const
{
	return legacy.points.lights;
}

const mat4 *LightClusterer::get_active_spot_light_shadow_matrices() const
{
	return legacy.spots.shadow_transforms;
}

const PointTransform *LightClusterer::get_active_point_light_shadow_transform() const
{
	return legacy.points.shadow_transforms;
}

const PositionalFragmentInfo *LightClusterer::get_active_spot_lights() const
{
	return legacy.spots.lights;
}

void LightClusterer::set_enable_clustering(bool enable)
{
	enable_clustering = enable;
}

void LightClusterer::set_enable_bindless(bool enable)
{
	enable_bindless = enable;
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

VkDescriptorSet LightClusterer::get_cluster_bindless_set() const
{
	return bindless.desc_set;
}

bool LightClusterer::clusterer_is_bindless() const
{
	return enable_bindless;
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

const Vulkan::ImageView *LightClusterer::get_cluster_image() const
{
	return enable_clustering ? legacy.target : nullptr;
}

const Vulkan::ImageView *LightClusterer::get_spot_light_shadows() const
{
	return (enable_shadows && legacy.spots.atlas) ? &legacy.spots.atlas->get_view() : nullptr;
}

const Vulkan::ImageView *LightClusterer::get_point_light_shadows() const
{
	return (enable_shadows && legacy.points.atlas) ? &legacy.points.atlas->get_view() : nullptr;
}

const mat4 &LightClusterer::get_cluster_transform() const
{
	return legacy.cluster_transform;
}

template <typename T>
static uint32_t reassign_indices_legacy(T &type)
{
	uint32_t partial_mask = 0;

	for (unsigned i = 0; i < type.count; i++)
	{
		// Try to inherit shadow information from some other index.
		auto itr = std::find_if(std::begin(type.cookie), std::end(type.cookie), [=](unsigned cookie) {
			return cookie == type.handles[i]->get_cookie();
		});

		if (itr != std::end(type.cookie))
		{
			auto index = std::distance(std::begin(type.cookie), itr);
			if (i != unsigned(index))
			{
				// Reuse the shadow data from the atlas.
				std::swap(type.cookie[i], type.cookie[index]);
				std::swap(type.shadow_transforms[i], type.shadow_transforms[index]);
				std::swap(type.index_remap[i], type.index_remap[index]);
			}
		}

		// Try to find an atlas slot which has never been used.
		if (type.handles[i]->get_cookie() != type.cookie[i] && type.cookie[i] != 0)
		{
			auto cookie_itr = std::find(std::begin(type.cookie), std::end(type.cookie), 0);

			if (cookie_itr != std::end(type.cookie))
			{
				auto index = std::distance(std::begin(type.cookie), cookie_itr);
				if (i != unsigned(index))
				{
					// Reuse the shadow data from the atlas.
					std::swap(type.cookie[i], type.cookie[index]);
					std::swap(type.shadow_transforms[i], type.shadow_transforms[index]);
					std::swap(type.index_remap[i], type.index_remap[index]);
				}
			}
		}

		if (type.handles[i]->get_cookie() != type.cookie[i])
			partial_mask |= 1u << i;
		else
			type.handles[i]->set_shadow_info(&type.atlas->get_view(), type.shadow_transforms[i]);

		type.cookie[i] = type.handles[i]->get_cookie();
	}

	return partial_mask;
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
		rp.clear_depth_stencil.depth = 1.0f;
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
		rp.clear_depth_stencil.depth = 1.0f;
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

void LightClusterer::render_shadow_legacy(Vulkan::CommandBuffer &cmd, const RenderContext &depth_context, VisibilityList &visible,
                                          unsigned off_x, unsigned off_y, unsigned res_x, unsigned res_y,
                                          const Vulkan::ImageView &rt, unsigned layer, Renderer::RendererFlushFlags flags)
{
	visible.clear();
	scene->gather_visible_static_shadow_renderables(depth_context.get_visibility_frustum(), visible);

	auto &depth_renderer = get_shadow_renderer();
	depth_renderer.begin(internal_queue);
	internal_queue.push_depth_renderables(depth_context, visible.data(), visible.size());
	internal_queue.sort();

	render_shadow(cmd, depth_context, internal_queue,
	              off_x, off_y, res_x, res_y,
	              rt, layer, flags | Renderer::SKIP_SORTING_BIT);
}

void LightClusterer::render_atlas_point(const RenderContext &context_)
{
	bool vsm = shadow_type == ShadowType::VSM;
	uint32_t partial_mask = reassign_indices_legacy(legacy.points);

	if (!legacy.points.atlas || force_update_shadows)
		partial_mask = ~0u;

	if (partial_mask == 0 && legacy.points.atlas && !force_update_shadows)
		return;

	bool partial_update = partial_mask != ~0u;
	auto &device = context_.get_device();
	auto cmd = device.request_command_buffer();

	if (!legacy.points.atlas)
	{
		auto format = vsm ? VK_FORMAT_R32G32_SFLOAT : VK_FORMAT_D16_UNORM;
		ImageCreateInfo info = ImageCreateInfo::render_target(shadow_resolution, shadow_resolution, format);
		info.layers = 6 * MaxLights;
		info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
		info.initial_layout = vsm ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;

		if (vsm)
			info.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		else
			info.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

		legacy.points.atlas = device.create_image(info, nullptr);
	}
	else if (partial_update)
	{
		VkImageMemoryBarrier2 barriers[32];
		unsigned barrier_count = 0;

		Util::for_each_bit(partial_mask, [&](unsigned bit) {
			auto &b = barriers[barrier_count++];
			b = {};
			b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
			b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			b.image = legacy.points.atlas->get_image();
			b.srcAccessMask = 0;

			if (vsm)
			{
				b.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
				                  VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
				b.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				b.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
				b.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			}
			else
			{
				b.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
				                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
				b.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
				b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
				b.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
				b.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			}

			b.subresourceRange.baseArrayLayer = 6u * legacy.points.index_remap[bit];
			b.subresourceRange.layerCount = 6;
			b.subresourceRange.levelCount = 1;
		});

		cmd->image_barriers(barrier_count, barriers);
	}
	else if (vsm)
	{
		cmd->image_barrier(*legacy.points.atlas, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
		                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT);
	}
	else
	{
		cmd->image_barrier(*legacy.points.atlas, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
		                   VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
	}

	RenderContext depth_context;
	VisibilityList visible;

	for (unsigned i = 0; i < legacy.points.count; i++)
	{
		if ((partial_mask & (1u << i)) == 0)
			continue;

		LOGI("Rendering shadow for point light %u (%p)\n", i, static_cast<void *>(legacy.points.handles[i]));

		unsigned remapped = legacy.points.index_remap[i];

		for (unsigned face = 0; face < 6; face++)
		{
			mat4 view, proj;
			compute_cube_render_transform(legacy.points.lights[i].position, face, proj, view,
			                              0.005f / legacy.points.lights[i].inv_radius,
			                              1.0f / legacy.points.lights[i].inv_radius);
			depth_context.set_camera(proj, view);

			if (face == 0)
			{
				legacy.points.shadow_transforms[i].transform = vec4(proj[2].zw(), proj[3].zw());
				legacy.points.shadow_transforms[i].slice.x = float(remapped);
				legacy.points.handles[i]->set_shadow_info(&legacy.points.atlas->get_view(), legacy.points.shadow_transforms[i]);
			}

			render_shadow_legacy(*cmd, depth_context, visible,
			                     0, 0, shadow_resolution, shadow_resolution,
			                     legacy.points.atlas->get_view(),
			                     6 * remapped + face,
			                     Renderer::FRONT_FACE_CLOCKWISE_BIT | Renderer::DEPTH_BIAS_BIT);
		}
	}

	if (partial_update)
	{
		VkImageMemoryBarrier2 barriers[32];
		unsigned barrier_count = 0;

		Util::for_each_bit(partial_mask, [&](unsigned bit) {
			auto &b = barriers[barrier_count++];
			b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
			b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			b.image = legacy.points.atlas->get_image();

			if (vsm)
			{
				b.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				b.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			}
			else
			{
				b.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
				b.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
				b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
				b.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			}

			b.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
			b.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			b.subresourceRange.baseArrayLayer = 6u * legacy.points.index_remap[bit];
			b.subresourceRange.layerCount = 6;
			b.subresourceRange.levelCount = 1;
		});

		cmd->image_barriers(barrier_count, barriers);
	}
	else if (vsm)
	{
		cmd->image_barrier(*legacy.points.atlas, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
	}
	else
	{
		cmd->image_barrier(*legacy.points.atlas, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		                   VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
	}

	device.submit(cmd);
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

	Threaded::compose_parallel_push_renderables(composer, spot_data.depth_context[0],
	                                            spot_data.queues[0], spot_data.visibility[0], MaxTasks,
	                                            Threaded::PushType::Depth);

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

		Threaded::compose_parallel_push_renderables(face_composer, point_data.depth_context[face],
		                                            point_data.queues[face], point_data.visibility[face], MaxTasks,
		                                            Threaded::PushType::Depth);

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

void LightClusterer::render_atlas_spot(const RenderContext &context_)
{
	bool vsm = shadow_type == ShadowType::VSM;
	uint32_t partial_mask = reassign_indices_legacy(legacy.spots);

	if (!legacy.spots.atlas || force_update_shadows)
		partial_mask = ~0u;

	if (partial_mask == 0 && legacy.spots.atlas && !force_update_shadows)
		return;

	auto &device = context_.get_device();
	auto cmd = device.request_command_buffer();

	if (!legacy.spots.atlas)
	{
		auto format = vsm ? VK_FORMAT_R32G32_SFLOAT : VK_FORMAT_D16_UNORM;
		ImageCreateInfo info = ImageCreateInfo::render_target(shadow_resolution * 8, shadow_resolution * 4, format);
		info.initial_layout = vsm ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;

		if (vsm)
			info.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		else
			info.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		legacy.spots.atlas = device.create_image(info, nullptr);

		// Make sure we have a cleared atlas so we don't spuriously filter against NaN.
		if (vsm)
		{
			cmd->image_barrier(*legacy.spots.atlas, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			                   VK_PIPELINE_STAGE_NONE, 0,
			                   VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
			cmd->clear_image(*legacy.spots.atlas, {});
			cmd->image_barrier(*legacy.spots.atlas, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			                   VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
			                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
		}
	}
	else
	{
		// Preserve data if we're not overwriting the entire shadow atlas.
		auto access = vsm ?
		              (VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT) :
		              (VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
		auto stages = vsm ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT :
		              (VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT);

		VkImageLayout layout = vsm ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		cmd->image_barrier(*legacy.spots.atlas,
		                   partial_mask != ~0u ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED, layout,
		                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
		                   stages, access);
	}

	RenderContext depth_context;
	VisibilityList visible;

	for (unsigned i = 0; i < legacy.spots.count; i++)
	{
		if ((partial_mask & (1u << i)) == 0)
			continue;

		LOGI("Rendering shadow for spot light %u (%p)\n", i, static_cast<void *>(legacy.spots.handles[i]));

		float range = tan(legacy.spots.handles[i]->get_xy_range());
		mat4 view = mat4_cast(look_at_arbitrary_up(legacy.spots.lights[i].direction)) *
		            translate(-legacy.spots.lights[i].position);
		mat4 proj = projection(range * 2.0f, 1.0f,
		                       0.005f / legacy.spots.lights[i].inv_radius,
		                       1.0f / legacy.spots.lights[i].inv_radius);

		unsigned remapped = legacy.spots.index_remap[i];

		// Carve out the atlas region where the spot light shadows live.
		legacy.spots.shadow_transforms[i] =
				translate(vec3(float(remapped & 7) / 8.0f, float(remapped >> 3) / 4.0f, 0.0f)) *
				scale(vec3(1.0f / 8.0f, 1.0f / 4.0f, 1.0f)) *
				translate(vec3(0.5f, 0.5f, 0.0f)) *
				scale(vec3(0.5f, 0.5f, 1.0f)) *
				proj * view;

		legacy.spots.handles[i]->set_shadow_info(&legacy.spots.atlas->get_view(), legacy.spots.shadow_transforms[i]);

		depth_context.set_camera(proj, view);

		render_shadow_legacy(*cmd, depth_context, visible,
		                     shadow_resolution * (remapped & 7), shadow_resolution * (remapped >> 3),
		                     shadow_resolution, shadow_resolution,
		                     legacy.spots.atlas->get_view(), 0, Renderer::DEPTH_BIAS_BIT);
	}

	if (vsm)
	{
		cmd->image_barrier(*legacy.spots.atlas, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
	}
	else
	{
		cmd->image_barrier(*legacy.spots.atlas, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		                   VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
	}

	device.submit(cmd);
}

void LightClusterer::refresh_legacy(const RenderContext& context_)
{
	legacy.points.count = 0;
	legacy.spots.count = 0;

	for (auto &light : light_sort_caches[0])
	{
		auto &l = *light.light;
		auto *transform = light.transform;

		if (l.get_type() == PositionalLight::Type::Spot)
		{
			auto &spot = static_cast<SpotLight &>(l);
			spot.set_shadow_info(nullptr, {});
			if (legacy.spots.count < max_spot_lights)
			{
				legacy.spots.lights[legacy.spots.count] = spot.get_shader_info(transform->get_world_transform());
				legacy.spots.handles[legacy.spots.count] = &spot;
				legacy.spots.count++;
			}
		}
		else if (l.get_type() == PositionalLight::Type::Point)
		{
			auto &point = static_cast<PointLight &>(l);
			point.set_shadow_info(nullptr, {});
			if (legacy.points.count < max_point_lights)
			{
				legacy.points.lights[legacy.points.count] = point.get_shader_info(transform->get_world_transform());
				legacy.points.handles[legacy.points.count] = &point;
				legacy.points.count++;
			}
		}
	}

	// Figure out aabb bounds in view space.
	auto &inv_proj = context_.get_render_parameters().inv_projection;
	const auto project = [](const vec4 &v) -> vec3 {
		return v.xyz() / v.w;
	};

	vec3 ul = project(inv_proj * vec4(-1.0f, -1.0f, 1.0f, 1.0f));
	vec3 ll = project(inv_proj * vec4(-1.0f, +1.0f, 1.0f, 1.0f));
	vec3 ur = project(inv_proj * vec4(+1.0f, -1.0f, 1.0f, 1.0f));
	vec3 lr = project(inv_proj * vec4(+1.0f, +1.0f, 1.0f, 1.0f));

	vec3 min_view = min(min(ul, ll), min(ur, lr));
	vec3 max_view = max(max(ul, ll), max(ur, lr));
	// Make sure scaling the box does not move the near plane.
	max_view.z = 0.0f;

	mat4 ortho_box = ortho(AABB(min_view, max_view));

	if (legacy.points.count || legacy.spots.count)
		legacy.cluster_transform = scale(vec3(1 << (ClusterHierarchies - 1))) * ortho_box * context_.get_render_parameters().view;
	else
		legacy.cluster_transform = scale(vec3(0.0f, 0.0f, 0.0f));

	if (enable_shadows)
	{
		render_atlas_spot(context_);
		render_atlas_point(context_);
	}
	else
	{
		legacy.spots.atlas.reset();
		legacy.points.atlas.reset();
	}
}

static void set_spot_model_transform(ClustererBindlessTransforms &transforms, unsigned index,
                                     const SpotLight &spot, const mat4 &transform)
{
	transforms.model[index] = spot.build_model_matrix(transform);
}

static void set_spot_model_transform(ClustererGlobalTransforms &, unsigned, const SpotLight &, const mat4 &)
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

	float z_slice_size = context_.get_render_parameters().z_far / float(resolution_z);
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

	if (enable_bindless)
	{
		refresh_bindless(context_, composer);
	}
	else
	{
		// For legacy path, just do everything in one thread.
		auto &group = incoming_composer.begin_pipeline_stage();
		group.enqueue_task([this, &context_]() {
			refresh_legacy(context_);
		});
	}

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
		bindless.desc_set = VK_NULL_HANDLE;
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
	range = range * (float(resolution_z) / context->get_render_parameters().z_far);
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

static vec2 decal_z_range(const RenderContext &context, const mat4 &transform)
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
		          visible_decals[i].transform->get_world_transform());
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

void LightClusterer::build_cluster(Vulkan::CommandBuffer &cmd, Vulkan::ImageView &view, const Vulkan::ImageView *pre_culled)
{
	unsigned res_x = resolution_x;
	unsigned res_y = resolution_y;
	unsigned res_z = resolution_z;
	if (!pre_culled)
	{
		res_x /= ClusterPrepassDownsample;
		res_y /= ClusterPrepassDownsample;
		res_z /= ClusterPrepassDownsample;
	}

	cmd.set_program(pre_culled ? legacy.inherit_variant->get_program() : legacy.cull_variant->get_program());
	cmd.set_storage_texture(0, 0, view);
	if (pre_culled)
		cmd.set_texture(0, 1, *pre_culled, StockSampler::NearestWrap);

	auto *spot_buffer = cmd.allocate_typed_constant_data<PositionalFragmentInfo>(1, 0, MaxLights);
	auto *point_buffer = cmd.allocate_typed_constant_data<PositionalFragmentInfo>(1, 1, MaxLights);
	memcpy(spot_buffer, legacy.spots.lights, legacy.spots.count * sizeof(PositionalFragmentInfo));
	memcpy(point_buffer, legacy.points.lights, legacy.points.count * sizeof(PositionalFragmentInfo));

	auto *spot_lut_buffer = cmd.allocate_typed_constant_data<vec4>(1, 2, MaxLights);
	for (unsigned i = 0; i < legacy.spots.count; i++)
	{
		spot_lut_buffer[i] = vec4(cosf(legacy.spots.handles[i]->get_xy_range()),
		                          sinf(legacy.spots.handles[i]->get_xy_range()),
		                          1.0f / legacy.spots.lights[i].inv_radius,
		                          0.0f);
	}

	struct Push
	{
		mat4 inverse_cluster_transform;
		uvec4 size_z_log2;
		vec4 inv_texture_size;
		vec4 inv_size_radius;
		uint32_t spot_count;
		uint32_t point_count;
	};

	auto inverse_cluster_transform = inverse(legacy.cluster_transform);

	vec3 inv_res = vec3(1.0f / res_x, 1.0f / res_y, 1.0f / res_z);
	float radius = 0.5f * length(mat3(inverse_cluster_transform) * (vec3(2.0f, 2.0f, 0.5f) * inv_res));

	Push push = {
		inverse_cluster_transform,
		uvec4(res_x, res_y, res_z, Util::trailing_zeroes(res_z)),
		vec4(1.0f / res_x, 1.0f / res_y, 1.0f / ((ClusterHierarchies + 1) * res_z), 1.0f),
		vec4(inv_res, radius),
		legacy.spots.count, legacy.points.count,
	};
	cmd.push_constants(&push, 0, sizeof(push));
	cmd.dispatch((res_x + 3) / 4, (res_y + 3) / 4, (ClusterHierarchies + 1) * ((res_z + 3) / 4));
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

void LightClusterer::add_render_passes_legacy(RenderGraph &graph)
{
	AttachmentInfo att;
	att.levels = 1;
	att.layers = 1;
	att.format = VK_FORMAT_R32G32B32A32_UINT;
	att.samples = 1;
	att.size_class = SizeClass::Absolute;
	att.size_x = resolution_x;
	att.size_y = resolution_y;
	att.size_z = resolution_z * (ClusterHierarchies + 1);
	att.aux_usage = VK_IMAGE_USAGE_SAMPLED_BIT;
	att.flags |= ATTACHMENT_INFO_PERSISTENT_BIT;

	att.format = VK_FORMAT_R32G32_UINT;

	AttachmentInfo att_prepass = att;
	assert((resolution_x % ClusterPrepassDownsample) == 0);
	assert((resolution_y % ClusterPrepassDownsample) == 0);
	assert((resolution_z % ClusterPrepassDownsample) == 0);
	assert((resolution_z & (resolution_z - 1)) == 0);
	att_prepass.size_x /= ClusterPrepassDownsample;
	att_prepass.size_y /= ClusterPrepassDownsample;
	att_prepass.size_z /= ClusterPrepassDownsample;

	auto &pass = graph.add_pass("clustering", RENDER_GRAPH_QUEUE_COMPUTE_BIT);
	pass.add_storage_texture_output("light-cluster", att);
	pass.add_storage_texture_output("light-cluster-prepass", att_prepass);
	pass.set_build_render_pass([this](Vulkan::CommandBuffer &cmd) {
		build_cluster(cmd, *legacy.pre_cull_target, nullptr);
		cmd.image_barrier(legacy.pre_cull_target->get_image(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
		                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		                  VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
		                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		                  VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
		                  VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
		build_cluster(cmd, *legacy.target, legacy.pre_cull_target);
	});
}

void LightClusterer::add_render_passes(RenderGraph &graph)
{
	if (enable_clustering)
	{
		if (enable_bindless)
		{
			add_render_passes_bindless(graph);
			graph.add_external_lock_interface("bindless-shadowmaps", this);
		}
		else
			add_render_passes_legacy(graph);
	}
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
