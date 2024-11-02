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

#include "render_components.hpp"
#include "scene.hpp"

namespace Granite
{
const mat4 &RenderInfoComponent::get_world_transform() const
{
	assert(scene_node->transform.count);
	return scene_node->parent_scene.get_transforms().get_cached_transforms()[scene_node->transform.offset];
}

const mat4 &RenderInfoComponent::get_prev_world_transform() const
{
	assert(scene_node->transform.count);
	return scene_node->parent_scene.get_transforms().get_cached_prev_transforms()[scene_node->transform.offset];
}

const AABB &RenderInfoComponent::get_aabb() const
{
	assert(aabb.count);
	return scene_node->parent_scene.get_aabbs().get_aabbs()[aabb.offset];
}

RenderInfoComponent::~RenderInfoComponent()
{
	if (aabb.count)
		scene_node->parent_scene.get_aabbs().free(aabb);
	if (occluder_state.count)
		scene_node->parent_scene.get_occluder_states().free(occluder_state);
}
}
