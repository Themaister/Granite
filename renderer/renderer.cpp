#include "renderer.hpp"
#include "device.hpp"
#include "render_context.hpp"

using namespace Vulkan;

namespace Granite
{

Renderer::Renderer()
{
	EventManager::get_global().register_latch_handler(DeviceCreatedEvent::type_id,
                                                      &Renderer::on_device_created,
                                                      &Renderer::on_device_destroyed,
                                                      this);
}

void Renderer::on_device_created(const Event &e)
{
	auto &created = e.as<DeviceCreatedEvent>();
	auto &device = created.get_device();
	suite.init_graphics(&device.get_shader_manager(), "assets://shaders/static_mesh.vert", "assets://shaders/static_mesh.frag");
	this->device = &device;
}

void Renderer::on_device_destroyed(const Event &)
{
}

void Renderer::render(CommandBuffer &cmd, RenderContext &context, const VisibilityList &visible)
{
	auto *global = static_cast<RenderParameters *>(cmd.allocate_constant_data(0, 0, sizeof(RenderParameters)));
	*global = context.get_render_parameters();

	queue.reset();
	queue.set_shader_suite(&suite);
	for (auto &vis : visible)
		vis.renderable->get_render_info(context, vis.transform, queue);
	queue.sort();

	queue.dispatch(Queue::Opaque, cmd);
	queue.dispatch(Queue::Transparent, cmd);
}
}