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

#include "scene.hpp"
#include "renderer.hpp"
#include "render_queue.hpp"
#include "render_context.hpp"
#include "render_graph.hpp"

namespace Granite
{
enum SceneRendererFlagBits : uint32_t
{
	SCENE_RENDERER_FORWARD_OPAQUE_BIT = 1 << 0,
	SCENE_RENDERER_FORWARD_TRANSPARENT_BIT = 1 << 1,
	SCENE_RENDERER_Z_PREPASS_BIT = 1 << 2,
	SCENE_RENDERER_DEFERRED_GBUFFER_BIT = 1 << 3,
	SCENE_RENDERER_SEPARATE_PER_LAYER_BIT = 1 << 4,
	SCENE_RENDERER_DEFERRED_LIGHTING_BIT = 1 << 5,
	SCENE_RENDERER_DEFERRED_CLUSTER_BIT = 1 << 6,
	SCENE_RENDERER_SHADOW_PCF_WIDE_BIT = 1 << 7,
	SCENE_RENDERER_SHADOW_VSM_BIT = 1 << 8,
	SCENE_RENDERER_DEPTH_BIT = 1 << 9,
	SCENE_RENDERER_DEPTH_STATIC_BIT = 1 << 10,
	SCENE_RENDERER_DEPTH_DYNAMIC_BIT = 1 << 11,
	SCENE_RENDERER_Z_EXISTING_PREPASS_BIT = 1 << 12,
	SCENE_RENDERER_DEBUG_PROBES_BIT = 1 << 13,
	SCENE_RENDERER_FALLBACK_DEPTH_BIT = 1 << 14,
	SCENE_RENDERER_MOTION_VECTOR_BIT = 1 << 15,
	SCENE_RENDERER_SKIP_UNBOUNDED_BIT = 1 << 16,
	SCENE_RENDERER_SKIP_OPAQUE_FLOATING_BIT = 1 << 17,
	SCENE_RENDERER_MOTION_VECTOR_FULL_BIT = 1 << 18, // Reconstruct MVs even for static objects.
};
using SceneRendererFlags = uint32_t;

class RenderPassSceneRenderer : public RenderPassInterface
{
public:
	struct Setup
	{
		Scene *scene;
		const RendererSuite *suite;
		SceneRendererFlags flags;

		// For per-layer rendering, each layer gets its own context.
		const RenderContext *context;
		unsigned layers;
	};
	void init(const Setup &setup);
	void set_clear_color(const VkClearColorValue &value);
	void set_extra_flush_flags(Renderer::RendererFlushFlags flags);

	void build_render_pass(Vulkan::CommandBuffer &cmd, unsigned layer) const;
	void build_render_pass(Vulkan::CommandBuffer &cmd) override;
	void build_render_pass_separate_layer(Vulkan::CommandBuffer &cmd, unsigned layer) override;
	bool get_clear_color(unsigned attachment, VkClearColorValue *value) const override;
	void enqueue_prepare_render_pass(RenderGraph &graph, TaskComposer &composer) override;

	// An immediate version of enqueue_prepare_render_pass.
	void prepare_render_pass();

	bool render_pass_is_separate_layered() const override final;

protected:
	Setup setup_data = {};
	VkClearColorValue clear_color_value = {};
	Renderer::RendererFlushFlags flush_flags = 0;

	// These need to be per-thread, and thus are hoisted out as state in RenderPassSceneRenderer.
	enum { MaxTasks = 4 };
	VisibilityList visible_per_task[MaxTasks];
	VisibilityList visible_per_task_transparent[MaxTasks];
	RenderQueue queue_per_task_depth[MaxTasks];
	RenderQueue queue_per_task_opaque[MaxTasks];
	RenderQueue queue_per_task_transparent[MaxTasks];
	mutable RenderQueue queue_non_tasked;

	void build_render_pass_inner(Vulkan::CommandBuffer &cmd, unsigned layer) const;
	void setup_debug_probes();
	void render_debug_probes(const Renderer &renderer, Vulkan::CommandBuffer &cmd, RenderQueue &queue,
	                         const RenderContext &context) const;
	AbstractRenderableHandle debug_probe_mesh;
	const ComponentGroupVector<VolumetricDiffuseLightComponent> *volumetric_diffuse_lights = nullptr;

