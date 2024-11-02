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
#include "lights/deferred_lights.hpp"

namespace Granite
{
enum SceneRendererFlagBits : uint32_t
{
	SCENE_RENDERER_FORWARD_OPAQUE_BIT = 1 << 0,
	SCENE_RENDERER_FORWARD_TRANSPARENT_BIT = 1 << 1,
	SCENE_RENDERER_FORWARD_Z_PREPASS_BIT = 1 << 2,
	SCENE_RENDERER_DEFERRED_GBUFFER_BIT = 1 << 3,
	SCENE_RENDERER_DEFERRED_GBUFFER_LIGHT_PREPASS_BIT = 1 << 4,
	SCENE_RENDERER_DEFERRED_LIGHTING_BIT = 1 << 5,
	SCENE_RENDERER_DEFERRED_CLUSTER_BIT = 1 << 6,
	SCENE_RENDERER_SHADOW_PCF_WIDE_BIT = 1 << 7,
	SCENE_RENDERER_SHADOW_VSM_BIT = 1 << 8,
	SCENE_RENDERER_DEPTH_BIT = 1 << 9,
	SCENE_RENDERER_DEPTH_STATIC_BIT = 1 << 10,
	SCENE_RENDERER_DEPTH_DYNAMIC_BIT = 1 << 11,
	SCENE_RENDERER_FORWARD_Z_EXISTING_PREPASS_BIT = 1 << 12,
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
		const RenderContext *context;
		const RendererSuite *suite;
		DeferredLights *deferred_lights;
		SceneRendererFlags flags;
	};
	void init(const Setup &setup);
	void set_clear_color(const VkClearColorValue &value);
	void set_extra_flush_flags(Renderer::RendererFlushFlags flags);

	void build_render_pass(Vulkan::CommandBuffer &cmd) const;
	void build_render_pass(Vulkan::CommandBuffer &cmd) override;
	bool get_clear_color(unsigned attachment, VkClearColorValue *value) const override;
	void enqueue_prepare_render_pass(RenderGraph &graph, TaskComposer &composer) override;

	// An immediate version of enqueue_prepare_render_pass.
	void prepare_render_pass();

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

	void build_render_pass_inner(Vulkan::CommandBuffer &cmd) const;
	void setup_debug_probes();
	void render_debug_probes(const Renderer &renderer, Vulkan::CommandBuffer &cmd, RenderQueue &queue,
	                         const RenderContext &context) const;
	AbstractRenderableHandle debug_probe_mesh;
	const ComponentGroupVector<VolumetricDiffuseLightComponent> *volumetric_diffuse_lights = nullptr;

	void prepare_setup_queues();
	void resolve_full_motion_vectors(Vulkan::CommandBuffer &cmd, const RenderContext &context) const;
};
}
