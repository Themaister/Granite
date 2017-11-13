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

#include "deferred_lights.hpp"
#include "renderer.hpp"
#include <algorithm>

namespace Granite
{
void DeferredLights::refresh(RenderContext &context)
{
	visible.clear();
	scene->gather_visible_positional_lights(context.get_visibility_frustum(), visible);

	clips.clear();
	for (auto &cluster : clusters)
		cluster.clear();

	auto &params = context.get_render_parameters();

	// Lights which clip either near or far don't need double-sided testing.
	auto itr = std::remove_if(begin(visible), end(visible), [&params](const RenderableInfo &light) -> bool {
		auto &aabb = light.transform->world_aabb;
		float to_center = dot(aabb.get_center() - params.camera_position, params.camera_front);
		float radius = aabb.get_radius();
		float aabb_near = to_center - params.z_near - radius;
		float aabb_far = to_center + radius - params.z_far;
		return aabb_near < 0.0f || aabb_far > 0.0f;
	});

	clips.insert(end(clips), itr, end(visible));
	visible.erase(itr, end(visible));

	if (visible.empty())
		return;

	// Find Z-range of all lights.
	float cluster_min = FLT_MAX;
	float cluster_max = 0.0f;
	for (auto &light : visible)
	{
		auto &aabb = light.transform->world_aabb;
		float to_center = dot(aabb.get_center() - params.camera_position, params.camera_front);
		float radius = aabb.get_radius();
		float aabb_near = to_center - radius;
		float aabb_far = to_center + radius;
		cluster_min = min(aabb_near, cluster_min);
		cluster_max = max(aabb_far, cluster_max);
	}

	float cluster_range = cluster_max - cluster_min;
	float cluster_inv_range = float(NumClusters) / cluster_range;

	// Assign each renderable to a cluster index based on their position.
	for (auto &light : visible)
	{
		auto &aabb = light.transform->world_aabb;
		float to_center = dot(aabb.get_center() - params.camera_position, params.camera_front);
		int cluster_index = clamp(int((to_center - cluster_min) * cluster_inv_range), 0, NumClusters - 1);
		clusters[cluster_index].push_back(light);
	}
}

void DeferredLights::set_scene(Scene *scene)
{
	this->scene = scene;
}

void DeferredLights::set_renderers(Renderer *depth_renderer, Renderer *deferred_renderer)
{
	this->depth_renderer = depth_renderer;
	this->deferred_renderer = deferred_renderer;
}

void DeferredLights::render_prepass_lights(Vulkan::CommandBuffer &cmd, RenderContext &context)
{
	for (unsigned cluster = 0; cluster < NumClusters; cluster++)
	{
		depth_renderer->begin();
		depth_renderer->push_depth_renderables(context, clusters[cluster]);
		depth_renderer->set_stencil_reference(0xff, 1 << cluster, 1 << cluster);
		depth_renderer->flush(cmd, context,
		                      Renderer::NO_COLOR |
		                      Renderer::BACKFACE_BIT |
		                      Renderer::DEPTH_STENCIL_READ_ONLY |
		                      Renderer::STENCIL_WRITE_REFERENCE_BIT);
	}
}

void DeferredLights::render_lights(Vulkan::CommandBuffer &cmd, RenderContext &context)
{
	deferred_renderer->begin();
	deferred_renderer->push_renderables(context, clips);
	deferred_renderer->set_stencil_reference(1, 0, 0);
	deferred_renderer->flush(cmd, context, Renderer::STENCIL_COMPARE_REFERENCE_BIT);

	for (unsigned cluster = 0; cluster < NumClusters; cluster++)
	{
		deferred_renderer->begin();
		deferred_renderer->push_renderables(context, clusters[cluster]);
		deferred_renderer->set_stencil_reference((1 << cluster) | 1, 0, 1 << cluster);
		deferred_renderer->flush(cmd, context, Renderer::STENCIL_COMPARE_REFERENCE_BIT);
	}
}
}