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

#include "threaded_scene.hpp"
#include "render_context.hpp"
#include <algorithm>

namespace Granite
{
namespace Threaded
{
void scene_gather_opaque_renderables(const Scene &scene, TaskComposer &composer, const Frustum &frustum,
                                     VisibilityList *lists, unsigned num_tasks)
{
	auto &group = composer.begin_pipeline_stage();
	for (unsigned i = 0; i < num_tasks; i++)
	{
		group.enqueue_task([&frustum, lists, &scene, i, num_tasks]() {
			scene.gather_visible_opaque_renderables_subset(frustum, lists[i], i, num_tasks);
		});
	}
}

void scene_gather_transparent_renderables(const Scene &scene, TaskComposer &composer, const Frustum &frustum,
                                          VisibilityList *lists, unsigned num_tasks)
{
	auto &group = composer.begin_pipeline_stage();
	for (unsigned i = 0; i < num_tasks; i++)
	{
		group.enqueue_task([&frustum, lists, &scene, i, num_tasks]() {
			scene.gather_visible_transparent_renderables_subset(frustum, lists[i], i, num_tasks);
		});
	}
}

void scene_gather_static_shadow_renderables(const Scene &scene, TaskComposer &composer, const Frustum &frustum,
                                            VisibilityList *lists, unsigned num_tasks)
{
	auto &group = composer.begin_pipeline_stage();
	for (unsigned i = 0; i < num_tasks; i++)
	{
		group.enqueue_task([&frustum, lists, &scene, i, num_tasks]() {
			scene.gather_visible_static_shadow_renderables_subset(frustum, lists[i], i, num_tasks);
		});
	}
}

void scene_gather_dynamic_shadow_renderables(const Scene &scene, TaskComposer &composer, const Frustum &frustum,
                                             VisibilityList *lists, unsigned num_tasks)
{
	auto &group = composer.begin_pipeline_stage();
	for (unsigned i = 0; i < num_tasks; i++)
	{
		group.enqueue_task([&frustum, lists, &scene, i, num_tasks]() {
			scene.gather_visible_dynamic_shadow_renderables_subset(frustum, lists[i], i, num_tasks);
		});
	}
}

void scene_gather_positional_light_renderables(const Scene &scene, TaskComposer &composer, const Frustum &frustum,
                                               VisibilityList *lists, unsigned num_tasks)
{
	auto &group = composer.begin_pipeline_stage();
	for (unsigned i = 0; i < num_tasks; i++)
	{
		group.enqueue_task([&frustum, lists, &scene, i, num_tasks]() {
			scene.gather_visible_positional_lights_subset(frustum, lists[i], i, num_tasks);
		});
	}
}

void scene_gather_positional_light_renderables_sorted(const Scene &scene, TaskComposer &composer,
                                                      const RenderContext &context,
                                                      PositionalLightList *lists, unsigned num_tasks)
{
	{
		auto &group = composer.begin_pipeline_stage();
		for (unsigned i = 0; i < num_tasks; i++)
		{
			group.enqueue_task([&context, lists, &scene, i, num_tasks]() {
				scene.gather_visible_positional_lights_subset(context.get_visibility_frustum(),
				                                              lists[i], i, num_tasks);
			});
		}
	}

	{
		auto &group = composer.begin_pipeline_stage();
		group.enqueue_task([&context, num_tasks, lists]() {
			size_t expected_size = 0;
			for (unsigned i = 0; i < num_tasks; i++)
				expected_size += lists[i].size();
			lists[0].reserve(expected_size);

			for (unsigned i = 1; i < num_tasks; i++)
				lists[0].insert(lists[0].end(), lists[i].begin(), lists[i].end());
			auto &lights = lists[0];

			// Prefer lights which are closest to the camera.
			std::sort(lights.begin(), lights.end(), [&context](const auto &a, const auto &b) -> bool {
				auto *transform_a = a.second;
				auto *transform_b = b.second;
				vec3 pos_a = transform_a->transform->world_transform[3].xyz();
				vec3 pos_b = transform_b->transform->world_transform[3].xyz();
				float dist_a = dot(pos_a, context.get_render_parameters().camera_front);
				float dist_b = dot(pos_b, context.get_render_parameters().camera_front);
				return dist_a < dist_b;
			});
		});
	}
}
}
}
