#pragma once

#include "widget.hpp"
#include "font.hpp"
#include "ui_manager.hpp"

namespace Granite
{
namespace UI
{
class Label : public Widget
{
public:
	Label(std::string text = "", FontSize font_size = FontSize::Normal);
	void set_text(std::string text);
	const std::string &get_text() const
	{
		return text;
	}

	void set_font_alignment(Font::Alignment alignment)
	{
		this->alignment = alignment;
		geometry_changed();
	}

	Font::Alignment get_font_alignment() const
	{
		return alignment;
	}

	void set_color(vec4 color)
	{
		this->color = color;
	}

	vec4 get_color() const
	{
		return color;
	}

private:
	std::string text;
	FontSize font_size;
	vec4 color = vec4(1.0f);
	Font::Alignment alignment = Font::Alignment::TopLeft;
	float render(FlatRenderer &renderer, float layer, vec2 offset, vec2 size) override;
	void reconfigure() override;
};
}
}