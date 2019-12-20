/* Copyright (c) 2017-2019 Hans-Kristian Arntzen
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

#pragma once

#include "lights.hpp"
#include "render_components.hpp"
#include "event.hpp"
#include "shader_manager.hpp"
#include "renderer.hpp"

namespace Granite
{
class LightClusterer : public RenderPassCreator, public EventHandler, public PerFrameRefreshable
{
public:
	LightClusterer();

	enum class ShadowType
	{
		PCF,
		VSM
	};

	void set_enable_shadows(bool enable);
	void set_force_update_shadows(bool enable);
	void set_enable_clustering(bool enable);
	void set_enable_bindless(bool enable);
	void set_shadow_type(ShadowType shadow_type);

	void set_resolution(unsigned x, unsigned y, unsigned z);
	void set_shadow_resolution(unsigned res);

	// Legacy clustering.
	const Vulkan::ImageView *get_cluster_image() const;
	const Vulkan::Buffer *get_cluster_list_buffer() const;
	const Vulkan::ImageView *get_spot_light_shadows() const;
	const Vulkan::ImageView *get_point_light_shadows() const;
	const PositionalFragmentInfo *get_active_point_lights() const;
	const PositionalFragmentInfo *get_active_spot_lights() const;
	const mat4 *get_active_spot_light_shadow_matrices() const;
	const PointTransform *get_active_point_light_shadow_transform() const;
	unsigned get_active_point_light_count() const;
	unsigned get_active_spot_light_count() const;
	const mat4 &get_cluster_transform() const;

	// Bindless clustering.
	const ClustererParametersBindless &get_cluster_parameters_bindless() const;
	const Vulkan::Buffer *get_cluster_transform_buffer() const;
	const Vulkan::Buffer *get_cluster_bitmask_buffer() const;
	const Vulkan::Buffer *get_cluster_range_buffer() const;
	VkDescriptorSet get_cluster_shadow_map_bindless_set() const;
	bool clusterer_is_bindless() const;

	void set_scene(Scene *scene) override;
	void set_base_renderer(Renderer *forward_renderer, Renderer *deferred_renderer, Renderer *depth_renderer) override;
	void set_base_render_context(const RenderContext *context) override;

	enum { MaxLights = 32, ClusterHierarchies = 8, ClusterPrepassDownsample = 4 };

	void set_max_spot_lights(unsigned count)
	{
		max_spot_lights = count;
	}

	void set_max_point_lights(unsigned count)
	{
		max_point_lights = count;
	}

private:
	void add_render_passes(RenderGraph &graph) override;
	void add_render_passes_legacy(RenderGraph &graph);
	void add_render_passes_bindless(RenderGraph &graph);

	void setup_render_pass_dependencies(RenderGraph &graph, RenderPass &target) override;
	void setup_render_pass_resources(RenderGraph &graph) override;
	void refresh(RenderContext &context_) override;
	void refresh_bindless(RenderContext &context_);
	void refresh_legacy(RenderContext &context_);

	Scene *scene = nullptr;
	const RenderContext *context = nullptr;
	ComponentGroupVector<PositionalLightComponent, RenderInfoComponent> *lights = nullptr;

	unsigned resolution_x = 64, resolution_y = 32, resolution_z = 16;
	unsigned shadow_resolution = 512;
	unsigned max_spot_lights = MaxLights;
	unsigned max_point_lights = MaxLights;
	void build_cluster(Vulkan::CommandBuffer &cmd, Vulkan::ImageView &view, const Vulkan::ImageView *pre_culled);
	void build_cluster_cpu(Vulkan::CommandBuffer &cmd, Vulkan::ImageView &view);
	void build_cluster_bindless(Vulkan::CommandBuffer &cmd);
	void on_device_created(const Vulkan::DeviceCreatedEvent &e);
	void on_device_destroyed(const Vulkan::DeviceCreatedEvent &e);
	Vulkan::ShaderProgram *program = nullptr;
	Vulkan::ImageView *target = nullptr;
	Vulkan::ImageView *pre_cull_target = nullptr;
	Vulkan::BufferHandle cluster_list;
	unsigned inherit_variant = 0;
	unsigned cull_variant = 0;

	struct
	{
		PositionalFragmentInfo lights[MaxLights] = {};
		PointLight *handles[MaxLights] = {};
		PointTransform transforms[MaxLights] = {};
		unsigned cookie[MaxLights] = {};
		unsigned count = 0;
		uint8_t index_remap[MaxLights];
		Vulkan::ImageHandle atlas;
	} points;

	struct
	{
		PositionalFragmentInfo lights[MaxLights] = {};
		SpotLight *handles[MaxLights] = {};
		mat4 transforms[MaxLights] = {};
		unsigned cookie[MaxLights] = {};
		unsigned count = 0;
		uint8_t index_remap[MaxLights];
		Vulkan::ImageHandle atlas;
	} spots;

	mat4 cluster_transform;
	std::vector<uint32_t> cluster_list_buffer;
	std::mutex cluster_list_lock;

	Renderer *depth_renderer = nullptr;
	void render_atlas_spot(RenderContext &context_);
	void render_atlas_point(RenderContext &context_);

	bool enable_shadows = true;
	bool enable_clustering = true;
	bool enable_bindless = false;
	bool force_update_shadows = false;
	ShadowType shadow_type = ShadowType::PCF;

	struct CPUGlobalAccelState
	{
		mat4 inverse_cluster_transform;
		vec3 spot_position[MaxLights];
		vec3 spot_direction[MaxLights];
		float spot_size[MaxLights];
		float spot_angle_sin[MaxLights];
		float spot_angle_cos[MaxLights];
		vec3 point_position[MaxLights];
		float point_size[MaxLights];

		vec3 inv_res;
		float radius;
	};

	struct CPULocalAccelState
	{
		float cube_radius;
		float world_scale_factor;
		float z_bias;
	};
	uvec2 cluster_lights_cpu(int x, int y, int z,
	                         const CPUGlobalAccelState &state,
	                         const CPULocalAccelState &local,
	                         float scale, uvec2 pre_mask);

	void render_shadow(Vulkan::CommandBuffer &cmd,
	                   RenderContext &context,
	                   VisibilityList &visibility,
	                   unsigned off_x, unsigned off_y,
	                   unsigned res_x, unsigned res_y,
	                   Vulkan::ImageView &rt, unsigned layer,
	                   Renderer::RendererFlushFlags flags);
	Vulkan::ImageHandle scratch_vsm_rt;
	Vulkan::ImageHandle scratch_vsm_down;

	// Bindless
	struct
	{
		ClustererParametersBindless parameters;
		Vulkan::BindlessDescriptorPoolHandle descriptor_pool;

		const Vulkan::Buffer *bitmask_buffer = nullptr;
		const Vulkan::Buffer *range_buffer = nullptr;
		const Vulkan::Buffer *transforms_buffer = nullptr;
		VkDescriptorSet desc_set = VK_NULL_HANDLE;
	} bindless;

	void update_bindless_descriptors(Vulkan::CommandBuffer &cmd);
	void update_bindless_range_buffer(Vulkan::CommandBuffer &cmd);
	void update_bindless_mask_buffer(Vulkan::CommandBuffer &cmd);
	void render_bindless_spot(RenderContext &context);
	void render_bindless_point(RenderContext &context);
};
}