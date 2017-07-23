#pragma once

#include "abstract_renderable.hpp"
#include "texture_manager.hpp"
#include "flat_renderer.hpp"

namespace Granite
{

namespace RenderFunctions
{
void sprite_render(Vulkan::CommandBuffer &cmd, const RenderQueueData *infos, unsigned instances);
}

struct QuadData
{
	float pos_off_x, pos_off_y, pos_scale_x, pos_scale_y;
	float tex_off_x, tex_off_y, tex_scale_x, tex_scale_y;
	float rotation[4];
	uint8_t color[4];
	float layer;
};

struct SpriteInstanceInfo
{
	QuadData *quads;
	unsigned count;
};

struct SpriteRenderInfo
{
	const Vulkan::ImageView *texture = nullptr;
	Vulkan::Program *program = nullptr;
	Vulkan::StockSampler sampler;
	ivec4 clip_quad = ivec4(0, 0, 0x4000, 0x4000);
};

struct Sprite : AbstractRenderable
{
	DrawPipeline pipeline = DrawPipeline::Opaque;
	Vulkan::Texture *texture = nullptr;
	Vulkan::StockSampler sampler = Vulkan::StockSampler::LinearWrap;

	ivec2 tex_offset;
	ivec2 size;
	uint8_t color[4];

	void get_sprite_render_info(const SpriteTransformInfo &transform, RenderQueue &queue) const override;
	void get_render_info(const RenderContext &, const CachedSpatialTransformComponent *, RenderQueue &) const override
	{
	}

	DrawPipeline get_mesh_draw_pipeline() const override
	{
		return pipeline;
	}
};
}