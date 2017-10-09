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
class LightClusterer : public RenderPassCreator, public EventHandler
{
public:
	LightClusterer();

	void set_resolution(unsigned x, unsigned y, unsigned z);

	const Vulkan::ImageView &get_cluster_image() const;
	const PositionalFragmentInfo *get_active_point_lights() const;
	const PositionalFragmentInfo *get_active_spot_lights() const;
	unsigned get_active_point_light_count() const;
	unsigned get_active_spot_light_count() const;
	const mat4 &get_cluster_transform() const;

	enum { MaxLights = 32 };

private:
	void add_render_passes(RenderGraph &graph) override;
	void set_base_renderer(Renderer *renderer) override;
	void set_base_render_context(const RenderContext *context) override;
	void setup_render_pass_dependencies(RenderGraph &graph, RenderPass &target) override;
	void setup_render_pass_resources(RenderGraph &graph) override;
	void set_scene(Scene *scene) override;
	RendererType get_renderer_type() override;

	Scene *scene = nullptr;
	const RenderContext *context = nullptr;
	std::vector<std::tuple<PositionalLightComponent *, CachedSpatialTransformComponent *>> *lights = nullptr;

	unsigned x = 64, y = 64, z = 64;
	void build_cluster(Vulkan::CommandBuffer &cmd, Vulkan::ImageView &view);
	void on_device_created(const Vulkan::DeviceCreatedEvent &e);
	void on_device_destroyed(const Vulkan::DeviceCreatedEvent &e);
	Vulkan::ShaderProgram *program = nullptr;
	Vulkan::ImageView *target = nullptr;
	unsigned variant = 0;

	PositionalFragmentInfo point_lights[MaxLights] = {};
	PositionalFragmentInfo spot_lights[MaxLights] = {};
	mat4 cluster_transform;
	unsigned point_count = 0;
	unsigned spot_count = 0;
};
}