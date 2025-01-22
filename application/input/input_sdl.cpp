/* Copyright (c) 2017-2024 Hans-Kristian Arntzen
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
bool InputTrackerSDL::init(InputTracker &tracker, const Dispatcher &dispatcher)
{
	// Open existing gamepads.
	int num_pads = 0;
	SDL_JoystickID *gamepad_ids = SDL_GetGamepads(&num_pads);
	for (int i = 0; i < num_pads; i++)
		add_gamepad(gamepad_ids[i], tracker, dispatcher);
	if (gamepad_ids)
		SDL_free(gamepad_ids);

	// Poll these separately, inline in poll_input().
	SDL_SetGamepadEventsEnabled(false);
	SDL_SetJoystickEventsEnabled(false);
	SDL_SetEventEnabled(SDL_EVENT_GAMEPAD_ADDED, true);
	SDL_SetEventEnabled(SDL_EVENT_GAMEPAD_REMOVED, true);
	SDL_SetEventEnabled(SDL_EVENT_JOYSTICK_UPDATE_COMPLETE, false);
	SDL_SetEventEnabled(SDL_EVENT_GAMEPAD_UPDATE_COMPLETE, false);

	return true;
}

void InputTrackerSDL::update(InputTracker &tracker)
{
	SDL_UpdateGamepads();

	for (int i = 0; i < int(InputTracker::Joypads); i++)
	{
		auto *pad = pads[i];
		if (!pad)
			continue;

		static const struct
		{
			JoypadKey gkey;
			SDL_GamepadButton sdl;
		} buttons[] = {
			{ JoypadKey::Left, SDL_GAMEPAD_BUTTON_DPAD_LEFT },
			{ JoypadKey::Right, SDL_GAMEPAD_BUTTON_DPAD_RIGHT },
			{ JoypadKey::Up, SDL_GAMEPAD_BUTTON_DPAD_UP },
			{ JoypadKey::Down, SDL_GAMEPAD_BUTTON_DPAD_DOWN },
			{ JoypadKey::Start, SDL_GAMEPAD_BUTTON_START },
			{ JoypadKey::Select, SDL_GAMEPAD_BUTTON_BACK },
			{ JoypadKey::East, SDL_GAMEPAD_BUTTON_EAST },
			{ JoypadKey::West, SDL_GAMEPAD_BUTTON_WEST },
			{ JoypadKey::North, SDL_GAMEPAD_BUTTON_NORTH },
			{ JoypadKey::South, SDL_GAMEPAD_BUTTON_SOUTH },
			{ JoypadKey::LeftShoulder, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER },
			{ JoypadKey::RightShoulder, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER },
			{ JoypadKey::LeftThumb, SDL_GAMEPAD_BUTTON_LEFT_STICK },
			{ JoypadKey::RightThumb, SDL_GAMEPAD_BUTTON_RIGHT_STICK },
			{ JoypadKey::Mode, SDL_GAMEPAD_BUTTON_GUIDE },
		};

		for (auto &b : buttons)
		{
			tracker.joypad_key_state(i, b.gkey,
			                         SDL_GetGamepadButton(pad, b.sdl) ?
			                         JoypadKeyState::Pressed : JoypadKeyState::Released);
		}

		static const struct
		{
			JoypadAxis gaxis;
			SDL_GamepadAxis sdl;
		} axes[] = {
			{ JoypadAxis::LeftX, SDL_GAMEPAD_AXIS_LEFTX },
			{ JoypadAxis::LeftY, SDL_GAMEPAD_AXIS_LEFTY },
			{ JoypadAxis::RightX, SDL_GAMEPAD_AXIS_RIGHTX },
			{ JoypadAxis::RightY, SDL_GAMEPAD_AXIS_RIGHTY },
		};

		for (auto &a : axes)
		{
			float value = float(SDL_GetGamepadAxis(pad, a.sdl) - SDL_JOYSTICK_AXIS_MIN) /
			              float(SDL_JOYSTICK_AXIS_MAX - SDL_JOYSTICK_AXIS_MIN);
			value = 2.0f * value - 1.0f;
			tracker.joyaxis_state(i, a.gaxis, value);
		}

		tracker.joyaxis_state(i, JoypadAxis::LeftTrigger,
		                      float(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER)) /
		                      float(SDL_JOYSTICK_AXIS_MAX));

		tracker.joyaxis_state(i, JoypadAxis::RightTrigger,
		                      float(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) /
		                      float(SDL_JOYSTICK_AXIS_MAX));
	}
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

	default:
		break;
	}

	return false;
}

void InputTrackerSDL::add_gamepad(SDL_JoystickID id, InputTracker &tracker, const Dispatcher &dispatcher)
{
	int player_index = SDL_GetJoystickPlayerIndexForID(id);
	if (player_index >= 0 && player_index < int(InputTracker::Joypads) && !pads[player_index])
	{
		uint32_t vid = SDL_GetGamepadVendorForID(id);
		uint32_t pid = SDL_GetGamepadProductForID(id);
		const char *name = SDL_GetGamepadNameForID(id);
		LOGI("Plugging in controller: \"%s\" (%u/%u).\n", name, vid, pid);
		const char *mapping = SDL_GetGamepadMappingForID(id);
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
