#include "render_context.hpp"

using namespace std;
using namespace Vulkan;

namespace Granite
{
void RenderContext::set_camera(const Camera &camera)
{
	set_camera(camera.get_projection(), camera.get_view());
}

void RenderContext::set_camera(const mat4 &projection, const mat4 &view)
{
	camera.projection = projection;
	camera.view = view;
	camera.view_projection = projection * view;
	camera.inv_projection = inverse(projection);
	camera.inv_view = inverse(view);
	camera.inv_view_projection = inverse(camera.view_projection);
	frustum.build_planes(camera.inv_view_projection);
}

}