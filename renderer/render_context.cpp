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

#include "render_context.hpp"
#include "muglm/matrix_helper.hpp"

using namespace std;
using namespace Vulkan;

namespace Granite
{
void RenderContext::set_camera(const Camera &camera_)
{
	set_camera(camera_.get_projection(), camera_.get_view());
}

void RenderContext::set_shadow_cascades(const mat4 cascades[NumShadowCascades])
{
	for (unsigned i = 0; i < NumShadowCascades; i++)
		camera.multiview_view_projection[i] = cascades[i];
}

void RenderContext::set_device(Device *device_)
{
	device = device_;
}

void RenderContext::set_camera(const mat4 &projection, const mat4 &view)
{
	camera.projection = projection;
	camera.view = view;
	camera.view_projection = camera.projection * view;
	camera.inv_projection = inverse(camera.projection);
	camera.inv_view = inverse(view);
	camera.inv_view_projection = inverse(camera.view_projection);

	mat4 local_view = view;
	local_view[3].x = 0.0f;
	local_view[3].y = 0.0f;
	local_view[3].z = 0.0f;
	camera.local_view_projection = camera.projection * local_view;
	camera.inv_local_view_projection = inverse(camera.local_view_projection);

	frustum.build_planes(camera.inv_view_projection);

	camera.camera_position = camera.inv_view[3].xyz();
	camera.camera_up = camera.inv_view[1].xyz();
	camera.camera_right = camera.inv_view[0].xyz();
	// Invert.
	camera.camera_front = -camera.inv_view[2].xyz();

	mat2 inv_zw(camera.inv_projection[2].zw(), camera.inv_projection[3].zw());
	const auto project = [](const vec2 &zw) -> float {
		return -zw.x / zw.y;
	};
	camera.z_near = project(inv_zw * vec2(0.0f, 1.0f));
	camera.z_far = project(inv_zw * vec2(1.0f, 1.0f));
}

}