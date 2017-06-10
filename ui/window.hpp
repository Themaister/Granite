#pragma once

#include "widget.hpp"

namespace Granite
{
namespace UI
{
class Window : public Widget
{
public:
	Window();
	void set_title(const std::string &title);

	const std::string &get_title() const
	{
		return title;
	}

	void set_position(ivec2 pos)
	{
		position = pos;
		geometry_changed();
	}

	ivec2 get_position() const
	{
		return position;
	}

private:
	std::string title;
	ivec2 position = ivec2(0);

	float render(FlatRenderer &renderer, float layer, ivec2 offset, ivec2 size) override;
	void reconfigure() override;
};
}
}