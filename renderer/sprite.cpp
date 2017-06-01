#include "sprite.hpp"
#include "device.hpp"
#include "render_context.hpp"
#include <string.h>

using namespace Util;

namespace Granite
{
namespace RenderFunctions
{
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
		cmd.set_texture(2, 0, *info.texture, Vulkan::StockSampler::LinearWrap);
	}

	VkRect2D sci;
	sci.offset.x = glm::max<uint32_t>(cmd.get_viewport().x, info.clip_quad.x);
	sci.offset.y = glm::max<uint32_t>(cmd.get_viewport().y, info.clip_quad.y);
	uint32_t end_x = glm::min<uint32_t>(cmd.get_viewport().x + cmd.get_viewport().width, info.clip_quad.x + info.clip_quad.z);
	uint32_t end_y = glm::min<uint32_t>(cmd.get_viewport().y + cmd.get_viewport().height, info.clip_quad.y + info.clip_quad.w);
	sci.extent.width = end_x - sci.offset.x;
	sci.extent.height = end_y - sci.offset.y;
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
	auto queue_type = pipeline == MeshDrawPipeline::AlphaBlend ? Queue::Transparent : Queue::Opaque;
	auto &sprite = queue.emplace<SpriteRenderInfo>(queue_type);

	static const uint32_t uv_mask = 1u << ecast(MeshAttribute::UV);
	static const uint32_t pos_mask = 1u << ecast(MeshAttribute::Position);
	static const uint32_t base_color_mask = 1u << ecast(Material::Textures::BaseColor);

	sprite.program = queue.get_shader_suites()[ecast(RenderableType::Sprite)].get_program(pipeline,
	                                                                                    texture ? (uv_mask | pos_mask) : pos_mask,
	                                                                                    texture ? base_color_mask : 0).get();
	if (texture)
		sprite.texture = &texture->get_image()->get_view();

	sprite.quads = static_cast<SpriteRenderInfo::QuadData *>(queue.allocate(sizeof(SpriteRenderInfo::QuadData)));
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

	sprite.render = RenderFunctions::sprite_render;

	Util::Hasher hasher;
	hasher.pointer(texture);
	hasher.u32(ecast(pipeline));
	hasher.u32(transform.clip.x);
	hasher.u32(transform.clip.y);
	hasher.u32(transform.clip.z);
	hasher.u32(transform.clip.w);
	sprite.instance_key = hasher.get();
	sprite.sorting_key = sprite.get_sprite_sort_key(queue_type, hasher.get(), transform.position.z);
}
}