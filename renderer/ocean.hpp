/* Copyright (c) 2017-2018 Hans-Kristian Arntzen
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

#include "abstract_renderable.hpp"
#include "scene.hpp"
#include "vulkan_events.hpp"

namespace Granite
{
class Ocean : public AbstractRenderable,
              public PerFrameRefreshable,
              public RenderPassCreator
{
public:
	Ocean();

private:
	bool has_static_aabb() const override
	{
		return false;
	}

	void get_render_info(const RenderContext &context,
	                     const CachedSpatialTransformComponent *transform,
	                     RenderQueue &queue) const override;

	vec2 size = vec2(1.0f);

	void refresh(RenderContext &context) override;

	void add_render_passes(RenderGraph &graph) override;
	void set_base_renderer(Renderer *forward_renderer,
	                       Renderer *deferred_renderer,
	                       Renderer *depth_renderer) override;
	void set_base_render_context(const RenderContext *context) override;
	void setup_render_pass_dependencies(RenderGraph &graph,
	                                    RenderPass &target) override;
	void setup_render_pass_resources(RenderGraph &graph) override;
	void set_scene(Scene *scene) override;
};
}