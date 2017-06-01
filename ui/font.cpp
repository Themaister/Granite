#include "font.hpp"
#include <stdexcept>
#include "filesystem.hpp"
#include "device.hpp"
#include "sprite.hpp"
#include <string.h>

using namespace Vulkan;
using namespace std;
using namespace Util;

namespace Granite
{
Font::Font(const std::string &path, unsigned size)
{
	auto file = Filesystem::get().open(path, FileMode::ReadOnly);
	if (!file)
		throw runtime_error("Failed to open font.");

	auto *mapped = file->map();
	if (!mapped)
		throw runtime_error("Failed to map font.");

	unsigned multiplier = 4;
	bool success = false;

	do
	{
		width = size * multiplier;
		height = size * multiplier;
		bitmap.resize(width * height);
		int res = stbtt_BakeFontBitmap(static_cast<const unsigned char *>(mapped),
		                               0, size,
		                               bitmap.data(), width, height,
		                               32, 96,
		                               baked_chars);

		if (res <= 0)
			multiplier++;
		else
		{
			height = unsigned(res);
			success = true;
		}
	} while (!success && multiplier <= 32);

	if (!success)
		throw runtime_error("Failed to bake bitmap.");

	EventManager::get_global().register_latch_handler(DeviceCreatedEvent::type_id,
	                                                  &Font::on_device_created,
	                                                  &Font::on_device_destroyed,
	                                                  this);
}

void Font::render_text(RenderQueue &queue, const char *text, const vec3 &offset, const vec2 &size,
                       Alignment alignment, float scale)
{
	(void)size;
	(void)alignment;
	(void)scale;

	size_t len = strlen(text);
	auto &sprite = queue.emplace<SpriteRenderInfo>(Queue::Opaque);
	sprite.texture = view.get();
	sprite.clip_quad = uvec4(0, 0, ~0u, ~0u);

	static const uint32_t uv_mask = 1u << ecast(MeshAttribute::UV);
	static const uint32_t pos_mask = 1u << ecast(MeshAttribute::Position);
	static const uint32_t base_color_mask = 1u << ecast(Material::Textures::BaseColor);
	sprite.program = queue.get_shader_suites()[ecast(RenderableType::Sprite)].get_program(MeshDrawPipeline::AlphaTest,
                                                                                          uv_mask | pos_mask, base_color_mask).get();

	Hasher hasher;
	hasher.pointer(sprite.texture);
	sprite.sorting_key = RenderInfo::get_sprite_sort_key(Queue::Opaque, hasher.get(), offset.z);
	sprite.instance_key = hasher.get();

	sprite.quads = static_cast<SpriteRenderInfo::QuadData *>(queue.allocate(sizeof(SpriteRenderInfo::QuadData) * len,
	                                                                        alignof(SpriteRenderInfo::QuadData)));
	sprite.quad_count = 0;

	vec2 off = offset.xy();
	while (*text)
	{
		stbtt_aligned_quad q;
		if (*text >= 32)
		{
			stbtt_GetBakedQuad(baked_chars, width, height, *text - 32, &off.x, &off.y, &q, 1);

			auto &quad = sprite.quads[sprite.quad_count++];
			quad.color[0] = 255;
			quad.color[1] = 255;
			quad.color[2] = 255;
			quad.color[3] = 255;
			quad.rotation[0] = 1.0f;
			quad.rotation[1] = 0.0f;
			quad.rotation[2] = 0.0f;
			quad.rotation[3] = 1.0f;
			quad.layer = offset.z;
			quad.pos_off_x = q.x0;
			quad.pos_off_y = q.y0;
			quad.pos_scale_x = q.x1 - q.x0;
			quad.pos_scale_y = q.y1 - q.y0;
			quad.tex_off_x = q.s0 * width;
			quad.tex_off_y = q.t0 * height;
			quad.tex_scale_x = q.s1 * width - quad.tex_off_x;
			quad.tex_scale_y = q.t1 * height - quad.tex_off_y;
		}
		text++;
	}
}

void Font::on_device_created(const Event &e)
{
	auto &created = e.as<DeviceCreatedEvent>();
	auto &device = created.get_device();

	ImageCreateInfo info = ImageCreateInfo::immutable_2d_image(width, height, VK_FORMAT_R8_UNORM, false);
	ImageInitialData initial = {};
	initial.data = bitmap.data();
	texture = device.create_image(info, &initial);

	ImageViewCreateInfo view_info;
	view_info.image = texture.get();
	view_info.swizzle.r = VK_COMPONENT_SWIZZLE_ONE;
	view_info.swizzle.g = VK_COMPONENT_SWIZZLE_ONE;
	view_info.swizzle.b = VK_COMPONENT_SWIZZLE_ONE;
	view_info.swizzle.a = VK_COMPONENT_SWIZZLE_R;
	view_info.format = VK_FORMAT_R8_UNORM;
	view_info.levels = 1;
	view_info.layers = 1;
	view = device.create_image_view(view_info);
}

void Font::on_device_destroyed(const Event &)
{
	view.reset();
	texture.reset();
}

}