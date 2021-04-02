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

#pragma once

#include "event.hpp"
#include "image.hpp"
#include "application_wsi_events.hpp"
#include "math.hpp"
#include "aabb.hpp"
#include "render_components.hpp"
#include "thread_group.hpp"

namespace Granite
{
class VolumetricDiffuseLightManager : public EventHandler, public PerFrameRefreshable, public RenderPassCreator
{
public:
	VolumetricDiffuseLightManager();

private:
	void refresh(const RenderContext &context_, TaskComposer &composer) override;
	const ComponentGroupVector<VolumetricDiffuseLightComponent> *volumetric_diffuse = nullptr;
	Scene *scene = nullptr;
	const RendererSuite *suite = nullptr;
	const RenderContext *base_render_context = nullptr;

	TaskGroupHandle create_probe_gbuffer(TaskComposer &composer, TaskGroup &incoming,
	                                     const RenderContext &context,
	                                     VolumetricDiffuseLightComponent &light);

	void light_probe_buffer(Vulkan::CommandBuffer &cmd,
	                        VolumetricDiffuseLightComponent &light);

	void set_base_renderer(const RendererSuite *suite) override;
	void set_base_render_context(const RenderContext *context) override;
	void set_scene(Scene *scene) override;
	void add_render_passes(RenderGraph &graph) override;
	void setup_render_pass_dependencies(RenderGraph &graph, RenderPass &target) override;
	void setup_render_pass_resources(RenderGraph &graph) override;

	mat4 inv_view_projections[6];
};
}