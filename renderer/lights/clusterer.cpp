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

#include "clusterer.hpp"
#include "render_graph.hpp"
#include "scene.hpp"
#include "render_context.hpp"
#include "renderer.hpp"
#include "application_wsi_events.hpp"
#include "quirks.hpp"
#include "muglm/matrix_helper.hpp"
#include "thread_group.hpp"
#include "cpu_rasterizer.hpp"
#include "simd.hpp"
#include <string.h>

using namespace Vulkan;
using namespace std;

namespace Granite
{
LightClusterer::LightClusterer()
{
	EVENT_MANAGER_REGISTER_LATCH(LightClusterer, on_device_created, on_device_destroyed, DeviceCreatedEvent);
	for (unsigned i = 0; i < MaxLights; i++)
	{
		legacy.points.index_remap[i] = i;
		legacy.spots.index_remap[i] = i;
	}
}

void LightClusterer::on_device_created(const Vulkan::DeviceCreatedEvent &e)
{
	auto &shader_manager = e.get_device().get_shader_manager();
	legacy.program = shader_manager.register_compute("builtin://shaders/lights/clustering.comp");
	legacy.inherit_variant = legacy.program->register_variant({{ "INHERIT", 1 }});
	legacy.cull_variant = legacy.program->register_variant({});
}

void LightClusterer::on_device_destroyed(const Vulkan::DeviceCreatedEvent &)
{
	legacy.program = nullptr;
	legacy.inherit_variant = 0;
	legacy.cull_variant = 0;

	legacy.spots.atlas.reset();
	legacy.points.atlas.reset();
	scratch_vsm_rt.reset();
	scratch_vsm_down.reset();

	fill(begin(legacy.spots.cookie), end(legacy.spots.cookie), 0);
	fill(begin(legacy.points.cookie), end(legacy.points.cookie), 0);
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

void LightClusterer::setup_render_pass_dependencies(RenderGraph &, RenderPass &target_)
{
	// TODO: Other passes might want this?
	if (enable_bindless)
	{
		target_.add_storage_read_only_input("cluster-bitmask");
		target_.add_storage_read_only_input("cluster-range");
		target_.add_storage_read_only_input("cluster-transforms");
	}
	else
	{
		target_.add_texture_input("light-cluster");
	}
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
		bindless.transforms_buffer = &graph.get_physical_buffer_resource(graph.get_buffer_resource("cluster-transforms"));

		if (!ImplementationQuirks::get().clustering_force_cpu)
		{
			bindless.transformed_spots = &graph.get_physical_buffer_resource(graph.get_buffer_resource("cluster-transformed-spot"));
			bindless.cull_data = &graph.get_physical_buffer_resource(graph.get_buffer_resource("cluster-cull-setup"));
		}
	}
	else
	{
		legacy.target = &graph.get_physical_texture_resource(graph.get_texture_resource("light-cluster").get_physical_index());
		if (!ImplementationQuirks::get().clustering_list_iteration && !ImplementationQuirks::get().clustering_force_cpu)
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

VkDescriptorSet LightClusterer::get_cluster_shadow_map_bindless_set() const
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

const Vulkan::Buffer *LightClusterer::get_cluster_list_buffer() const
{
	return enable_clustering && legacy.cluster_list ? legacy.cluster_list.get() : nullptr;
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
		auto itr = find_if(begin(type.cookie), end(type.cookie), [=](unsigned cookie) {
			return cookie == type.handles[i]->get_cookie();
		});

		if (itr != end(type.cookie))
		{
			auto index = std::distance(begin(type.cookie), itr);
			if (i != unsigned(index))
			{
				// Reuse the shadow data from the atlas.
				swap(type.cookie[i], type.cookie[index]);
				swap(type.shadow_transforms[i], type.shadow_transforms[index]);
				swap(type.index_remap[i], type.index_remap[index]);
			}
		}

		// Try to find an atlas slot which has never been used.
		if (type.handles[i]->get_cookie() != type.cookie[i] && type.cookie[i] != 0)
		{
			auto cookie_itr = find(begin(type.cookie), end(type.cookie), 0);

			if (cookie_itr != end(type.cookie))
			{
				auto index = std::distance(begin(type.cookie), cookie_itr);
				if (i != unsigned(index))
				{
					// Reuse the shadow data from the atlas.
					swap(type.cookie[i], type.cookie[index]);
					swap(type.shadow_transforms[i], type.shadow_transforms[index]);
					swap(type.index_remap[i], type.index_remap[index]);
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
		rp.color_attachments[0] = &cmd.get_device().get_transient_attachment(shadow_resolution, shadow_resolution, VK_FORMAT_R32G32_SFLOAT, 0, 4);
		rp.color_attachments[1] = &scratch_vsm_rt->get_view();
		rp.num_color_attachments = 2;
		rp.depth_stencil = &cmd.get_device().get_transient_attachment(shadow_resolution, shadow_resolution, VK_FORMAT_D16_UNORM, 0, 4);
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
		                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

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
		                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

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
	internal_queue.push_depth_renderables(depth_context, visible);
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
		VkImageMemoryBarrier barriers[32];
		unsigned barrier_count = 0;

		Util::for_each_bit(partial_mask, [&](unsigned bit) {
			auto &b = barriers[barrier_count++];
			b = {};
			b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			b.image = legacy.points.atlas->get_image();
			b.srcAccessMask = 0;

			if (vsm)
			{
				b.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
				                  VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
				b.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			}
			else
			{
				b.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
				                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
				b.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
				b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			}

			b.subresourceRange.baseArrayLayer = 6u * legacy.points.index_remap[bit];
			b.subresourceRange.layerCount = 6;
			b.subresourceRange.levelCount = 1;
		});

		if (vsm)
		{
			cmd->barrier(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			             0, nullptr, 0, nullptr, barrier_count, barriers);
		}
		else
		{
			cmd->barrier(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			             VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			             0, nullptr, 0, nullptr, barrier_count, barriers);
		}
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
		VkImageMemoryBarrier barriers[32];
		unsigned barrier_count = 0;

		Util::for_each_bit(partial_mask, [&](unsigned bit) {
			auto &b = barriers[barrier_count++];
			b = {};
			b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			b.image = legacy.points.atlas->get_image();

			if (vsm)
			{
				b.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			}
			else
			{
				b.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
				b.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
				b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			}

			b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			b.subresourceRange.baseArrayLayer = 6u * legacy.points.index_remap[bit];
			b.subresourceRange.layerCount = 6;
			b.subresourceRange.levelCount = 1;
		});

		cmd->barrier(vsm ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT : VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		             0, nullptr, 0, nullptr, barrier_count, barriers);
	}
	else if (vsm)
	{
		cmd->image_barrier(*legacy.points.atlas, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
	}
	else
	{
		cmd->image_barrier(*legacy.points.atlas, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		                   VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
	}

	device.submit(cmd);
}

void LightClusterer::begin_bindless_barriers(Vulkan::CommandBuffer &cmd)
{
	bool vsm = shadow_type == ShadowType::VSM;
	bindless.src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	bindless.dst_stage = vsm ?
	                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT :
	                     (VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT);

	bindless.shadow_barriers.clear();
	bindless.shadow_barriers.reserve(bindless.count);

	bindless.shadow_images.clear();
	bindless.shadow_images.resize(bindless.count);

	const auto add_barrier = [&](VkImage image) {
		VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
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
		bindless.shadow_barriers.push_back(barrier);
	};

	for (unsigned i = 0; i < bindless.count; i++)
	{
		bool point = bindless_light_is_point(i);
		auto cookie = bindless.handles[i]->get_cookie();
		auto &image = *bindless.shadow_map_cache.allocate(cookie,
		                                                  shadow_resolution * shadow_resolution *
		                                                  (point ? 6 : 1) *
		                                                  (vsm ? 8 : 2));

		Util::Hash current_transform_hash;
		if (point)
			current_transform_hash = static_cast<const ShadowTaskContextPoint &>(*bindless.shadow_task_handles[i]).get_combined_hash();
		else
			current_transform_hash = static_cast<const ShadowTaskContextSpot &>(*bindless.shadow_task_handles[i]).get_combined_hash();

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
		else
			bindless.src_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

		bindless.handles[i]->set_shadow_transform_hash(current_transform_hash);
		bindless.shadow_images[i] = image.get();
		add_barrier(image->get_image());
	}

	if (!bindless.shadow_barriers.empty())
	{
		cmd.barrier(bindless.src_stage, bindless.dst_stage,
		            0, nullptr,
		            0, nullptr,
		            bindless.shadow_barriers.size(), bindless.shadow_barriers.data());
	}
}

void LightClusterer::end_bindless_barriers(Vulkan::CommandBuffer &cmd)
{
	bool vsm = shadow_type == ShadowType::VSM;
	for (auto &barrier : bindless.shadow_barriers)
	{
		if (vsm)
			barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		else
			barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		barrier.oldLayout = barrier.newLayout;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	}

	bindless.src_stage = vsm ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT : VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	bindless.dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

	if (!bindless.shadow_barriers.empty())
	{
		cmd.barrier(bindless.src_stage, bindless.dst_stage,
		            0, nullptr,
		            0, nullptr,
		            bindless.shadow_barriers.size(), bindless.shadow_barriers.data());
	}
}

LightClusterer::ShadowTaskContextSpotHandle
LightClusterer::gather_bindless_spot_shadow_renderables(unsigned index, TaskComposer &composer)
{
	auto data = Util::make_handle<ShadowTaskContextSpot>();

	auto &setup_group = composer.begin_pipeline_stage();
	setup_group.set_desc("clusterer-spot-setup");
	setup_group.enqueue_task([this, data, index]() mutable {
		float range = tan(static_cast<const SpotLight *>(bindless.handles[index])->get_xy_range());
		mat4 view = mat4_cast(look_at_arbitrary_up(bindless.transforms.lights[index].direction)) *
		            translate(-bindless.transforms.lights[index].position);
		mat4 proj = projection(range * 2.0f, 1.0f,
		                       0.005f / bindless.transforms.lights[index].inv_radius,
		                       1.0f / bindless.transforms.lights[index].inv_radius);

		bindless.transforms.shadow[index] =
				translate(vec3(0.5f, 0.5f, 0.0f)) *
				scale(vec3(0.5f, 0.5f, 1.0f)) *
				proj * view;

		data->depth_context[0].set_camera(proj, view);
		auto &depth_renderer = get_shadow_renderer();
		for (auto &queue : data->queues[0])
			depth_renderer.begin(queue);
	});

	Threaded::scene_gather_static_shadow_renderables(*scene, composer,
	                                                 data->depth_context[0].get_visibility_frustum(),
	                                                 data->visibility[0], data->hashes[0], MaxTasks);

	return data;
}

LightClusterer::ShadowTaskContextPointHandle
LightClusterer::gather_bindless_point_shadow_renderables(unsigned index, TaskComposer &composer)
{
	auto data = Util::make_handle<ShadowTaskContextPoint>();

	auto &setup_group = composer.begin_pipeline_stage();
	setup_group.set_desc("clusterer-point-setup");
	setup_group.enqueue_task([data, index, this]() mutable {
		mat4 view, proj;
		compute_cube_render_transform(bindless.transforms.lights[index].position, 0, proj, view,
		                              0.005f / bindless.transforms.lights[index].inv_radius,
		                              1.0f / bindless.transforms.lights[index].inv_radius);
		bindless.transforms.shadow[index][0] = vec4(proj[2].zw(), proj[3].zw());

		for (unsigned face = 0; face < 6; face++)
		{
			compute_cube_render_transform(bindless.transforms.lights[index].position, face, proj, view,
			                              0.005f / bindless.transforms.lights[index].inv_radius,
			                              1.0f / bindless.transforms.lights[index].inv_radius);
			data->depth_context[face].set_camera(proj, view);
			auto &depth_renderer = get_shadow_renderer();
			for (auto &queue : data->queues[face])
				depth_renderer.begin(queue);
		}
	});

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

	return data;
}

void LightClusterer::render_bindless_spot(Vulkan::Device &device, unsigned index, TaskComposer &composer)
{
	auto data = bindless.shadow_task_handles[index];
	auto &spot_data = static_cast<ShadowTaskContextSpot &>(*data);

	Threaded::compose_parallel_push_renderables(composer, spot_data.depth_context[0],
	                                            spot_data.queues[0], spot_data.visibility[0], MaxTasks);

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
		                                            point_data.queues[face], point_data.visibility[face], MaxTasks);

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
			                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
			                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
			cmd->clear_image(*legacy.spots.atlas, {});
			cmd->image_barrier(*legacy.spots.atlas, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
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
		                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
	}
	else
	{
		cmd->image_barrier(*legacy.spots.atlas, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		                   VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
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
				legacy.spots.lights[legacy.spots.count] = spot.get_shader_info(transform->transform->world_transform);
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
				legacy.points.lights[legacy.points.count] = point.get_shader_info(transform->transform->world_transform);
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

void LightClusterer::refresh_bindless_prepare(const RenderContext &context_)
{
	bindless.count = 0;
	unsigned index = 0;
	memset(bindless.transforms.type_mask, 0, sizeof(bindless.transforms.type_mask));

	bindless.light_transform_hashes.clear();
	bindless.light_transform_hashes.reserve(light_sort_caches[0].size());

	for (auto &light : light_sort_caches[0])
	{
		auto &l = *light.light;
		auto *transform = light.transform;

		if (l.get_type() == PositionalLight::Type::Spot)
		{
			auto &spot = static_cast<SpotLight &>(l);
			spot.set_shadow_info(nullptr, {});
			if (index < MaxLightsBindless)
			{
				bindless.transforms.lights[index] = spot.get_shader_info(transform->transform->world_transform);
				bindless.transforms.model[index] = spot.build_model_matrix(transform->transform->world_transform);
				bindless.handles[index] = &l;
				bindless.light_transform_hashes.push_back(light.transform_hash);
				index++;
			}
		}
		else if (l.get_type() == PositionalLight::Type::Point)
		{
			auto &point = static_cast<PointLight &>(l);
			point.set_shadow_info(nullptr, {});
			if (index < MaxLightsBindless)
			{
				bindless.transforms.lights[index] = point.get_shader_info(transform->transform->world_transform);
				bindless.transforms.model[index][0] = vec4(bindless.transforms.lights[index].position,
				                                           1.0f / bindless.transforms.lights[index].inv_radius);
				bindless.transforms.type_mask[index >> 5] |= 1u << (index & 31u);
				bindless.handles[index] = &l;
				bindless.light_transform_hashes.push_back(light.transform_hash);
				index++;
			}
		}
	}

	bindless.count = index;

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
	bindless.parameters.num_lights = bindless.count;
	bindless.parameters.num_lights_32 = (bindless.parameters.num_lights + 31) / 32;

	bindless.shadow_map_cache.set_total_cost(64 * 1024 * 1024);
	uint64_t total_pruned = bindless.shadow_map_cache.prune();
	if (total_pruned)
		LOGI("Clusterer pruned a total of %llu bytes.\n", static_cast<unsigned long long>(total_pruned));
}

void LightClusterer::refresh_bindless(const RenderContext &context_, TaskComposer &composer)
{
	auto &device = context_.get_device();
	auto &thread_group = composer.get_thread_group();

	bindless.shadow_task_handles.clear();
	bindless.shadow_task_handles.reserve(bindless.count);

	// Single task, prepare the lights.
	{
		auto &group = composer.begin_pipeline_stage();
		group.set_desc("clusterer-bindless-prepare");
		group.enqueue_task([this, &context_]() {
			refresh_bindless_prepare(context_);
		});
	}

	if (enable_shadows)
	{
		auto gather_indirect_task = thread_group.create_task();
		{
			auto &group = composer.begin_pipeline_stage();
			group.set_desc("clusterer-bindless-setup");
			group.enqueue_task([this, gather_indirect_task, &thread_group]() mutable {
				// Gather renderables and compute the visiblity hash.
				for (unsigned i = 0; i < bindless.count; i++)
				{
					TaskComposer per_light_composer(thread_group);
					if (bindless_light_is_point(i))
					{
						bindless.shadow_task_handles.emplace_back(
								gather_bindless_point_shadow_renderables(i, per_light_composer));
					}
					else
					{
						bindless.shadow_task_handles.emplace_back(
								gather_bindless_spot_shadow_renderables(i, per_light_composer));
					}
					thread_group.add_dependency(*gather_indirect_task, *per_light_composer.get_outgoing_task());
				}
			});
		}

		// Submit barriers from UNDEFINED -> COLOR/DEPTH.
		auto indirect_task = thread_group.create_task();
		{
			auto &group = composer.begin_pipeline_stage();
			thread_group.add_dependency(group, *gather_indirect_task);

			group.enqueue_task([this, &device, indirect_task, &thread_group]() mutable {
				auto cmd = device.request_command_buffer();
				cmd->begin_region("shadow-map-begin-barriers");
				begin_bindless_barriers(*cmd);
				cmd->end_region();
				device.submit(cmd);

				// Run all shadow map renderings in parallel in separate composers.
				for (unsigned i = 0; i < bindless.count; i++)
				{
					if (!bindless.shadow_images[i])
						continue;

					TaskComposer per_light_composer(thread_group);
					if (bindless_light_is_point(i))
						render_bindless_point(device, i, per_light_composer);
					else
						render_bindless_spot(device, i, per_light_composer);
					thread_group.add_dependency(*indirect_task, *per_light_composer.get_outgoing_task());
				}
			});
		}

		// Submit barriers from COLOR/DEPTH -> SHADER_READ_ONLY
		{
			auto &group = composer.begin_pipeline_stage();
			composer.get_thread_group().add_dependency(group, *indirect_task);
			group.enqueue_task([this, &device]() {
				auto cmd = device.request_command_buffer();
				cmd->begin_region("shadow-map-end-barriers");
				end_bindless_barriers(*cmd);
				cmd->end_region();
				device.submit(cmd);
				device.flush_frame();
				update_bindless_descriptors(device);
			});
		}
	}
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

uvec2 LightClusterer::cluster_lights_cpu(int x, int y, int z, const CPUGlobalAccelState &state,
                                         const CPULocalAccelState &local_state, float scale, uvec2 pre_mask)
{
	uint32_t spot_mask = 0;
	uint32_t point_mask = 0;

	vec3 view_space = vec3(2.0f, 2.0f, 0.5f) *
	                  (vec3(x, y, z) + vec3(0.5f * scale)) *
	                  state.inv_res +
	                  vec3(-1.0f, -1.0f, local_state.z_bias);
	view_space *= local_state.world_scale_factor;
	vec3 cube_center = (state.inverse_cluster_transform * vec4(view_space, 1.0f)).xyz();
	float cube_radius = local_state.cube_radius * scale;

	while (pre_mask.x)
	{
		unsigned i = trailing_zeroes(pre_mask.x);
		pre_mask.x &= ~(1u << i);

		// Sphere/cone culling from https://bartwronski.com/2017/04/13/cull-that-cone/.
		vec3 V = cube_center - state.spot_position[i];
		float V_sq = dot(V, V);
		float V1_len  = dot(V, state.spot_direction[i]);

		if (V1_len > cube_radius + state.spot_size[i])
			continue;
		if (-V1_len > cube_radius)
			continue;

		float V2_len = sqrtf(std::max(V_sq - V1_len * V1_len, 0.0f));
		float distance_closest_point = state.spot_angle_cos[i] * V2_len - state.spot_angle_sin[i] * V1_len;

		if (distance_closest_point > cube_radius)
			continue;

		spot_mask |= 1u << i;
	}

	while (pre_mask.y)
	{
		unsigned i = trailing_zeroes(pre_mask.y);
		pre_mask.y &= ~(1u << i);

		vec3 cube_center_dist = cube_center - state.point_position[i];
		float radial_dist_sqr = dot(cube_center_dist, cube_center_dist);

		float cutoff = state.point_size[i] + cube_radius;
		cutoff *= cutoff;
		bool radial_inside = radial_dist_sqr <= cutoff;
		if (radial_inside)
			point_mask |= 1u << i;
	}

	return uvec2(spot_mask, point_mask);
}

void LightClusterer::update_bindless_data(Vulkan::CommandBuffer &cmd)
{
	memcpy(cmd.update_buffer(*bindless.transforms_buffer, offsetof(ClustererBindlessTransforms, lights),
	                         bindless.count * sizeof(bindless.transforms.lights[0])),
	       bindless.transforms.lights,
	       bindless.count * sizeof(bindless.transforms.lights[0]));

	memcpy(cmd.update_buffer(*bindless.transforms_buffer, offsetof(ClustererBindlessTransforms, shadow),
	                         bindless.count * sizeof(bindless.transforms.shadow[0])),
	       bindless.transforms.shadow,
	       bindless.count * sizeof(bindless.transforms.shadow[0]));

	memcpy(cmd.update_buffer(*bindless.transforms_buffer, offsetof(ClustererBindlessTransforms, model),
	                         bindless.count * sizeof(bindless.transforms.model[0])),
	       bindless.transforms.model,
	       bindless.count * sizeof(bindless.transforms.model[0]));

	memcpy(cmd.update_buffer(*bindless.transforms_buffer, offsetof(ClustererBindlessTransforms, type_mask),
	                         bindless.parameters.num_lights_32 * sizeof(bindless.transforms.type_mask[0])),
	       bindless.transforms.type_mask,
	       bindless.parameters.num_lights_32 * sizeof(bindless.transforms.type_mask[0]));
}

void LightClusterer::update_bindless_descriptors(Vulkan::Device &device)
{
	if (!enable_shadows)
	{
		bindless.desc_set = VK_NULL_HANDLE;
		return;
	}

	if (!bindless.descriptor_pool)
		bindless.descriptor_pool = device.create_bindless_descriptor_pool(BindlessResourceType::ImageFP, 16, MaxLightsBindless);

	unsigned num_lights = std::max(1u, bindless.count);
	if (!bindless.descriptor_pool->allocate_descriptors(num_lights))
	{
		bindless.descriptor_pool = device.create_bindless_descriptor_pool(BindlessResourceType::ImageFP, 16, MaxLightsBindless);
		if (!bindless.descriptor_pool->allocate_descriptors(num_lights))
			LOGE("Failed to allocate descriptors on a fresh descriptor pool!\n");
	}

	bindless.desc_set = bindless.descriptor_pool->get_descriptor_set();

	if (!bindless.count)
		return;

	if (enable_shadows)
	{
		for (unsigned i = 0; i < bindless.count; i++)
		{
			auto *image = bindless.shadow_map_cache.find_and_mark_as_recent(bindless.handles[i]->get_cookie());
			assert(image);
			bindless.descriptor_pool->set_texture(i, (*image)->get_view());
		}
	}
}

bool LightClusterer::bindless_light_is_point(unsigned index) const
{
	return (bindless.transforms.type_mask[index >> 5] & (1u << (index & 31))) != 0;
}

void LightClusterer::update_bindless_range_buffer_gpu(Vulkan::CommandBuffer &cmd)
{
	bindless.light_index_range.resize(bindless.count);

	const auto compute_uint_range = [&](vec2 range) -> uvec2 {
		range = range * (float(resolution_z) / context->get_render_parameters().z_far);
		if (range.y < 0.0f)
			return uvec2(0xffffffffu, 0u);
		range.x = muglm::max(range.x, 0.0f);

		uvec2 urange(range);
		urange.y = muglm::min(urange.y, resolution_z - 1);
		return urange;
	};

	for (unsigned i = 0; i < bindless.count; i++)
	{
		vec2 range;
		if (bindless_light_is_point(i))
		{
			range = point_light_z_range(*context, bindless.transforms.lights[i].position,
			                            1.0f / bindless.transforms.lights[i].inv_radius);
		}
		else
			range = spot_light_z_range(*context, bindless.transforms.model[i]);

		bindless.light_index_range[i] = compute_uint_range(range);
	}

	// Still need to run this algorithm to make sure we get a cleared out Z-range buffer.
	if (bindless.light_index_range.empty())
		bindless.light_index_range.push_back(uvec2(~0u, 0u));

	BufferCreateInfo info;
	info.domain = BufferDomain::Host;
	info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	info.size = bindless.light_index_range.size() * sizeof(uvec2);
	auto buffer = cmd.get_device().create_buffer(info, bindless.light_index_range.data());

	cmd.set_program("builtin://shaders/lights/clusterer_bindless_z_range.comp");
	cmd.set_storage_buffer(0, 0, *buffer);
	cmd.set_storage_buffer(0, 1, *bindless.range_buffer);

	assert((resolution_z & 63) == 0);

	struct Registers
	{
		uint num_lights;
	} push;
	push.num_lights = bindless.parameters.num_lights;
	cmd.push_constants(&push, 0, sizeof(push));
	cmd.dispatch(resolution_z / 64, 1, 1);
}

void LightClusterer::update_bindless_range_buffer_cpu(Vulkan::CommandBuffer &cmd)
{
	bindless.light_index_range.resize(resolution_z);
	for (auto &range : bindless.light_index_range)
		range = uvec2(0xffffffff, 0);

	// PERF: We can certainly be smarter here with some scan algorithm trickery.
	const auto apply_range = [&](unsigned index, vec2 range) {
		range = range * (float(resolution_z) / context->get_render_parameters().z_far);
		if (range.y < 0.0f)
			return;
		range.x = muglm::max(range.x, 0.0f);

		uvec2 urange(range);
		urange.y = muglm::min(urange.y, resolution_z - 1);

		for (unsigned x = urange.x; x <= urange.y; x++)
		{
			auto &index_range = bindless.light_index_range[x];
			index_range.x = muglm::min(index_range.x, index);
			index_range.y = muglm::max(index_range.y, index);
		}
	};

	for (unsigned i = 0; i < bindless.count; i++)
	{
		vec2 range;
		if (bindless_light_is_point(i))
		{
			range = point_light_z_range(*context, bindless.transforms.lights[i].position,
			                            1.0f / bindless.transforms.lights[i].inv_radius);
		}
		else
			range = spot_light_z_range(*context, bindless.transforms.model[i]);

		apply_range(i, range);
	}

	auto *ranges = static_cast<uvec2 *>(cmd.update_buffer(*bindless.range_buffer, 0, bindless.range_buffer->get_create_info().size));
	memcpy(ranges, bindless.light_index_range.data(), resolution_z * sizeof(ivec2));
}

static vec2 project_sphere_flat(float view_xy, float view_z, float radius)
{
	// Goal here is to deal with the intersection problem in 2D.
	// Camera forms a cone with the sphere.
	// We want to intersect that cone with the near plane.
	// To do that we find minimum and maximum angles in 2D, rotate the direction vector,
	// and project down to plane.

	float len = length(vec2(view_xy, view_z));
	float sin_xy = radius / len;

	if (sin_xy < 0.999f)
	{
		// Find half-angles for the cone, and turn it into a 2x2 rotation matrix.
		float cos_xy = muglm::sqrt(1.0f - sin_xy * sin_xy);

		// Rotate half-angles in each direction.
		vec2 rot_lo = mat2(vec2(cos_xy, +sin_xy), vec2(-sin_xy, cos_xy)) * vec2(view_xy, view_z);
		vec2 rot_hi = mat2(vec2(cos_xy, -sin_xy), vec2(+sin_xy, cos_xy)) * vec2(view_xy, view_z);

		// Clip to some sensible ranges.
		if (rot_lo.y <= 0.0f)
		{
			rot_lo.x = -1.0f;
			rot_lo.y = 0.0f;
		}

		if (rot_hi.y <= 0.0f)
		{
			rot_hi.x = +1.0f;
			rot_hi.y = 0.0f;
		}

		return vec2(rot_lo.x / rot_lo.y, rot_hi.x / rot_hi.y);
	}
	else
		return vec2(-std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity());
}

struct ProjectedResult
{
	vec4 ranges;
	vec4 transformed_ranges;
	mat2 clip_transform;
	bool ellipsis;
};

static ProjectedResult project_sphere(const RenderContext &context,
                                      const vec3 &pos, float radius)
{
	ProjectedResult result;
	vec3 view = (context.get_render_parameters().view * vec4(pos, 1.0f)).xyz();

	// Work in projection space.
	view.y = -view.y;
	view.z = -view.z;

	result.ranges = vec4(project_sphere_flat(view.x, view.z, radius),
	                     project_sphere_flat(view.y, view.z, radius));

	// Need to rotate view space on the Z-axis so the ellipsis will
	// have its major axes orthogonal with X/Y.
	float xy_length = length(vec2(view.x, view.y));

	if (xy_length < 0.0001f)
	{
		result.clip_transform = mat2(1.0f);
	}
	else
	{
		float inv_xy_length = 1.0f / muglm::max(xy_length, 0.0000001f);
		result.clip_transform = mat2(vec2(view.x, -view.y) * inv_xy_length,
		                             vec2(view.y, view.x) * inv_xy_length);
	}

	vec2 transformed_xy = result.clip_transform * vec2(view.x, view.y);

	result.transformed_ranges = vec4(project_sphere_flat(transformed_xy.x, view.z, radius),
	                                 project_sphere_flat(transformed_xy.y, view.z, radius));

	result.ellipsis =
			result.transformed_ranges.x > -std::numeric_limits<float>::infinity() &&
			result.transformed_ranges.y < std::numeric_limits<float>::infinity() &&
			result.transformed_ranges.z > -std::numeric_limits<float>::infinity() &&
			result.transformed_ranges.w < std::numeric_limits<float>::infinity();

	return result;
}

void LightClusterer::update_bindless_mask_buffer_spot(uint32_t *masks, unsigned index)
{
	vector<uvec2> coverage;

	Rasterizer::CullMode cull;
	vec2 range = spot_light_z_range(*context, bindless.transforms.model[index]);
	if (range.x <= context->get_render_parameters().z_near && range.y >= context->get_render_parameters().z_far)
		cull = Rasterizer::CullMode::Both;
	else if (range.x <= context->get_render_parameters().z_near)
		cull = Rasterizer::CullMode::Back;
	else
		cull = Rasterizer::CullMode::Front;

	if (cull != Rasterizer::CullMode::Both)
	{
		auto mvp = context->get_render_parameters().view_projection * bindless.transforms.model[index];
		const vec4 spot_points[5] = {
				vec4(0.0f, 0.0f, 0.0f, 1.0f),
				vec4(+1.0f, +1.0f, -1.0f, 1.0f),
				vec4(-1.0f, +1.0f, -1.0f, 1.0f),
				vec4(-1.0f, -1.0f, -1.0f, 1.0f),
				vec4(+1.0f, -1.0f, -1.0f, 1.0f),
		};
		vec4 clip[5];
		Rasterizer::transform_vertices(clip, spot_points, 5, mvp);
		coverage.clear();

		static const unsigned indices[6 * 3] = {
				0, 1, 2,
				0, 2, 3,
				0, 3, 4,
				0, 4, 1,
				2, 1, 3,
				4, 3, 1,
		};

		Rasterizer::rasterize_conservative_triangles(coverage, clip,
		                                             indices, sizeof(indices) / sizeof(indices[0]),
		                                             uvec2(resolution_x, resolution_y),
		                                             cull);

		for (auto &cov : coverage)
		{
			unsigned linear_coord = cov.y * resolution_x + cov.x;
			auto *tile_list = masks + linear_coord * bindless.parameters.num_lights_32;
			tile_list[index >> 5] |= 1u << (index & 31);
		}
	}
	else
	{
		for (unsigned y = 0; y < resolution_y; y++)
		{
			for (unsigned x = 0; x < resolution_x; x++)
			{
				unsigned linear_coord = y * resolution_x + x;
				auto *tile_list = masks + linear_coord * bindless.parameters.num_lights_32;
				tile_list[index >> 5] |= 1u << (index & 31);
			}
		}
	}
}

void LightClusterer::update_bindless_mask_buffer_point(uint32_t *masks, unsigned index)
{
	vec2 inv_resolution = 1.0f / vec2(resolution_x, resolution_y);
	vec2 clip_scale = vec2(context->get_render_parameters().inv_projection[0][0],
	                       -context->get_render_parameters().inv_projection[1][1]);

	auto &pos = bindless.transforms.lights[index].position;
	float radius = 1.0f / bindless.transforms.lights[index].inv_radius;

	auto projection = project_sphere(*context, pos, radius);
	auto &ranges = projection.ranges;
	auto &transformed_ranges = projection.transformed_ranges;
	auto &clip_transform = projection.clip_transform;
	auto &ellipsis = projection.ellipsis;

	// Compute screen-space BB for projected sphere.
	ranges = ranges *
	         vec4(context->get_render_parameters().projection[0][0],
	              context->get_render_parameters().projection[0][0],
	              -context->get_render_parameters().projection[1][1],
	              -context->get_render_parameters().projection[1][1]) *
	         0.5f + 0.5f;

	ranges *= vec4(resolution_x, resolution_x, resolution_y, resolution_y);
	ranges = clamp(ranges, vec4(0.0f), vec4(resolution_x, resolution_x - 1,
	                                        resolution_y, resolution_y - 1));

	uvec4 uranges(ranges);

	if (ellipsis)
	{
		vec2 intersection_center = 0.5f * (transformed_ranges.xz() + transformed_ranges.yw());
		vec2 intersection_radius = transformed_ranges.yw() - intersection_center;

		vec2 inv_intersection_radius = 1.0f / intersection_radius;

		for (unsigned y = uranges.z; y <= uranges.w; y++)
		{
			for (unsigned x = uranges.x; x <= uranges.y; x++)
			{
				vec2 clip_lo = 2.0f * vec2(x, y) * inv_resolution - 1.0f;
				vec2 clip_hi = clip_lo + 2.0f * inv_resolution;
				clip_lo *= clip_scale;
				clip_hi *= clip_scale;

				vec2 dist_00 = clip_transform * vec2(clip_lo.x, clip_lo.y) - intersection_center;
				vec2 dist_01 = clip_transform * vec2(clip_lo.x, clip_hi.y) - intersection_center;
				vec2 dist_10 = clip_transform * vec2(clip_hi.x, clip_lo.y) - intersection_center;
				vec2 dist_11 = clip_transform * vec2(clip_hi.x, clip_hi.y) - intersection_center;

				dist_00 *= inv_intersection_radius;
				dist_01 *= inv_intersection_radius;
				dist_10 *= inv_intersection_radius;
				dist_11 *= inv_intersection_radius;

				float max_diag = muglm::max(distance(dist_00, dist_11), distance(dist_01, dist_10));
				float min_sq_dist = (1.0f + max_diag) * (1.0f + max_diag);

				if (dot(dist_00, dist_00) < min_sq_dist &&
				    dot(dist_01, dist_01) < min_sq_dist &&
				    dot(dist_10, dist_10) < min_sq_dist &&
				    dot(dist_11, dist_11) < min_sq_dist)
				{
					unsigned linear_coord = y * resolution_x + x;
					auto *tile_list = masks + linear_coord * bindless.parameters.num_lights_32;
					tile_list[index >> 5] |= 1u << (index & 31);
				}
			}
		}
	}
	else
	{
		for (unsigned y = uranges.z; y <= uranges.w; y++)
		{
			for (unsigned x = uranges.x; x <= uranges.y; x++)
			{
				unsigned linear_coord = y * resolution_x + x;
				auto *tile_list = masks + linear_coord * bindless.parameters.num_lights_32;
				tile_list[index >> 5] |= 1u << (index & 31);
			}
		}
	}
}

void LightClusterer::update_bindless_mask_buffer_cpu(Vulkan::CommandBuffer &cmd)
{
	if (bindless.parameters.num_lights == 0)
		return;

	size_t size = bindless.parameters.num_lights_32 * sizeof(uint32_t) * resolution_x * resolution_y;
	auto *masks = static_cast<uint32_t *>(cmd.update_buffer(*bindless.bitmask_buffer, 0, size));
	memset(masks, 0, size);

	for (unsigned i = 0; i < bindless.count; i++)
	{
		if (bindless_light_is_point(i))
			update_bindless_mask_buffer_point(masks, i);
		else
			update_bindless_mask_buffer_spot(masks, i);
	}
}

void LightClusterer::update_bindless_mask_buffer_gpu(Vulkan::CommandBuffer &cmd)
{
	if (bindless.parameters.num_lights == 0)
		return;

	cmd.barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
	            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

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

	cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
	            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

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

	cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
	            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

	assert((resolution_x & 7) == 0);
	assert((resolution_y & 7) == 0);

	bool use_subgroups = false;
	auto &features = cmd.get_device().get_device_features();
	unsigned tile_width = 1;
	unsigned tile_height = 1;

	constexpr VkSubgroupFeatureFlags required = VK_SUBGROUP_FEATURE_BALLOT_BIT | VK_SUBGROUP_FEATURE_BASIC_BIT;

	if (features.subgroup_size_control_features.subgroupSizeControl &&
	    features.subgroup_size_control_features.computeFullSubgroups &&
	    (features.subgroup_properties.supportedOperations & required) == required &&
	    (features.subgroup_properties.supportedStages & VK_SHADER_STAGE_COMPUTE_BIT) != 0)
	{
		// Our desired range is either 32 threads or 64 threads, 32 threads is preferred.

		if (features.subgroup_size_control_properties.minSubgroupSize <= 32 &&
		    features.subgroup_size_control_properties.maxSubgroupSize >= 32 &&
		    (features.subgroup_size_control_properties.requiredSubgroupSizeStages & VK_SHADER_STAGE_COMPUTE_BIT) != 0)
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
		else if (features.subgroup_size_control_properties.minSubgroupSize >= 32 &&
		         features.subgroup_size_control_properties.maxSubgroupSize <= 64)
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

void LightClusterer::build_cluster_bindless_cpu(Vulkan::CommandBuffer &cmd)
{
	update_bindless_data(cmd);
	update_bindless_range_buffer_cpu(cmd);
	update_bindless_mask_buffer_cpu(cmd);
}

void LightClusterer::build_cluster_bindless_gpu(Vulkan::CommandBuffer &cmd)
{
	update_bindless_data(cmd);
	update_bindless_mask_buffer_gpu(cmd);
	update_bindless_range_buffer_gpu(cmd);
}

void LightClusterer::build_cluster_cpu(Vulkan::CommandBuffer &cmd, Vulkan::ImageView &view)
{
	unsigned res_x = resolution_x;
	unsigned res_y = resolution_y;
	unsigned res_z = resolution_z;

#ifdef CLUSTERER_FORCE_TRANSFER_UPDATE
	auto &image = view.get_image();
	auto *image_data = static_cast<uvec4 *>(cmd.update_image(image, 0, 0));
#else
	// Copy to image using a compute pipeline so we know how it's implemented.
	BufferCreateInfo compute_staging_info = {};
	compute_staging_info.domain = BufferDomain::Host;
	compute_staging_info.size = res_x * res_y * res_z * (ClusterHierarchies + 1) * sizeof(uvec4);
	compute_staging_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	auto compute_staging = cmd.get_device().create_buffer(compute_staging_info, nullptr);
	auto *image_data = static_cast<uvec4 *>(cmd.get_device().map_host_buffer(*compute_staging, MEMORY_ACCESS_WRITE_BIT));

	{
		auto *copy_program = cmd.get_device().get_shader_manager().register_compute(
				"builtin://shaders/util/copy_buffer_to_image_3d.comp");
		auto *variant = copy_program->register_variant({});
		cmd.set_program(variant->get_program());
		cmd.set_storage_texture(0, 0, view);
		cmd.set_storage_buffer(0, 1, *compute_staging);

		struct Push
		{
			uint dim_x;
			uint dim_y;
			uint row_stride;
			uint height_stride;
		};

		const Push push = {
			res_x, res_y,
			res_x, res_x * res_y,
		};

		cmd.push_constants(&push, 0, sizeof(push));
		cmd.dispatch((res_x + 7) / 8, (res_y + 7) / 8, res_z * (ClusterHierarchies + 1));
	}
#endif

	legacy.cluster_list_buffer.clear();

	auto &workers = *Global::thread_group();
	auto task = workers.create_task();

	// Naive and simple multithreading :)
	// Pre-compute useful data structures before we go wide ...
	CPUGlobalAccelState state;
	state.inverse_cluster_transform = inverse(legacy.cluster_transform);
	state.inv_res = vec3(1.0f / res_x, 1.0f / res_y, 1.0f / res_z);
	state.radius = 0.5f * length(mat3(state.inverse_cluster_transform) * (vec3(2.0f, 2.0f, 0.5f) * state.inv_res));

	for (unsigned i = 0; i < legacy.spots.count; i++)
	{
		state.spot_position[i] = legacy.spots.lights[i].position;
		state.spot_direction[i] = legacy.spots.lights[i].direction;
		state.spot_size[i] = 1.0f / legacy.spots.lights[i].inv_radius;
		state.spot_angle_cos[i] = cosf(legacy.spots.handles[i]->get_xy_range());
		state.spot_angle_sin[i] = sinf(legacy.spots.handles[i]->get_xy_range());
	}

	for (unsigned i = 0; i < legacy.points.count; i++)
	{
		state.point_position[i] = legacy.points.lights[i].position;
		state.point_size[i] = 1.0f / legacy.points.lights[i].inv_radius;
	}

	for (unsigned slice = 0; slice < ClusterHierarchies + 1; slice++)
	{
		float world_scale_factor;
		float z_bias;

		if (slice == 0)
		{
			world_scale_factor = 1.0f;
			z_bias = 0.0f;
		}
		else
		{
			world_scale_factor = exp2(float(slice - 1));
			z_bias = 0.5f;
		}

		for (unsigned cz = 0; cz < res_z; cz += ClusterPrepassDownsample)
		{
			// Four slices per task.
			task->enqueue_task([&, z_bias, world_scale_factor, slice, cz]() {
				CPULocalAccelState local_state;
				local_state.world_scale_factor = world_scale_factor;
				local_state.z_bias = z_bias;
				local_state.cube_radius = state.radius * world_scale_factor;

				uint32_t cached_spot_mask = 0;
				uint32_t cached_point_mask = 0;
				uvec4 cached_node = uvec4(0);

				vector<uint32_t> tmp_list_buffer;
				vector<uvec4> image_base;
				if (ImplementationQuirks::get().clustering_list_iteration)
					image_base.resize(ClusterPrepassDownsample * res_x * res_y);

				auto *image_output_base = &image_data[slice * res_z * res_y * res_x + cz * res_y * res_x];

				// Add a small guard band for safety.
				float range_z = z_bias + (0.5f * (cz + ClusterPrepassDownsample + 0.5f)) / res_z;
				int min_x = int(std::floor((0.5f - 0.5f * range_z) * res_x));
				int max_x = int(std::ceil((0.5f + 0.5f * range_z) * res_x));
				int min_y = int(std::floor((0.5f - 0.5f * range_z) * res_y));
				int max_y = int(std::ceil((0.5f + 0.5f * range_z) * res_y));

				min_x = clamp(min_x, 0, int(res_x));
				max_x = clamp(max_x, 0, int(res_x));
				min_y = clamp(min_y, 0, int(res_y));
				max_y = clamp(max_y, 0, int(res_y));

				uvec2 pre_mask((1ull << legacy.spots.count) - 1,
				               (1ull << legacy.points.count) - 1);

				for (int cy = min_y; cy < max_y; cy += ClusterPrepassDownsample)
				{
					for (int cx = min_x; cx < max_x; cx += ClusterPrepassDownsample)
					{
						int target_x = std::min(cx + ClusterPrepassDownsample, max_x);
						int target_y = std::min(cy + ClusterPrepassDownsample, max_y);

						auto res = cluster_lights_cpu(cx, cy, cz, state, local_state,
						                              float(ClusterPrepassDownsample),
						                              pre_mask);

						// No lights in large block? Quick eliminate.
						if (!res.x && !res.y)
						{
							if (!ImplementationQuirks::get().clustering_list_iteration)
							{
								for (int sz = 0; sz < 4; sz++)
									for (int sy = cy; sy < target_y; sy++)
										for (int sx = cx; sx < target_x; sx++)
											image_output_base[sz * res_y * res_x + sy * res_x + sx] = uvec4(0u);
							}
							continue;
						}

						for (int sz = 0; sz < 4; sz++)
						{
							for (int sy = cy; sy < target_y; sy++)
							{
								for (int sx = cx; sx < target_x; sx++)
								{
									auto final_res = cluster_lights_cpu(sx, sy, sz + int(cz), state, local_state, 1.0f, res);

									if (!ImplementationQuirks::get().clustering_list_iteration)
									{
										image_output_base[sz * res_y * res_x + sy * res_x + sx] = uvec4(final_res, 0u, 0u);
									}
									else if (cached_spot_mask == final_res.x && cached_point_mask == final_res.y)
									{
										// Neighbor blocks have a high likelihood of sharing the same lights,
										// try to conserve memory.
										image_base[sz * res_y * res_x + sy * res_x + sx] = cached_node;
									}
									else
									{
										uint32_t spot_count = 0;
										uint32_t point_count = 0;
										uint32_t spot_start = tmp_list_buffer.size();

										Util::for_each_bit(final_res.x, [&](uint32_t bit) {
											tmp_list_buffer.push_back(bit);
											spot_count++;
										});

										uint32_t point_start = tmp_list_buffer.size();

										Util::for_each_bit(final_res.y, [&](uint32_t bit) {
											tmp_list_buffer.push_back(bit);
											point_count++;
										});

										uvec4 node(spot_start, spot_count, point_start, point_count);
										image_base[sz * res_y * res_x + sy * res_x + sx] = node;
										cached_spot_mask = final_res.x;
										cached_point_mask = final_res.y;
										cached_node = node;
									}
								}
							}
						}
					}
				}

				if (ImplementationQuirks::get().clustering_list_iteration)
				{
					size_t cluster_offset = 0;
					{
						lock_guard<mutex> holder{legacy.cluster_list_lock};
						cluster_offset = legacy.cluster_list_buffer.size();
						legacy.cluster_list_buffer.resize(cluster_offset + tmp_list_buffer.size());
						memcpy(legacy.cluster_list_buffer.data() + cluster_offset, tmp_list_buffer.data(),
						       tmp_list_buffer.size() * sizeof(uint32_t));
					}

					unsigned elems = ClusterPrepassDownsample * res_x * res_y;
					for (unsigned i = 0; i < elems; i++)
						image_output_base[i] = image_base[i] + uvec4(cluster_offset, 0, cluster_offset, 0);
				}
			});
		}
	}

	task->flush();
	task->wait();

	if (!legacy.cluster_list_buffer.empty())
	{
		// Just allocate a fresh buffer every frame.
		BufferCreateInfo info = {};
		info.domain = BufferDomain::Device;
		info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		info.size = legacy.cluster_list_buffer.size() * sizeof(legacy.cluster_list_buffer[0]);
		legacy.cluster_list = cmd.get_device().create_buffer(info, legacy.cluster_list_buffer.data());
		//LOGI("Cluster list has %u elements.\n", unsigned(cluster_list_buffer.size()));
	}
	else if (ImplementationQuirks::get().clustering_list_iteration)
	{
		BufferCreateInfo info = {};
		info.domain = BufferDomain::Device;
		info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		info.size = sizeof(uvec4);
		static const uvec4 dummy(0u);
		legacy.cluster_list = cmd.get_device().create_buffer(info, &dummy);
	}
	else
		legacy.cluster_list.reset();
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
		uvec4(res_x, res_y, res_z, trailing_zeroes(res_z)),
		vec4(1.0f / res_x, 1.0f / res_y, 1.0f / ((ClusterHierarchies + 1) * res_z), 1.0f),
		vec4(inv_res, radius),
		legacy.spots.count, legacy.points.count,
	};
	cmd.push_constants(&push, 0, sizeof(push));
	cmd.dispatch((res_x + 3) / 4, (res_y + 3) / 4, (ClusterHierarchies + 1) * ((res_z + 3) / 4));
}

void LightClusterer::add_render_passes_bindless(RenderGraph &graph)
{
	if (ImplementationQuirks::get().clustering_force_cpu)
	{
		BufferInfo att;
		att.persistent = true;
		att.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

		auto &pass = graph.add_pass("clustering", RENDER_GRAPH_QUEUE_COMPUTE_BIT);

		{
			att.size = resolution_x * resolution_y * (MaxLightsBindless / 8);
			pass.add_transfer_output("cluster-bitmask", att);
		}

		{
			att.size = resolution_z * sizeof(ivec2);
			pass.add_transfer_output("cluster-range", att);
		}

		{
			att.size = sizeof(ClustererBindlessTransforms);
			pass.add_transfer_output("cluster-transforms", att);
		}

		pass.set_build_render_pass([&](Vulkan::CommandBuffer &cmd) {
			build_cluster_bindless_cpu(cmd);
		});
	}
	else
	{
		BufferInfo att;
		att.persistent = true;
		att.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

		auto &pass = graph.add_pass("clustering", RENDER_GRAPH_QUEUE_COMPUTE_BIT);

		{
			att.size = resolution_x * resolution_y * (MaxLightsBindless / 8);
			pass.add_storage_output("cluster-bitmask", att);
		}

		{
			att.size = resolution_z * sizeof(ivec2);
			pass.add_storage_output("cluster-range", att);
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
	att.persistent = true;

	if (ImplementationQuirks::get().clustering_list_iteration || ImplementationQuirks::get().clustering_force_cpu)
	{
#ifdef CLUSTERER_FORCE_TRANSFER_UPDATE
		auto &pass = graph.add_pass("clustering", RENDER_GRAPH_QUEUE_COMPUTE_BIT);
		pass.add_blit_texture_output("light-cluster", att);
#else
		auto &pass = graph.add_pass("clustering", RENDER_GRAPH_QUEUE_COMPUTE_BIT);
		pass.add_storage_texture_output("light-cluster", att);
#endif
		pass.set_build_render_pass([this](Vulkan::CommandBuffer &cmd) {
			build_cluster_cpu(cmd, *legacy.target);
		});
	}
	else
	{
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
			                  VK_ACCESS_SHADER_WRITE_BIT,
			                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
			build_cluster(cmd, *legacy.target, legacy.pre_cull_target);
		});
	}
}

void LightClusterer::add_render_passes(RenderGraph &graph)
{
	if (enable_clustering)
	{
		if (enable_bindless)
			add_render_passes_bindless(graph);
		else
			add_render_passes_legacy(graph);
	}
}

void LightClusterer::set_base_renderer(const RendererSuite *suite)
{
	renderer_suite = suite;
}
}
