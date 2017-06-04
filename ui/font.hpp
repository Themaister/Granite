#pragma once

#include "stb_truetype.h"
#include "renderer.hpp"
#include "render_queue.hpp"
#include "event.hpp"

namespace Granite
{
class Font : public EventHandler
{
public:
	Font(const std::string &path, unsigned size);

	enum class Alignment
	{
		TopLeft,
		TopRight,
		TopCenter,
		CenterLeft,
		CenterRight,
		Center,
		BottomLeft,
		BottomRight,
		BottomCenter
	};

	void render_text(RenderQueue &queue, const char *text,
	                 const vec3 &offset, const vec2 &size,
	                 const vec4 &color,
	                 Alignment alignment = Alignment::TopLeft, float scale = 1.0f) const;

private:
	Vulkan::ImageHandle texture;
	Vulkan::ImageViewHandle view;
	stbtt_bakedchar baked_chars[128 - 32];
	void on_device_created(const Event &e);
	void on_device_destroyed(const Event &e);

	std::vector<uint8_t> bitmap;
	unsigned width = 0, height = 0;
	unsigned font_height = 0;
};
}