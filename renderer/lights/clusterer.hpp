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

#pragma once

#include "lights.hpp"
#include "render_components.hpp"
#include "event.hpp"
#include "shader_manager.hpp"
#include "renderer.hpp"
#include "lru_cache.hpp"
#include "threaded_scene.hpp"
#include "render_context.hpp"
#include "render_graph.hpp"
#include "descriptor_set.hpp"

namespace Granite
{
class LightClusterer : public RenderPassCreator,
                       public EventHandler,
                       public PerFrameRefreshable,
                       public RenderPassExternalLockInterface
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
	ShadowType get_shadow_type() const;

	void set_resolution(unsigned x, unsigned y, unsigned z);
	void set_shadow_resolution(unsigned res);

	// Legacy clustering.
	const Vulkan::ImageView *get_cluster_image() const;
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
	const ClustererGlobalTransforms &get_cluster_global_transforms_bindless() const;
	const Vulkan::Buffer *get_cluster_transform_buffer() const;
	const Vulkan::Buffer *get_cluster_bitmask_buffer() const;
	const Vulkan::Buffer *get_cluster_range_buffer() const;
	VkDescriptorSet get_cluster_bindless_set() const;
	bool clusterer_is_bindless() const;

	void set_enable_volumetric_diffuse(bool enable);
	bool clusterer_has_volumetric_diffuse() const;
	const ClustererParametersVolumetric &get_cluster_volumetric_diffuse_data() const;
	size_t get_cluster_volumetric_diffuse_size() const;

	void set_enable_volumetric_fog(bool enable);
	bool clusterer_has_volumetric_fog() const;
	const ClustererParametersFogRegions &get_cluster_volumetric_fog_data() const;
	size_t get_cluster_volumetric_fog_size() const;

	void set_enable_volumetric_decals(bool enable);
	bool clusterer_has_volumetric_decals() const;
	const Vulkan::Buffer *get_cluster_bitmask_decal_buffer() const;
	const Vulkan::Buffer *get_cluster_range_decal_buffer() const;

	void set_scene(Scene *scene) override;
	void set_base_renderer(const RendererSuite *suite) override;
	void set_base_render_context(const RenderContext *context) override;

	enum
	{
		MaxLights = CLUSTERER_MAX_LIGHTS,
		MaxLightsBindless = CLUSTERER_MAX_LIGHTS_BINDLESS,
		MaxLightsGlobal = CLUSTERER_MAX_LIGHTS_GLOBAL,
		MaxLightsVolume = CLUSTERER_MAX_VOLUMES,
		MaxFogRegions = CLUSTERER_MAX_FOG_REGIONS,
		MaxDecalsBindless = CLUSTERER_MAX_DECALS_BINDLESS,
		ClusterHierarchies = 8,
		ClusterPrepassDownsample = 4
	};

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

	void setup_render_pass_dependencies(RenderGraph &graph, RenderPass &target,
	                                    RenderPassCreator::DependencyFlags dep_flags) override;
	void setup_render_pass_dependencies(RenderGraph &graph) override;
	void setup_render_pass_resources(RenderGraph &graph) override;
	void refresh(const RenderContext &context_, TaskComposer &composer) override;
	void refresh_bindless(const RenderContext &context_, TaskComposer &composer);

	template <typename Transforms>
	unsigned scan_visible_positional_lights(const PositionalLightList &lights, Transforms &transforms,
	                                        unsigned max_lights, unsigned handle_offset);

	void refresh_bindless_prepare(const RenderContext &context_);
	void refresh_legacy(const RenderContext &context_);

	Scene *scene = nullptr;
	const RenderContext *context = nullptr;
	const ComponentGroupVector<PositionalLightComponent, RenderInfoComponent> *lights = nullptr;

	enum { MaxTasks = 4 };
	PositionalLightList light_sort_caches[MaxTasks];
	VolumetricDiffuseLightList visible_diffuse_lights;
	VolumetricFogRegionList visible_fog_regions;
	VolumetricDecalList visible_decals;
	PositionalLightList existing_global_lights;
	RenderQueue internal_queue;

