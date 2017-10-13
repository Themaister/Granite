/* Copyright (c) 2017 Hans-Kristian Arntzen
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

namespace Granite
{
class LightClusterer : public RenderPassCreator, public EventHandler, public PerFrameRefreshable
{
public:
	LightClusterer();

	void set_enable_shadows(bool enable);
	void set_enable_clustering(bool enable);

	void set_resolution(unsigned x, unsigned y, unsigned z);

	const Vulkan::ImageView *get_cluster_image() const;
	const Vulkan::ImageView *get_spot_light_shadows() const;
	const PositionalFragmentInfo *get_active_point_lights() const;
	const PositionalFragmentInfo *get_active_spot_lights() const;
	const mat4 *get_active_spot_light_shadow_matrices() const;
	unsigned get_active_point_light_count() const;
	unsigned get_active_spot_light_count() const;
	const mat4 &get_cluster_transform() const;

	enum { MaxLights = 32, ClusterHierarchies = 8, ClusterPrepassDownsample = 4 };

private:
	void add_render_passes(RenderGraph &graph) override;
	void set_base_renderer(Renderer *forward_renderer, Renderer *deferred_renderer, Renderer *depth_renderer) override;
	void set_base_render_context(const RenderContext *context) override;
	void setup_render_pass_dependencies(RenderGraph &graph, RenderPass &target) override;
	void setup_render_pass_resources(RenderGraph &graph) override;
	void set_scene(Scene *scene) override;
	void refresh(RenderContext &context) override;
	RendererType get_renderer_type() override;

	Scene *scene = nullptr;
	const RenderContext *context = nullptr;
	std::vector<std::tuple<PositionalLightComponent *, CachedSpatialTransformComponent *>> *lights = nullptr;

	unsigned x = 64, y = 32, z = 32;
	void build_cluster(Vulkan::CommandBuffer &cmd, Vulkan::ImageView &view, const Vulkan::ImageView *pre_culled);
	void on_device_created(const Vulkan::DeviceCreatedEvent &e);
	void on_device_destroyed(const Vulkan::DeviceCreatedEvent &e);
	Vulkan::ShaderProgram *program = nullptr;
	Vulkan::ImageView *target = nullptr;
	Vulkan::ImageView *pre_cull_target = nullptr;
	unsigned inherit_variant = 0;
	unsigned cull_variant = 0;

	PositionalFragmentInfo point_lights[MaxLights] = {};
	PositionalFragmentInfo spot_lights[MaxLights] = {};
	SpotLight *spot_light_handles[MaxLights] = {};
	mat4 spot_light_shadow_transforms[MaxLights] = {};
	mat4 cluster_transform;
	unsigned point_count = 0;
	unsigned spot_count = 0;

	Renderer *depth_renderer = nullptr;
	Vulkan::ImageHandle shadow_atlas;
	void render_atlas(RenderContext &context);

	bool enable_shadows = true;
	bool enable_clustering = true;
};
}