/* Copyright (c) 2017-2024 Hans-Kristian Arntzen
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

#pragma once

#include "event.hpp"
#include "render_queue.hpp"
#include "renderer.hpp"
#include <memory>

namespace Granite
{
class Font : public EventHandler
{
public:
	Font(const std::string &path, unsigned size);
	~Font();

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
	                 const vec2 &clip_offset, const vec2 &clip_size,
	                 const vec4 &color,
	                 Alignment alignment = Alignment::TopLeft) const;

	vec2 get_text_geometry(const char *text) const;

	vec2 get_aligned_offset(Alignment alignment, vec2 text_geometry, vec2 target_geometry) const;

private:
	Vulkan::ImageHandle texture;
	struct Baked;
	std::unique_ptr<Baked> baked_chars;
	void on_device_created(const Vulkan::DeviceCreatedEvent &e);
	void on_device_destroyed(const Vulkan::DeviceCreatedEvent &e);

	std::vector<uint8_t> bitmap;
	unsigned width = 0, height = 0;
	unsigned font_height = 0;
};
}