	unsigned resolution_x = 64, resolution_y = 32, resolution_z = 16;
	unsigned shadow_resolution = 512;
	unsigned max_spot_lights = MaxLights;
	unsigned max_point_lights = MaxLights;
	void build_cluster(Vulkan::CommandBuffer &cmd, Vulkan::ImageView &view, const Vulkan::ImageView *pre_culled);
	void build_cluster_bindless_gpu(Vulkan::CommandBuffer &cmd);
	void on_pipeline_created(const Vulkan::DevicePipelineReadyEvent &e);
	void on_pipeline_destroyed(const Vulkan::DevicePipelineReadyEvent &e);

	struct
	{
		struct
		{
			PositionalFragmentInfo lights[MaxLights] = {};
			PointLight *handles[MaxLights] = {};
			PointTransform shadow_transforms[MaxLights] = {};
			vec4 model_transforms[MaxLights] = {};
			unsigned cookie[MaxLights] = {};
			unsigned count = 0;
			uint8_t index_remap[MaxLights];
			Vulkan::ImageHandle atlas;
		} points;

		struct
		{
			PositionalFragmentInfo lights[MaxLights] = {};
			SpotLight *handles[MaxLights] = {};
			mat4 shadow_transforms[MaxLights] = {};
			unsigned cookie[MaxLights] = {};
			unsigned count = 0;
			uint8_t index_remap[MaxLights];
			Vulkan::ImageHandle atlas;
		} spots;

		Vulkan::ShaderProgramVariant *inherit_variant = nullptr;
		Vulkan::ShaderProgramVariant *cull_variant = nullptr;

		mat4 cluster_transform;

		Vulkan::ShaderProgram *program = nullptr;
		Vulkan::ImageView *target = nullptr;
		Vulkan::ImageView *pre_cull_target = nullptr;
	} legacy;

	const RendererSuite *renderer_suite = nullptr;
	void render_atlas_spot(const RenderContext &context_);
	void render_atlas_point(const RenderContext &context_);

	bool enable_shadows = true;
	bool enable_clustering = true;
	bool enable_bindless = false;
	bool force_update_shadows = false;
	bool enable_volumetric_diffuse = false;
	bool enable_volumetric_fog = false;
	bool enable_volumetric_decals = false;
	ShadowType shadow_type = ShadowType::PCF;

	void render_shadow_legacy(Vulkan::CommandBuffer &cmd,
	                          const RenderContext &context,
	                          VisibilityList &visibility,
	                          unsigned off_x, unsigned off_y,
	                          unsigned res_x, unsigned res_y,
	                          const Vulkan::ImageView &rt, unsigned layer,
	                          Renderer::RendererFlushFlags flags);

	void render_shadow(Vulkan::CommandBuffer &cmd,
	                   const RenderContext &context,
	                   const RenderQueue &queue,
	                   unsigned off_x, unsigned off_y,
	                   unsigned res_x, unsigned res_y,
	                   const Vulkan::ImageView &rt, unsigned layer,
	                   Renderer::RendererFlushFlags flags) const;

	void setup_scratch_buffers_vsm(Vulkan::Device &device);

	Vulkan::ImageHandle scratch_vsm_rt;
	Vulkan::ImageHandle scratch_vsm_down;

	struct ShadowTaskBase : Util::ThreadSafeIntrusivePtrEnabled<ShadowTaskBase>
	{
		virtual ~ShadowTaskBase() = default;
	};
	using ShadowTaskHandle = Util::IntrusivePtr<ShadowTaskBase>;

	// Bindless
	struct
	{
		ClustererParametersBindless parameters;
		ClustererBindlessTransforms transforms;
		ClustererGlobalTransforms global_transforms;
		ClustererParametersVolumetric volumetric;
		ClustererParametersFogRegions fog_regions;

		PositionalLight *handles[MaxLightsBindless + MaxLightsGlobal] = {};

