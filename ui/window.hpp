#pragma once

#include "widget.hpp"

namespace Granite
{
namespace UI
{
class Window : public Widget
{
public:
	void set_title(const std::string &title);

	const std::string &get_title() const
	{
		return title;
	}

private:
	std::string title;

	void render(FlatRenderer &renderer, float layer, ivec2 offset, ivec2 size) override;
	void reconfigure() override;
};
}
}