#include "renderer.hpp"
#include "device.hpp"
#include "render_context.hpp"

using namespace Vulkan;
using namespace Util;

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
	suite[ecast(RenderableType::Mesh)].init_graphics(&device.get_shader_manager(), "assets://shaders/static_mesh.vert", "assets://shaders/static_mesh.frag");
	suite[ecast(RenderableType::Skybox)].init_graphics(&device.get_shader_manager(), "assets://shaders/skybox.vert", "assets://shaders/skybox.frag");
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
	queue.set_shader_suites(suite);
	for (auto &vis : visible)
		vis.renderable->get_render_info(context, vis.transform, queue);
	queue.sort();

	cmd.set_opaque_state();

	CommandBufferSavedState state;
	cmd.save_state(COMMAND_BUFFER_SAVED_SCISSOR_BIT | COMMAND_BUFFER_SAVED_VIEWPORT_BIT | COMMAND_BUFFER_SAVED_RENDER_STATE_BIT, state);
	queue.dispatch(Queue::Opaque, cmd);
	queue.dispatch(Queue::Transparent, cmd);
}
}