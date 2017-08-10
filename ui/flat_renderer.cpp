/* Copyright (c) 2017 Hans-Kristian Arntzen
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
	EVENT_MANAGER_REGISTER_LATCH(FlatRenderer, on_device_created, on_device_destroyed, DeviceCreatedEvent);
	reset_scissor();
}

void FlatRenderer::reset_scissor()
{
	scissor_stack.clear();
	scissor_stack.push_back({ vec2(0), vec2(0x4000) });
}

void FlatRenderer::push_scissor(const vec2 &offset, const vec2 &size)
{
	scissor_stack.push_back({ offset, size });
}

void FlatRenderer::pop_scissor()
{
	assert(!scissor_stack.empty());
	scissor_stack.pop_back();
}

void FlatRenderer::on_device_created(const DeviceCreatedEvent &created)
{
	auto &device = created.get_device();
	suite[ecast(RenderableType::Sprite)].init_graphics(&device.get_shader_manager(), "builtin://shaders/sprite.vert", "builtin://shaders/sprite.frag");
	suite[ecast(RenderableType::LineUI)].init_graphics(&device.get_shader_manager(), "builtin://shaders/line_ui.vert", "builtin://shaders/debug_mesh.frag");

	for (auto &s : suite)
		s.bake_base_defines();

	this->device = &device;
}

void FlatRenderer::on_device_destroyed(const DeviceCreatedEvent &)
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
	queue.dispatch(Queue::OpaqueEmissive, cmd, &state);

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

	SpriteRenderInfo sprite;
	build_scissor(sprite.clip_quad, offset.xy(), offset.xy() + size);

	auto *quads = queue.allocate_one<QuadData>();
	auto *instance_data = queue.allocate_one<SpriteInstanceInfo>();
	instance_data->quads = quads;
	instance_data->count = 1;

	Hasher h;
	h.string("quad");
	h.u32(transparent);
	auto pipe_hash = h.get();
	h.s32(sprite.clip_quad.x);
	h.s32(sprite.clip_quad.y);
	h.s32(sprite.clip_quad.z);
	h.s32(sprite.clip_quad.w);

	if (view)
	{
		sprite.texture = view;
		sprite.sampler = sampler;
		h.u64(view->get_cookie());
		h.u32(ecast(sampler));
	}

	auto instance_key = h.get();
	auto sorting_key = RenderInfo::get_sprite_sort_key(type, pipe_hash, h.get(), offset.z);

	auto *sprite_data = queue.push<SpriteRenderInfo>(type, instance_key, sorting_key, RenderFunctions::sprite_render, instance_data);

	if (sprite_data)
	{
		if (view)
		{
			sprite.program = suite[ecast(RenderableType::Sprite)].get_program(pipeline,
			                                                                  MESH_ATTRIBUTE_POSITION_BIT |
			                                                                  MESH_ATTRIBUTE_VERTEX_COLOR_BIT |
			                                                                  MESH_ATTRIBUTE_UV_BIT,
			                                                                  MATERIAL_TEXTURE_BASE_COLOR_BIT).get();
		}
		else
		{
			sprite.program = suite[ecast(RenderableType::Sprite)].get_program(pipeline,
			                                                                  MESH_ATTRIBUTE_POSITION_BIT |
			                                                                  MESH_ATTRIBUTE_VERTEX_COLOR_BIT, 0).get();
		}
		*sprite_data = sprite;
	}

	quads->layer = offset.z;
	quads->pos_off_x = offset.x;
	quads->pos_off_y = offset.y;
	quads->pos_scale_x = size.x;
	quads->pos_scale_y = size.y;
	quads->tex_off_x = tex_offset.x;
	quads->tex_off_y = tex_offset.y;
	quads->tex_scale_x = tex_size.x;
	quads->tex_scale_y = tex_size.y;
	quantize_color(quads->color, color);
	quads->rotation[0] = 1.0f;
	quads->rotation[1] = 0.0f;
	quads->rotation[2] = 0.0f;
	quads->rotation[3] = 1.0f;
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

void FlatRenderer::build_scissor(ivec4 &clip, const vec2 &minimum, const vec2 &maximum) const
{
	auto &current = scissor_stack.back();
	bool scissor_invariant =
		all(lessThanEqual(current.offset, minimum)) &&
		all(greaterThanEqual(current.offset + current.size, maximum));

	if (scissor_invariant)
		clip = ivec4(0, 0, 0x4000, 0x4000);
	else
		clip = ivec4(current.offset, current.size);
}

void FlatRenderer::render_line_strip(const vec2 *offset, float layer, unsigned count, const vec4 &color)
{
	auto transparent = color.a < 1.0f;
	LineStripInfo strip;

	auto *lines = queue.allocate_one<LineInfo>();
	lines->count = count;
	lines->positions = queue.allocate_many<vec3>(count);
	lines->colors = queue.allocate_many<vec4>(count);

	vec2 minimum(FLT_MAX);
	vec2 maximum(-FLT_MAX);

	for (unsigned i = 0; i < count; i++)
		lines->colors[i] = color;

	for (unsigned i = 0; i < count; i++)
	{
		lines->positions[i] = vec3(offset[i], layer);
		minimum = min(minimum, offset[i]);
		maximum = max(maximum, offset[i]);
	}

	build_scissor(strip.clip, minimum, maximum);

	Hasher h;
	h.string("line");
	h.u32(transparent);
	auto pipe_hash = h.get();
	h.s32(strip.clip.x);
	h.s32(strip.clip.y);
	h.s32(strip.clip.z);
	h.s32(strip.clip.w);
	auto instance_key = h.get();
	auto sorting_key = RenderInfo::get_sprite_sort_key(transparent ? Queue::Transparent : Queue::Opaque, pipe_hash, h.get(), layer);

	LineStripInfo *strip_data = queue.push<LineStripInfo>(transparent ? Queue::Transparent : Queue::Opaque,
	                                                      instance_key, sorting_key, RenderFunctions::line_strip_render,
	                                                      lines);
	if (strip_data)
	{
		strip.program = suite[ecast(RenderableType::LineUI)].get_program(
			transparent ? DrawPipeline::AlphaBlend : DrawPipeline::Opaque,
			MESH_ATTRIBUTE_POSITION_BIT | MESH_ATTRIBUTE_VERTEX_COLOR_BIT, 0).get();

		*strip_data = strip;
	}
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
