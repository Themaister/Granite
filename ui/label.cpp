#include "label.hpp"
#include "widget.hpp"

using namespace std;

namespace Granite
{
namespace UI
{
Label::Label(string text, FontSize font_size)
	: text(move(text)), font_size(font_size)
{
}

void Label::set_text(std::string text)
{
	this->text = move(text);
}

float Label::render(FlatRenderer &renderer, float layer, vec2 offset, vec2 size)
{
	auto &font = UIManager::get().get_font(font_size);
	renderer.render_text(font, text.c_str(), vec3(offset + geometry.margin, layer), size - 2.0f * geometry.margin,
	                     color, alignment);

	assert(children.empty());
	return layer;
}

void Label::reconfigure()
{
	auto &font = UIManager::get().get_font(font_size);
	vec2 minimum = font.get_text_geometry(text.c_str());

	geometry.minimum = minimum + 2.0f * geometry.margin;
}
}
}