/* Copyright (c) 2017-2023 Hans-Kristian Arntzen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "input_sdl.hpp"

namespace Granite
{
static JoypadKey sdl_gamepad_button_to_granite(SDL_GamepadButton button)
{
	switch (button)
	{
	case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
		return JoypadKey::Down;
	case SDL_GAMEPAD_BUTTON_DPAD_UP:
		return JoypadKey::Up;
	case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
		return JoypadKey::Left;
	case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
		return JoypadKey::Right;
	case SDL_GAMEPAD_BUTTON_GUIDE:
		return JoypadKey::Mode;
	case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
		return JoypadKey::LeftShoulder;
	case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
		return JoypadKey::RightShoulder;
	case SDL_GAMEPAD_BUTTON_WEST:
		return JoypadKey::West;
	case SDL_GAMEPAD_BUTTON_EAST:
		return JoypadKey::East;
	case SDL_GAMEPAD_BUTTON_NORTH:
		return JoypadKey::North;
	case SDL_GAMEPAD_BUTTON_SOUTH:
		return JoypadKey::South;
	case SDL_GAMEPAD_BUTTON_START:
		return JoypadKey::Start;
	case SDL_GAMEPAD_BUTTON_BACK:
		return JoypadKey::Select;
	case SDL_GAMEPAD_BUTTON_LEFT_STICK:
		return JoypadKey::LeftThumb;
	case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
		return JoypadKey::RightThumb;
	default:
		return JoypadKey::Unknown;
	}
}

static JoypadAxis sdl_gamepad_axis_to_granite(SDL_GamepadAxis axis)
{
	switch (axis)
	{
	case SDL_GAMEPAD_AXIS_LEFTX:
		return JoypadAxis::LeftX;
	case SDL_GAMEPAD_AXIS_LEFTY:
		return JoypadAxis::LeftY;
	case SDL_GAMEPAD_AXIS_RIGHTX:
		return JoypadAxis::RightX;
	case SDL_GAMEPAD_AXIS_RIGHTY:
		return JoypadAxis::RightY;
	case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
		return JoypadAxis::LeftTrigger;
	case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
		return JoypadAxis::RightTrigger;
	default:
		return JoypadAxis::Unknown;
	}
}

bool InputTrackerSDL::init(InputTracker &tracker, const Dispatcher &dispatcher)
{
	// Open existing gamepads.
	int num_pads = 0;
	SDL_JoystickID *gamepad_ids = SDL_GetGamepads(&num_pads);
	for (int i = 0; i < num_pads; i++)
		add_gamepad(gamepad_ids[i], tracker, dispatcher);
	if (gamepad_ids)
		SDL_free(gamepad_ids);
	SDL_SetGamepadEventsEnabled(SDL_TRUE);
	return true;
}

void InputTrackerSDL::close()
{
	for (auto *pad : pads)
		if (pad)
			SDL_CloseGamepad(pad);
}

bool InputTrackerSDL::process_sdl_event(const SDL_Event &e, InputTracker &tracker,
                                        const InputTrackerSDL::Dispatcher &dispatcher)
{
	switch (e.type)
	{
	case SDL_EVENT_GAMEPAD_ADDED:
	{
		add_gamepad(e.gdevice.which, tracker, dispatcher);
		return true;
	}

	case SDL_EVENT_GAMEPAD_REMOVED:
	{
		remove_gamepad(e.gdevice.which, tracker, dispatcher);
		return true;
	}

	case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
	case SDL_EVENT_GAMEPAD_BUTTON_UP:
	{
		int player = SDL_GetJoystickInstancePlayerIndex(e.gbutton.which);
		if (player < 0 || player >= int(InputTracker::Joypads) || !pads[player])
			break;

		JoypadKey key = sdl_gamepad_button_to_granite(SDL_GamepadButton(e.gbutton.button));
		if (key == JoypadKey::Unknown)
			break;

		auto state = e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ?
		             JoypadKeyState::Pressed : JoypadKeyState::Released;

		dispatcher([player, key, state, &tracker]() {
			tracker.joypad_key_state(player, key, state);
		});
		return true;
	}

	case SDL_EVENT_GAMEPAD_AXIS_MOTION:
	{
		int player = SDL_GetJoystickInstancePlayerIndex(e.gaxis.which);
		if (player < 0 || player >= int(InputTracker::Joypads) || !pads[player])
			break;

		JoypadAxis axis = sdl_gamepad_axis_to_granite(SDL_GamepadAxis(e.gaxis.axis));
		bool is_trigger = axis == JoypadAxis::LeftTrigger || axis == JoypadAxis::RightTrigger;

		float value;
		if (is_trigger)
		{
			value = float(e.gaxis.value) / float(SDL_JOYSTICK_AXIS_MAX);
		}
		else
		{
			value = (float(e.gaxis.value) - SDL_JOYSTICK_AXIS_MIN) /
			        float(SDL_JOYSTICK_AXIS_MAX - SDL_JOYSTICK_AXIS_MIN);
			value = 2.0f * value - 1.0f;
		}

		dispatcher([player, axis, value, &tracker]() {
			tracker.joyaxis_state(player, axis, value);
		});
		return true;
	}

	default:
		break;
	}

	return false;
}

void InputTrackerSDL::add_gamepad(SDL_JoystickID id, InputTracker &tracker, const Dispatcher &dispatcher)
{
	int player_index = SDL_GetJoystickInstancePlayerIndex(id);
	if (player_index >= 0 && player_index < int(InputTracker::Joypads) && !pads[player_index])
	{
		uint32_t vid = SDL_GetGamepadInstanceVendor(id);
		uint32_t pid = SDL_GetGamepadInstanceProduct(id);
		const char *name = SDL_GetGamepadInstanceName(id);
		LOGI("Plugging in controller: \"%s\" (%u/%u).\n", name, vid, pid);
		const char *mapping = SDL_GetGamepadInstanceMapping(id);
		LOGI(" Using mapping: \"%s\"\n", mapping);
		pads[player_index] = SDL_OpenGamepad(id);
		ids[player_index] = id;
		dispatcher([player_index, vid, pid, &tracker]() {
			tracker.enable_joypad(player_index, vid, pid);
		});
	}
}

void InputTrackerSDL::remove_gamepad(SDL_JoystickID id, InputTracker &tracker, const Dispatcher &dispatcher)
{
	for (int i = 0; i < int(InputTracker::Joypads); i++)
	{
		if (pads[i] && ids[i] == id)
		{
			uint32_t vid = SDL_GetGamepadVendor(pads[i]);
			uint32_t pid = SDL_GetGamepadProduct(pads[i]);
			SDL_CloseGamepad(pads[i]);
			pads[i] = nullptr;
			ids[i] = 0;
			dispatcher([i, vid, pid, &tracker]() {
				tracker.disable_joypad(i, vid, pid);
			});
			break;
		}
	}
}
}
