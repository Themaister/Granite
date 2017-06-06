#pragma once

#include "event.hpp"
#include "widget.hpp"

namespace Granite
{
namespace UI
{
class UIManager : public EventHandler
{
public:
	static UIManager &get();

	bool filter_input_event(const Event &e);


private:
	UIManager();
};
}
}