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

#include "sprite.hpp"
#include "device.hpp"
#include "render_context.hpp"
#include <string.h>

using namespace Util;

namespace Granite
{

namespace RenderFunctions
{
void line_strip_render(Vulkan::CommandBuffer &cmd, const RenderQueueData *infos, unsigned instances)
{
	auto &info = *static_cast<const LineStripInfo *>(infos[0].render_info);
	cmd.set_program(info.program);

	cmd.set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_LINE_STRIP);
	cmd.set_primitive_restart(true);

	VkRect2D sci;
	sci.offset.x = info.clip.x;
	sci.offset.y = info.clip.y;
	sci.extent.width = uint32_t(info.clip.z);
	sci.extent.height = uint32_t(info.clip.w);
	cmd.set_scissor(sci);

	unsigned count = 0;
	for (unsigned i = 0; i < instances; i++)
		count += static_cast<const LineInfo *>(infos[i].instance_data)->count + 1;

	uint32_t *indices = static_cast<uint32_t *>(cmd.allocate_index_data(count * sizeof(uint32_t), VK_INDEX_TYPE_UINT32));
	vec3 *positions = static_cast<vec3 *>(cmd.allocate_vertex_data(0, sizeof(vec3) * count, sizeof(vec3)));
	vec4 *colors = static_cast<vec4 *>(cmd.allocate_vertex_data(1, sizeof(vec4) * count, sizeof(vec4)));
	cmd.set_vertex_attrib(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0);
	cmd.set_vertex_attrib(1, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0);

	unsigned index = 0;
	for (unsigned i = 0; i < instances; i++)
	{
		auto &instance_info = *static_cast<const LineInfo *>(infos[i].instance_data);
		for (unsigned x = 0; x < instance_info.count; x++)
		{
			*positions++ = instance_info.positions[x];
			*colors++ = instance_info.colors[x];
			*indices++ = index++;
		}
		*indices++ = 0xffffffffu;
	}

	cmd.draw_indexed(count);
}

void sprite_render(Vulkan::CommandBuffer &cmd, const RenderQueueData *infos, unsigned num_instances)
{
	auto &info = *static_cast<const SpriteRenderInfo *>(infos->render_info);
	cmd.set_program(info.program);

	if (info.textures[0])
	{
		struct Push
		{
			alignas(8) vec2 resolution;
			alignas(8) vec2 inv_resolution;
		} push;

		push.resolution.x = info.textures[0]->get_image().get_width();
		push.resolution.y = info.textures[0]->get_image().get_height();
		push.inv_resolution = 1.0f / push.resolution;

		*cmd.allocate_typed_constant_data<Push>(3, 0, 1) = push;

		cmd.set_texture(2, 0, *info.textures[0], info.sampler);
		if (info.textures[1])
			cmd.set_texture(2, 1, *info.textures[1], info.sampler);
	}

	VkRect2D sci;
	sci.offset.x = info.clip_quad.x;
	sci.offset.y = info.clip_quad.y;
	sci.extent.width = uint32_t(info.clip_quad.z);
	sci.extent.height = uint32_t(info.clip_quad.w);
	cmd.set_scissor(sci);

	cmd.set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
	Vulkan::CommandBufferUtil::set_quad_vertex_state(cmd);

	unsigned quads = 0;
	for (unsigned i = 0; i < num_instances; i++)
		quads += static_cast<const SpriteInstanceInfo *>(infos[i].instance_data)->count;

	auto *data = static_cast<QuadData *>(
		cmd.allocate_vertex_data(1, quads * sizeof(QuadData),
		                         sizeof(QuadData), VK_VERTEX_INPUT_RATE_INSTANCE));

	quads = 0;
	for (unsigned i = 0; i < num_instances; i++)
	{
		auto &instance_info = *static_cast<const SpriteInstanceInfo *>(infos[i].instance_data);
		memcpy(data + quads, instance_info.quads, instance_info.count * sizeof(*data));
		quads += instance_info.count;
	}

	cmd.set_vertex_attrib(1, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(QuadData, pos_off_x));
	cmd.set_vertex_attrib(2, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(QuadData, tex_off_x));
	cmd.set_vertex_attrib(3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(QuadData, rotation));
	cmd.set_vertex_attrib(4, 1, VK_FORMAT_R8G8B8A8_UNORM, offsetof(QuadData, color));
	cmd.set_vertex_attrib(5, 1, VK_FORMAT_R32_SFLOAT, offsetof(QuadData, layer));
	if (info.textures[1])
		cmd.set_vertex_attrib(6, 1, VK_FORMAT_R32_SFLOAT, offsetof(QuadData, blend_factor));
	cmd.set_vertex_attrib(7, 1, VK_FORMAT_R32_SFLOAT, offsetof(QuadData, array_layer));
	cmd.draw(4, quads);
}
}