		Vulkan::BindlessAllocator allocator;

		Util::LRUCache<Vulkan::ImageHandle> shadow_map_cache;

		const Vulkan::Buffer *bitmask_buffer = nullptr;
		const Vulkan::Buffer *range_buffer = nullptr;
		const Vulkan::Buffer *bitmask_buffer_decal = nullptr;
		const Vulkan::Buffer *range_buffer_decal = nullptr;
		const Vulkan::Buffer *transforms_buffer = nullptr;

		const Vulkan::Buffer *transformed_spots = nullptr;
		const Vulkan::Buffer *cull_data = nullptr;

		VkDescriptorSet desc_set = VK_NULL_HANDLE;

		std::vector<uvec2> volume_index_range;

		std::vector<VkImageMemoryBarrier2> shadow_barriers;
		std::vector<const Vulkan::Image *> shadow_images;
		std::vector<ShadowTaskHandle> shadow_task_handles;
		std::vector<Util::Hash> light_transform_hashes;
	} bindless;

	void update_bindless_descriptors(Vulkan::Device &device);
	void update_bindless_data(Vulkan::CommandBuffer &cmd);
	void update_bindless_range_buffer_gpu(Vulkan::CommandBuffer &cmd);
	void update_bindless_range_buffer_decal_gpu(Vulkan::CommandBuffer &cmd);
	void update_bindless_mask_buffer_gpu(Vulkan::CommandBuffer &cmd);
	void update_bindless_mask_buffer_decal_gpu(Vulkan::CommandBuffer &cmd);
	void begin_bindless_barriers(Vulkan::CommandBuffer &cmd);
	void end_bindless_barriers(Vulkan::CommandBuffer &cmd);

	void update_bindless_range_buffer_gpu(Vulkan::CommandBuffer &cmd, const Vulkan::Buffer &range_buffer,
	                                      const std::vector<uvec2> &index_range);
	uvec2 compute_uint_range(vec2 range) const;

	template <unsigned Faces, unsigned MaxTasks>
	struct ShadowTaskContext : ShadowTaskBase
	{
		RenderContext depth_context[Faces];
		VisibilityList visibility[Faces][MaxTasks];
		Util::Hash hashes[Faces][MaxTasks];
		RenderQueue queues[Faces][MaxTasks];

		Util::Hash get_combined_hash(Util::Hash self_transform) const
		{
			Util::Hasher hasher;
			hasher.u64(self_transform);
			for (unsigned face = 0; face < Faces; face++)
			{
				Util::Hash h = 0;
				for (auto &hash : hashes[face])
					h ^= hash;
				hasher.u64(h);
			}
			return hasher.get();
		}
	};
	using ShadowTaskContextSpot = ShadowTaskContext<1, MaxTasks>;
	using ShadowTaskContextPoint = ShadowTaskContext<6, MaxTasks>;
	using ShadowTaskContextSpotHandle = Util::IntrusivePtr<ShadowTaskContextSpot>;
	using ShadowTaskContextPointHandle = Util::IntrusivePtr<ShadowTaskContextPoint>;

	ShadowTaskContextSpotHandle gather_bindless_spot_shadow_renderables(unsigned index, TaskComposer &composer,
	                                                                    bool requires_rendering);
	ShadowTaskContextPointHandle gather_bindless_point_shadow_renderables(unsigned index, TaskComposer &composer,
	                                                                      bool requires_rendering);

	void render_bindless_spot(Vulkan::Device &device, unsigned index, TaskComposer &composer);
	void render_bindless_point(Vulkan::Device &device, unsigned index, TaskComposer &composer);

	bool bindless_light_is_point(unsigned index) const;

	const Renderer &get_shadow_renderer() const;

	Vulkan::Semaphore external_acquire() override;
	void external_release(Vulkan::Semaphore sem) override;
	Vulkan::Semaphore acquire_semaphore;
	Util::SmallVector<Vulkan::Semaphore> release_semaphores;
};
}
