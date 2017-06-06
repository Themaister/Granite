#include "window.hpp"
#include "flat_renderer.hpp"

namespace Granite
{
namespace UI
{
void Window::set_title(const std::string &title)
{
	this->title = title;
	geometry_changed();
}

void Window::render(FlatRenderer &renderer, float layer, ivec2 offset, ivec2 size)
{
	render_children(renderer, layer, offset);
	renderer.render_quad(vec3(offset, layer), size, bg_color);
}

void Window::reconfigure()
{
}
}
}