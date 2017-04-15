#include "render_context.hpp"

using namespace std;
using namespace Vulkan;

namespace Granite
{
RenderContext::RenderContext()
{
	EventManager::get_global().register_latch_handler(DeviceCreatedEvent::type_id,
	                                                  &RenderContext::on_device_created,
	                                                  &RenderContext::on_device_destroyed,
	                                                  this);
}

void RenderContext::on_device_created(const Event &e)
{
	device = &e.as<DeviceCreatedEvent>().get_device();
}

void RenderContext::on_device_destroyed(const Event &)
{
}

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

	mat4 local_view = view;
	local_view[3].x = 0.0f;
	local_view[3].y = 0.0f;
	local_view[3].z = 0.0f;
	camera.inv_local_view_projection = inverse(projection * local_view);

	frustum.build_planes(camera.inv_view_projection);

	camera.camera_position = camera.inv_view[3].xyz();
	camera.camera_up = camera.inv_view[1].xyz();
	camera.camera_right = camera.inv_view[0].xyz();
	// Invert.
	camera.camera_front = -camera.inv_view[2].xyz();
}

}