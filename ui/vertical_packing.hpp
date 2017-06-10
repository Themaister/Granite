#pragma once

#include "widget.hpp"

namespace Granite
{
namespace UI
{
class VerticalPacking : public Widget
{
public:
protected:
	float render(FlatRenderer &renderer, float layer, vec2 offset, vec2 size) override;
	void reconfigure() override;
};
}
}