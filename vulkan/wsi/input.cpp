#include "input.hpp"
#include "vulkan_events.hpp"
#include "event.hpp"

using namespace Util;
using namespace Granite;

namespace Vulkan
{
void InputTracker::key_event(Key key, KeyState state)
{
	if (state == KeyState::Released)
		key_state &= ~(1ull << ecast(key));
	else if (state == KeyState::Pressed)
		key_state |= 1ull << ecast(key);

	EventManager::get_global().dispatch_inline(KeyboardEvent{key, state});
}

void InputTracker::mouse_button_event(MouseButton button, bool pressed)
{
	if (pressed)
		mouse_button_state |= 1ull << ecast(button);
	else
		mouse_button_state &= ~(1ull << ecast(button));

	EventManager::get_global().dispatch_inline(MouseButtonEvent{button, pressed});
}

void InputTracker::mouse_move_event(double x, double y)
{
	if (mouse_active)
	{
		double delta_x = x - last_mouse_x;
		double delta_y = y - last_mouse_y;
		last_mouse_x = x;
		last_mouse_y = y;
		EventManager::get_global().dispatch_inline(
			MouseMoveEvent{delta_x, delta_y, x, y, key_state, mouse_button_state});
	}
}

void InputTracker::mouse_enter(double x, double y)
{
	mouse_active = true;
	last_mouse_x = x;
	last_mouse_y = y;
}

void InputTracker::mouse_leave()
{
	mouse_active = false;
}

void InputTracker::dispatch_current_state(double delta_time)
{
	EventManager::get_global().dispatch_inline(InputStateEvent{last_mouse_x, last_mouse_y, delta_time, key_state, mouse_button_state, mouse_active});
}

}