void Sprite::get_sprite_render_info(const SpriteTransformInfo &transform, RenderQueue &queue) const
{
	bool transparent = pipeline == DrawPipeline::AlphaBlend;
	auto queue_type = transparent ? Queue::Transparent : Queue::Opaque;
	SpriteRenderInfo sprite;

	if (texture)
		sprite.textures[0] = &texture->get_image()->get_view();
	if (texture_alt)
		sprite.textures[1] = &texture_alt->get_image()->get_view();
	sprite.sampler = sampler;

	auto *instance_data = queue.allocate_one<SpriteInstanceInfo>();
	auto *quads = queue.allocate_one<QuadData>();
	instance_data->quads = quads;
	instance_data->count = 1;

	for (unsigned i = 0; i < 4; i++)
		quads->color[i] = color[i];

	quads->pos_off_x = transform.position.x;
	quads->pos_off_y = transform.position.y;
	quads->pos_scale_x = size.x * transform.scale.x;
	quads->pos_scale_y = size.y * transform.scale.y;
	quads->tex_off_x = tex_offset.x;
	quads->tex_off_y = tex_offset.y;
	quads->tex_scale_x = size.x;
	quads->tex_scale_y = size.y;
	quads->rotation[0] = transform.rotation[0].x;
	quads->rotation[1] = transform.rotation[0].y;
	quads->rotation[2] = transform.rotation[1].x;
	quads->rotation[3] = transform.rotation[1].y;
	quads->layer = transform.position.z;
	quads->blend_factor = texture_blending_factor;
	sprite.clip_quad = transform.clip;

	Util::Hasher hasher;
	hasher.s32(transparent);
	hasher.s32(bandlimited_pixel);
	hasher.s32(luma_to_alpha);
	hasher.s32(clear_alpha_to_zero);
	hasher.s32(texture_alt ? 1 : 0);
	auto pipe_hash = hasher.get();

	hasher.pointer(texture);
	hasher.pointer(texture_alt);
	hasher.s32(ecast(sampler));
	hasher.s32(ecast(pipeline));
	hasher.s32(transform.clip.x);
	hasher.s32(transform.clip.y);
	hasher.s32(transform.clip.z);
	hasher.s32(transform.clip.w);
	auto instance_key = hasher.get();
	auto sorting_key = RenderInfo::get_sprite_sort_key(queue_type, pipe_hash, hasher.get(), transform.position.z);

	auto *sprite_data = queue.push<SpriteRenderInfo>(queue_type, instance_key, sorting_key,
	                                                 RenderFunctions::sprite_render,
	                                                 instance_data);

	if (sprite_data)
	{
		auto &suite = queue.get_shader_suites()[ecast(RenderableType::Sprite)];

		uint32_t shader_flags = 0;
		if (bandlimited_pixel)
			shader_flags |= BANDLIMITED_PIXEL_BIT;
		if (sprite.textures[1])
			shader_flags |= BLEND_TEXUTRE_BIT;
		if (luma_to_alpha)
			shader_flags |= LUMA_TO_ALPHA_BIT;
		if (clear_alpha_to_zero)
			shader_flags |= CLEAR_ALPHA_TO_ZERO_BIT;

		sprite.program = suite.get_program(pipeline,
		                                   MESH_ATTRIBUTE_POSITION_BIT |
		                                   MESH_ATTRIBUTE_VERTEX_COLOR_BIT |
		                                   (texture ? MESH_ATTRIBUTE_UV_BIT : 0),
		                                   texture ? MATERIAL_TEXTURE_BASE_COLOR_BIT : 0,
		                                   shader_flags);
		*sprite_data = sprite;
	}
}
}
