#include "window.hpp"
#include "flat_renderer.hpp"
#include "ui_manager.hpp"

namespace Granite
{
namespace UI
{
Window::Window()
{
	bg_color = vec4(1.0f);
}

void Window::set_title(const std::string &title)
{
	this->title = title;
	geometry_changed();
}

float Window::render(FlatRenderer &renderer, float layer, ivec2 offset, ivec2 size)
{
	float ret = render_children(renderer, layer, offset);

	if (bg_color.a > 0.0f)
		renderer.render_quad(vec3(offset, layer), size, bg_color);

	auto &font = UIManager::get().get_font(FontSize::Large);

	vec2 text_geom = font.get_text_geometry(title.c_str());
	vec2 text_offset = font.get_aligned_offset(Font::Alignment::TopCenter, text_geom, size);
	float line_y = offset.y + text_geom.y + text_offset.y + 5.0f;
	vec2 offsets[] = {
		{ offset.x + 2.0f, line_y },
		{ offset.x + size.x - 2.0f, line_y },
	};
	renderer.render_line_strip(offsets, layer - 0.5f, 2, vec4(0.0f, 0.0f, 0.0f, 1.0f));
	offsets[0].y += 2.0f;
	offsets[1].y += 2.0f;
	renderer.render_line_strip(offsets, layer - 0.5f, 2, vec4(0.0f, 0.0f, 0.0f, 1.0f));

	renderer.render_text(font, title.c_str(),
	                     vec3(offset, layer - 0.5f), size, vec4(0.0f, 0.0f, 0.0f, 1.0f), Font::Alignment::TopCenter);
	return std::min(ret, layer - 0.5f);
}

void Window::reconfigure()
{
}
}
}