#include "sprite.hpp"
#include "device.hpp"
#include "render_context.hpp"
#include <string.h>

using namespace Util;

namespace Granite
{

namespace RenderFunctions
{
void line_strip_render(Vulkan::CommandBuffer &cmd, const RenderInfo **infos, unsigned instances)
{
	auto &info = *static_cast<const LineStripInfo *>(infos[0]);
	cmd.set_program(*info.program);

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
		count += static_cast<const LineStripInfo *>(infos[i])->count + 1;

	uint32_t *indices = static_cast<uint32_t *>(cmd.allocate_index_data(count * sizeof(uint32_t), VK_INDEX_TYPE_UINT32));
	vec3 *positions = static_cast<vec3 *>(cmd.allocate_vertex_data(0, sizeof(vec3) * count, sizeof(vec3)));
	vec4 *colors = static_cast<vec4 *>(cmd.allocate_vertex_data(1, sizeof(vec4) * count, sizeof(vec4)));
	cmd.set_vertex_attrib(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0);
	cmd.set_vertex_attrib(1, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0);

	unsigned index = 0;
	for (unsigned i = 0; i < instances; i++)
	{
		auto &info = *static_cast<const LineStripInfo *>(infos[i]);
		for (unsigned x = 0; x < info.count; x++)
		{
			*positions++ = info.positions[x];
			*colors++ = info.colors[x];
			*indices++ = index++;
		}
		*indices++ = 0xffffffffu;
	}

	cmd.draw_indexed(count);
}

void sprite_render(Vulkan::CommandBuffer &cmd, const RenderInfo **infos, unsigned num_instances)
{
	auto &info = *static_cast<const SpriteRenderInfo *>(infos[0]);
	cmd.set_program(*info.program);

	if (info.texture)
	{
		float inv_res[2] = {
			1.0f / info.texture->get_image().get_create_info().width,
			1.0f / info.texture->get_image().get_create_info().height,
		};
		cmd.push_constants(inv_res, 0, sizeof(inv_res));
		cmd.set_texture(2, 0, *info.texture, info.sampler);
	}

	VkRect2D sci;
	sci.offset.x = info.clip_quad.x;
	sci.offset.y = info.clip_quad.y;
	sci.extent.width = uint32_t(info.clip_quad.z);
	sci.extent.height = uint32_t(info.clip_quad.w);
	cmd.set_scissor(sci);

	cmd.set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
	auto *quad = static_cast<int8_t *>(cmd.allocate_vertex_data(0, 8, 2));
	quad[0] = -128;
	quad[1] = 127;
	quad[2] = 127;
	quad[3] = 127;
	quad[4] = -128;
	quad[5] = -128;
	quad[6] = 127;
	quad[7] = -128;

	unsigned quads = 0;
	for (unsigned i = 0; i < num_instances; i++)
		quads += static_cast<const SpriteRenderInfo *>(infos[i])->quad_count;

	auto *data = static_cast<SpriteRenderInfo::QuadData *>(
		cmd.allocate_vertex_data(1, quads * sizeof(SpriteRenderInfo::QuadData),
		                         sizeof(SpriteRenderInfo::QuadData), VK_VERTEX_INPUT_RATE_INSTANCE));

	quads = 0;
	for (unsigned i = 0; i < num_instances; i++)
	{
		auto &info = *static_cast<const SpriteRenderInfo *>(infos[i]);
		memcpy(data + quads, info.quads, info.quad_count * sizeof(*data));
		quads += info.quad_count;
	}
	cmd.set_vertex_attrib(0, 0, VK_FORMAT_R8G8_SNORM, 0);
	cmd.set_vertex_attrib(1, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(SpriteRenderInfo::QuadData, pos_off_x));
	cmd.set_vertex_attrib(2, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(SpriteRenderInfo::QuadData, tex_off_x));
	cmd.set_vertex_attrib(3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(SpriteRenderInfo::QuadData, rotation));
	cmd.set_vertex_attrib(4, 1, VK_FORMAT_R8G8B8A8_UNORM, offsetof(SpriteRenderInfo::QuadData, color));
	cmd.set_vertex_attrib(5, 1, VK_FORMAT_R32_SFLOAT, offsetof(SpriteRenderInfo::QuadData, layer));
	cmd.draw(4, quads);
}
}

void Sprite::get_sprite_render_info(const SpriteTransformInfo &transform, RenderQueue &queue) const
{
	auto queue_type = pipeline == DrawPipeline::AlphaBlend ? Queue::Transparent : Queue::Opaque;
	auto &sprite = queue.emplace<SpriteRenderInfo>(queue_type);

	sprite.program = queue.get_shader_suites()[ecast(RenderableType::Sprite)].get_program(pipeline,
	                                                                                      (MESH_ATTRIBUTE_POSITION_BIT | MESH_ATTRIBUTE_VERTEX_COLOR_BIT) |
		                                                                                      (texture ? MESH_ATTRIBUTE_UV_BIT : 0),
	                                                                                      texture ? MATERIAL_TEXTURE_BASE_COLOR_BIT : 0).get();
	if (texture)
		sprite.texture = &texture->get_image()->get_view();
	sprite.sampler = sampler;

	sprite.quads = static_cast<SpriteRenderInfo::QuadData *>(queue.allocate(sizeof(SpriteRenderInfo::QuadData),
	                                                                        alignof(SpriteRenderInfo::QuadData)));
	sprite.quad_count = 1;

	for (unsigned i = 0; i < 4; i++)
		sprite.quads->color[i] = color[i];

	sprite.quads->pos_off_x = transform.position.x;
	sprite.quads->pos_off_y = transform.position.y;
	sprite.quads->pos_scale_x = size.x * transform.scale.x;
	sprite.quads->pos_scale_y = size.y * transform.scale.y;
	sprite.quads->tex_off_x = tex_offset.x;
	sprite.quads->tex_off_y = tex_offset.y;
	sprite.quads->tex_scale_x = size.x;
	sprite.quads->tex_scale_y = size.y;
	sprite.quads->rotation[0] = transform.rotation[0].x;
	sprite.quads->rotation[1] = transform.rotation[0].y;
	sprite.quads->rotation[2] = transform.rotation[1].x;
	sprite.quads->rotation[3] = transform.rotation[1].y;
	sprite.quads->layer = transform.position.z;
	sprite.clip_quad = transform.clip;

	Util::Hasher hasher;
	hasher.pointer(texture);
	hasher.u32(ecast(sampler));
	hasher.u32(ecast(pipeline));
	hasher.s32(transform.clip.x);
	hasher.s32(transform.clip.y);
	hasher.s32(transform.clip.z);
	hasher.s32(transform.clip.w);
	sprite.instance_key = hasher.get();
	sprite.sorting_key = sprite.get_sprite_sort_key(queue_type, hasher.get(), transform.position.z);
}
}