#pragma once

#include "vertical_packing.hpp"

namespace Granite
{
namespace UI
{
class Window : public VerticalPacking
{
public:
	Window();
	void set_title(const std::string &title);

	const std::string &get_title() const
	{
		return title;
	}

	void set_floating_position(vec2 pos)
	{
		position = pos;
		geometry_changed();
	}

	vec2 get_floating_position() const
	{
		return position;
	}

private:
	std::string title;
	vec2 position = vec2(0);

	float render(FlatRenderer &renderer, float layer, vec2 offset, vec2 size) override;
	void reconfigure() override;
};
}
}