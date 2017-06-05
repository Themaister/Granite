#include "flat_renderer.hpp"
#include "device.hpp"
#include "event.hpp"
#include "sprite.hpp"

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
	suite[ecast(RenderableType::LineUI)].init_graphics(&device.get_shader_manager(), "assets://shaders/line_ui.vert", "assets://shaders/debug_mesh.frag");
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

void FlatRenderer::flush(Vulkan::CommandBuffer &cmd, const vec3 &camera_pos, const vec3 &camera_size)
{
	struct GlobalData
	{
		float inv_resolution[4];
		float pos_offset_pixels[4];
	};
	auto *global = static_cast<GlobalData *>(cmd.allocate_constant_data(0, 0, sizeof(GlobalData)));

	global->inv_resolution[0] = 1.0f / camera_size.x;
	global->inv_resolution[1] = 1.0f / camera_size.y;
	global->inv_resolution[2] = 1.0f / camera_size.z;
	global->inv_resolution[3] = 0.0f;
	global->pos_offset_pixels[0] = -camera_pos.x;
	global->pos_offset_pixels[1] = -camera_pos.y;
	global->pos_offset_pixels[2] = -camera_pos.z;
	global->pos_offset_pixels[3] = 0.0f;

	queue.sort();

	cmd.set_opaque_sprite_state();
	CommandBufferSavedState state;
	cmd.save_state(COMMAND_BUFFER_SAVED_SCISSOR_BIT | COMMAND_BUFFER_SAVED_VIEWPORT_BIT | COMMAND_BUFFER_SAVED_RENDER_STATE_BIT, state);
	queue.dispatch(Queue::Opaque, cmd, &state);

	cmd.set_transparent_sprite_state();
	cmd.save_state(COMMAND_BUFFER_SAVED_SCISSOR_BIT | COMMAND_BUFFER_SAVED_VIEWPORT_BIT | COMMAND_BUFFER_SAVED_RENDER_STATE_BIT, state);
	queue.dispatch(Queue::Transparent, cmd, &state);
}

void FlatRenderer::render_quad(const ImageView *view, Vulkan::StockSampler sampler,
                               const vec3 &offset, const vec2 &size, const vec2 &tex_offset, const vec2 &tex_size, const vec4 &color,
                               bool transparent)
{
	auto type = transparent ? Queue::Transparent : Queue::Opaque;
	auto pipeline = transparent ? DrawPipeline::AlphaBlend : DrawPipeline::Opaque;
	auto &sprite = queue.emplace<SpriteRenderInfo>(type);
	sprite.quad_count = 1;
	sprite.quads = static_cast<SpriteRenderInfo::QuadData *>(queue.allocate(sizeof(SpriteRenderInfo::QuadData), alignof(SpriteRenderInfo::QuadData)));

	Hasher h;
	h.pointer(sprite.program);
	h.u32(transparent);

	if (view)
	{
		sprite.texture = view;
		sprite.sampler = sampler;
		sprite.program = suite[ecast(RenderableType::Sprite)].get_program(pipeline,
		                                                                  MESH_ATTRIBUTE_POSITION_BIT |
		                                                                  MESH_ATTRIBUTE_VERTEX_COLOR_BIT |
		                                                                  MESH_ATTRIBUTE_UV_BIT,
		                                                                  MATERIAL_TEXTURE_BASE_COLOR_BIT).get();
		h.u64(view->get_cookie());
		h.u32(ecast(sampler));
	}
	else
		sprite.program = suite[ecast(RenderableType::Sprite)].get_program(pipeline,
		                                                                  MESH_ATTRIBUTE_POSITION_BIT |
		                                                                  MESH_ATTRIBUTE_VERTEX_COLOR_BIT, 0).get();

	sprite.instance_key = h.get();
	sprite.sorting_key = RenderInfo::get_sprite_sort_key(type, h.get(), offset.z);

	sprite.quads->layer = offset.z;
	sprite.quads->pos_off_x = offset.x;
	sprite.quads->pos_off_y = offset.y;
	sprite.quads->pos_scale_x = size.x;
	sprite.quads->pos_scale_y = size.y;
	sprite.quads->tex_off_x = tex_offset.x;
	sprite.quads->tex_off_y = tex_offset.y;
	sprite.quads->tex_scale_x = tex_size.x;
	sprite.quads->tex_scale_y = tex_size.y;
	quantize_color(sprite.quads->color, color);
	sprite.quads->rotation[0] = 1.0f;
	sprite.quads->rotation[1] = 0.0f;
	sprite.quads->rotation[2] = 0.0f;
	sprite.quads->rotation[3] = 1.0f;
}

void FlatRenderer::render_textured_quad(const ImageView &view, const vec3 &offset, const vec2 &size, const vec2 &tex_offset,
                                        const vec2 &tex_size, bool transparent, const vec4 &color, Vulkan::StockSampler sampler)
{
	render_quad(&view, sampler, offset, size, tex_offset, tex_size, color, transparent);
}

void FlatRenderer::render_quad(const vec3 &offset, const vec2 &size, const vec4 &color)
{
	render_quad(nullptr, Vulkan::StockSampler::Count, offset, size, vec2(0.0f), vec2(0.0f), color, color.a < 1.0f);
}

void FlatRenderer::render_line_strip(const vec2 *offset, float layer, unsigned count, const vec4 &color)
{
	auto transparent = color.a < 1.0f;
	auto &strip = queue.emplace<LineStripInfo>(transparent ? Queue::Transparent : Queue::Opaque);

	strip.program = suite[ecast(RenderableType::LineUI)].get_program(transparent ? DrawPipeline::AlphaBlend : DrawPipeline::Opaque,
	                                                                 MESH_ATTRIBUTE_POSITION_BIT | MESH_ATTRIBUTE_VERTEX_COLOR_BIT, 0).get();

	strip.render = RenderFunctions::line_strip_render;
	strip.count = count;

	Hasher h;
	h.u32(transparent);
	strip.instance_key = h.get();
	strip.sorting_key = RenderInfo::get_sprite_sort_key(transparent ? Queue::Transparent : Queue::Opaque, h.get(), layer);
	strip.positions = static_cast<vec3 *>(queue.allocate(sizeof(vec3) * count, alignof(vec3)));
	strip.colors = static_cast<vec4 *>(queue.allocate(sizeof(vec4) * count, alignof(vec4)));

	for (unsigned i = 0; i < count; i++)
		strip.colors[i] = color;
	for (unsigned i = 0; i < count; i++)
		strip.positions[i] = vec3(offset[i], layer);
}

void FlatRenderer::render_text(const Font &font, const char *text, const vec3 &offset, const vec2 &size, const vec4 &color,
                               Font::Alignment alignment, float scale)
{
	font.render_text(queue, text, offset, size, color, alignment, scale);
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