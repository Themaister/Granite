/* Copyright (c) 2017-2020 Hans-Kristian Arntzen
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

#include "xinput_windows.hpp"
#include "logging.hpp"
#include "bitops.hpp"

using namespace Util;
using namespace std;

#ifndef ERROR_DEVICE_NOT_CONNECTED
#define ERROR_DEVICE_NOT_CONNECTED 1167
#endif

namespace Granite
{
bool XInputManager::init(Granite::InputTracker *tracker_)
{
	if (!lib)
		lib = DynamicLibrary("xinput1_4");
	if (!lib)
		lib = DynamicLibrary("xinput1_3");

	if (lib)
		pXInputGetState = lib.get_symbol<decltype(&XInputGetState)>("XInputGetState");

	tracker = tracker_;

	for (unsigned i = 0; i < 4; i++)
		try_polling_device(i);

	return true;
}

void XInputManager::try_polling_device(unsigned index)
{
	if (active_pads & (1u << index))
		return;
	if (!pXInputGetState)
		return;

	XINPUT_STATE new_state;
	memset(&new_state, 0, sizeof(new_state));
	if (pXInputGetState(index, &new_state) != ERROR_DEVICE_NOT_CONNECTED)
	{
		tracker->enable_joypad(index);
		create_events(index, new_state);
		active_pads |= 1u << index;
	}
}

bool XInputManager::poll()
{
	if (!pXInputGetState)
		return true;

	for (unsigned i = 0; i < 4; i++)
	{
		if ((active_pads & (1u << i)) == 0)
			continue;

		XINPUT_STATE new_state;
		memset(&new_state, 0, sizeof(new_state));
		if (pXInputGetState(i, &new_state) != ERROR_DEVICE_NOT_CONNECTED)
			create_events(i, new_state);
		else
		{
			tracker->disable_joypad(i);
			memset(&pads[i], 0, sizeof(pads[i]));
			active_pads &= ~(1u << i);
		}
	}

	poll_count++;
	if (poll_count >= 200) // Poll for new devices once in a while
	{
		poll_count = 0;
		for (unsigned i = 0; i < 4; i++)
			try_polling_device(i);
	}

	return true;
}

static float remap_axis(int v)
{
	float f = float(v) / 0x7fff;
	if (f < -1.0f)
		f = -1.0f;
	else if (f > 1.0f)
		f = 1.0f;
	return f;
}

void XInputManager::create_events(unsigned index, const XINPUT_STATE &state)
{
	auto &pad = pads[index];
	if (pad.dwPacketNumber == state.dwPacketNumber && pad.dwPacketNumber != 0)
		return;

	uint16_t pressed = state.Gamepad.wButtons & ~pad.Gamepad.wButtons;
	uint16_t released = ~state.Gamepad.wButtons & pad.Gamepad.wButtons;

	static const JoypadKey joykey_mapping[16] = {
		JoypadKey::Up,
		JoypadKey::Down,
		JoypadKey::Left,
		JoypadKey::Right,
		JoypadKey::Start,
		JoypadKey::Select,
		JoypadKey::LeftThumb,
		JoypadKey::RightThumb,
		JoypadKey::LeftShoulder,
		JoypadKey::RightShoulder,
		JoypadKey::Unknown,
		JoypadKey::Unknown,
		JoypadKey::South,
		JoypadKey::East,
		JoypadKey::West,
		JoypadKey::North
	};

	for_each_bit(pressed, [&](unsigned bit) {
		tracker->joypad_key_state(index, joykey_mapping[bit], JoypadKeyState::Pressed);
	});
	for_each_bit(released, [&](unsigned bit) {
		tracker->joypad_key_state(index, joykey_mapping[bit], JoypadKeyState::Released);
	});

	if (state.Gamepad.sThumbLX != pad.Gamepad.sThumbLX)
		tracker->joyaxis_state(index, JoypadAxis::LeftX, remap_axis(state.Gamepad.sThumbLX));
	if (state.Gamepad.sThumbRX != pad.Gamepad.sThumbRX)
		tracker->joyaxis_state(index, JoypadAxis::RightX, remap_axis(state.Gamepad.sThumbRX));
	if (state.Gamepad.sThumbLY != pad.Gamepad.sThumbLY)
		tracker->joyaxis_state(index, JoypadAxis::LeftY, remap_axis(-int(state.Gamepad.sThumbLY)));
	if (state.Gamepad.sThumbRY != pad.Gamepad.sThumbRY)
		tracker->joyaxis_state(index, JoypadAxis::RightY, remap_axis(-int(state.Gamepad.sThumbRY)));
	if (state.Gamepad.bLeftTrigger != pad.Gamepad.bLeftTrigger)
		tracker->joyaxis_state(index, JoypadAxis::LeftTrigger, state.Gamepad.bLeftTrigger / 255.0f);
	if (state.Gamepad.bRightTrigger != pad.Gamepad.bRightTrigger)
		tracker->joyaxis_state(index, JoypadAxis::RightTrigger, state.Gamepad.bRightTrigger / 255.0f);
	pad = state;
}

}
