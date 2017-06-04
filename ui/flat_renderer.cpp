#include "flat_renderer.hpp"
#include "device.hpp"
#include "event.hpp"

using namespace Vulkan;
using namespace std;
using namespace Util;

namespace Granite
{
FlatRenderer::FlatRenderer()
{
	EventManager::get_global().register_latch_handler(DeviceCreatedEvent::type_id,
	                                                  &FlatRenderer::on_device_created,
	                                                  &FlatRenderer::on_device_destroyed,
	                                                  this);
}

void FlatRenderer::on_device_created(const Event &e)
{
	auto &created = e.as<DeviceCreatedEvent>();
	auto &device = created.get_device();
	suite[ecast(RenderableType::Sprite)].init_graphics(&device.get_shader_manager(), "assets://shaders/sprite.vert", "assets://shaders/sprite.frag");
	this->device = &device;
}

void FlatRenderer::on_device_destroyed(const Event &)
{
}

void FlatRenderer::begin()
{
	queue.reset();
	queue.set_shader_suites(suite);
}

void FlatRenderer::flush(Vulkan::CommandBuffer &cmd, const vec2 &camera_pos, const vec2 &camera_size)
{
	struct GlobalData
	{
		float inv_resolution[2];
		float pos_offset_pixels[2];
	};
	auto *global = static_cast<GlobalData *>(cmd.allocate_constant_data(0, 0, sizeof(GlobalData)));

	global->inv_resolution[0] = 1.0f / camera_size.x;
	global->inv_resolution[1] = 1.0f / camera_size.y;
	global->pos_offset_pixels[0] = -camera_pos.x;
	global->pos_offset_pixels[1] = -camera_pos.y;

	queue.sort();

	cmd.set_opaque_sprite_state();
	CommandBufferSavedState state;
	cmd.save_state(COMMAND_BUFFER_SAVED_SCISSOR_BIT | COMMAND_BUFFER_SAVED_VIEWPORT_BIT | COMMAND_BUFFER_SAVED_RENDER_STATE_BIT, state);
	queue.dispatch(Queue::Opaque, cmd, &state);

	cmd.set_transparent_sprite_state();
	cmd.save_state(COMMAND_BUFFER_SAVED_SCISSOR_BIT | COMMAND_BUFFER_SAVED_VIEWPORT_BIT | COMMAND_BUFFER_SAVED_RENDER_STATE_BIT, state);
	queue.dispatch(Queue::Transparent, cmd, &state);

	queue.reset();
}

void FlatRenderer::render_text(const Font &font, const char *text, const vec3 &offset, const vec2 &size,
                               Font::Alignment alignment, float scale)
{
	font.render_text(queue, text, offset, size, alignment, scale);
}

void FlatRenderer::push_sprite(const SpriteInfo &info)
{
	info.sprite->get_sprite_render_info(info.transform, queue);
}

void FlatRenderer::push_sprites(const SpriteList &visible)
{
	for (auto &vis : visible)
		vis.sprite->get_sprite_render_info(vis.transform, queue);
}

}