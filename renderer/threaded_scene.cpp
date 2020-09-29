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
}
}