	void prepare_setup_queues();
	void resolve_full_motion_vectors(Vulkan::CommandBuffer &cmd, const RenderContext &context) const;
};

class SceneTransformManager final : public EventHandler,
                                    RenderPassCreator,
                                    RenderPassExternalLockInterface,
                                    PerFrameRefreshable
{
public:
	SceneTransformManager();
	void init(Scene &scene);

	// Every RenderContext that needs to render meshes in an occlusion cullable way
	// should allocate once instance.
	void register_persistent_render_context(RenderContext *context);

	// For RenderContexts that just want to render stuff once and forget about it
	// e.g. positional lights rendering and other misc stuff which allocates
	// contexts on the fly.
	void register_one_shot_render_context(RenderContext *context);

	const Vulkan::Buffer *get_transforms() const { return transforms.get(); }
	const Vulkan::Buffer *get_prev_transforms() const { return prev_transforms.get(); }
	const Vulkan::Buffer *get_aabbs() const { return aabbs.get(); }
	const Vulkan::Buffer *get_scene_task_buffer() const { return task_buffer.get(); }
	const Vulkan::Buffer *get_occlusion_state(unsigned index) const { return per_context_data[index].occlusions.get(); }

	const Vulkan::Buffer *get_task_buffer() const { return task_buffer.get(); }

	struct MDICall
	{
		const Vulkan::Buffer *indirect_buffer;
		VkDeviceSize indirect_offset;
		const Vulkan::Buffer *indirect_count;
		VkDeviceSize indirect_count_offset;
		uint32_t indirect_count_max;
	};

	enum class CullingPhase
	{
		First = 0, // Everything visible last frame.
		Second, // Everything visible this frame (minus visible in first in most cases)
		MotionVector, // Everything visible this frame which needs MV rendering. Used by motion vector rendering, etc.
		Count,
	};

	MDICall get_mdi_call_parameters(CullingPhase phase, DrawPipeline pipe, bool skinned) const;

private:
	const ComponentGroupVector<
		RenderableComponent,
		MeshletComponent,
		RenderInfoComponent,
		CachedSpatialTransformTimestampComponent> *meshlets = nullptr;

	Vulkan::CommandBuffer::Type owning_queue_type() const override;
	const char *get_ident() const override;
	void add_render_passes(RenderGraph &graph) override;
	void set_base_renderer(const RendererSuite *suite) override;
	void set_base_render_context(const RenderContext *context) override;
	void setup_render_pass_dependencies(RenderGraph &graph, RenderPass &target, DependencyFlags dep_flags) override;
	void setup_render_pass_dependencies(RenderGraph &graph) override;
	void setup_render_pass_resources(RenderGraph &graph) override;
	void set_scene(Scene *scene) override;
	void refresh(const RenderContext &context, TaskComposer &composer) override;

	void on_device_created(const Vulkan::DeviceCreatedEvent &event);
	void on_device_destroyed(const Vulkan::DeviceCreatedEvent &event);

	void update_scene_buffers();

	Vulkan::Device *device = nullptr;
	Vulkan::BufferHandle transforms;
	Vulkan::BufferHandle prev_transforms;
	Vulkan::BufferHandle aabbs;
	Scene *scene = nullptr;

	// Skinning and not skinned. One extra pipeline to deal with motion vectors.
	// Those are always rendered with EQUAL depth after prepass, so alpha testing is not needed.
	// Alpha blending cannot write motion vectors either.
	enum
	{
		// Skinned and non-skinned
		NumDrawTypesPerPipe = 2,

		// Reserve one extra for motion vectors (which is really just opaque).
		NumPipelines = int(DrawPipeline::Count) + 1,

		// For task + mesh path.
		NumDrawTypes = NumDrawTypesPerPipe * NumPipelines,

		// For MDI path.
		// First and Second phases support everything, but the MotionVector pass only does opaque (skinned, non-skinned).
		NumMDIDrawTypesPerPhase = NumDrawTypesPerPipe * int(DrawPipeline::Count),
		NumMDIDrawTypes = int(CullingPhase::MotionVector) * NumMDIDrawTypesPerPhase + NumDrawTypesPerPipe,
	};

	Vulkan::BufferHandle task_buffer;
	std::pair<uint32_t, uint32_t> task_offset_counts[NumDrawTypes] = {};

	Vulkan::BufferHandle mdi;
	MDICall mdi_calls[NumMDIDrawTypes] = {};

	struct PerContext
	{
		Vulkan::BufferHandle occlusions;
	};

	Util::SmallVector<PerContext> per_context_data;

	std::mutex sem_lock;
	Util::SmallVector<Vulkan::Semaphore> sems;

	void ensure_buffer(Vulkan::CommandBuffer &cmd, Vulkan::BufferHandle &buffer, VkDeviceSize size, const char *tag);
	void update_task_buffer(Vulkan::CommandBuffer &cmd);

public:
	std::pair<uint32_t, uint32_t> get_task_range(DrawPipeline pipe, bool skinned) const
	{
		return task_offset_counts[NumDrawTypesPerPipe * int(pipe) + skinned];
	}

	std::pair<uint32_t, uint32_t> get_task_range_motion_vector(bool skinned) const
	{
		return task_offset_counts[NumDrawTypes - 2 + skinned];
	}
};
}
