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

#include "xinput_windows.hpp"
#include "logging.hpp"
#include "bitops.hpp"

using namespace Util;

#ifndef ERROR_DEVICE_NOT_CONNECTED
#define ERROR_DEVICE_NOT_CONNECTED 1167
#endif

namespace Granite
{
XInputManager::~XInputManager()
{
	for (auto *dev : pDevice)
		if (dev)
			dev->Release();
	if (pDI)
		pDI->Release();
}

// Really should just move to SDL ... But need quick hack to make PS4 controllers work :')

static BOOL CALLBACK enum_callback(const DIDEVICEINSTANCEA *inst, void *p)
{
	auto *manager = static_cast<XInputManager *>(p);
	return manager->di_enum_callback(inst);
}

BOOL XInputManager::di_enum_callback(const DIDEVICEINSTANCEA *inst)
{
	// Different PIDs for wireless dongle and cabled connection.
	if (inst->guidProduct.Data1 != 195036492 && inst->guidProduct.Data1 != 164365644)
	{
		LOGI("Enumerated DI input device that is not PS4 controller. Bailing ...\n");
		return DIENUM_CONTINUE;
	}

	LOGI("Enumerated PS4 controller.\n");

	unsigned index = trailing_ones(active_pads);
	if (index >= 4)
		return DIENUM_STOP;

	if (FAILED(pDI->CreateDevice(inst->guidInstance, &pDevice[index], nullptr)))
		return DIENUM_CONTINUE;

	if (FAILED(pDevice[index]->SetDataFormat(&c_dfDIJoystick2)))
	{
		pDevice[index]->Release();
		pDevice[index] = nullptr;
		return DIENUM_CONTINUE;
	}

	if (FAILED(pDevice[index]->SetCooperativeLevel(hwnd, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE)))
	{
		pDevice[index]->Release();
		pDevice[index] = nullptr;
		return DIENUM_CONTINUE;
	}

	active_pads |= 1u << index;
	tracker->enable_joypad(index);
	return DIENUM_CONTINUE;
}

bool XInputManager::init(Granite::InputTracker *tracker_, HWND hwnd_)
{
	hwnd = hwnd_;
	if (!lib)
		lib = DynamicLibrary("xinput1_4");
	if (!lib)
		lib = DynamicLibrary("xinput1_3");

	if (lib)
		pXInputGetState = lib.get_symbol<decltype(&XInputGetState)>("XInputGetState");

	tracker = tracker_;

	for (unsigned i = 0; i < 4; i++)
		try_polling_device(i);

	HRESULT hr;
	if (FAILED(hr = DirectInput8Create(GetModuleHandle(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8A, (void**)&pDI, nullptr)))
		return false;

	if (FAILED(pDI->EnumDevices(DI8DEVCLASS_GAMECTRL, enum_callback, this, DIEDFL_ATTACHEDONLY)))
		return false;

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

		if (pDevice[i])
		{
			if (FAILED(pDevice[i]->Poll()) && FAILED(pDevice[i]->Acquire()) && FAILED(pDevice[i]->Poll()))
			{
				tracker->disable_joypad(i);
				active_pads &= ~(1u << i);
				pDevice[i]->Release();
				pDevice[i] = nullptr;
			}
			else
			{
				DIJOYSTATE2 joy_state = {};
				if (SUCCEEDED(pDevice[i]->GetDeviceState(sizeof(DIJOYSTATE2), &joy_state)))
				{
					// Hardcoded for PS4 dinput, yaaaay <_<
					static const JoypadKey joykey_mapping[] =
					{
						JoypadKey::West, JoypadKey::South, JoypadKey::East, JoypadKey::North,
						JoypadKey::LeftShoulder, JoypadKey::RightShoulder,
						JoypadKey::Unknown, JoypadKey::Unknown,
						JoypadKey::Select, JoypadKey::Start,
						JoypadKey::LeftThumb, JoypadKey::RightThumb,
						JoypadKey::Mode
					};

					for (unsigned j = 0; j < sizeof(joykey_mapping) / sizeof(joykey_mapping[0]); j++)
					{
						if (joykey_mapping[j] == JoypadKey::Unknown)
							continue;

						tracker->joypad_key_state(
							i, joykey_mapping[j], joy_state.rgbButtons[j] ? JoypadKeyState::Pressed : JoypadKeyState::Released);
					}

					float lx = 2.0f * joy_state.lX / float(0xffff) - 1.0f;
					float ly = 2.0f * joy_state.lY / float(0xffff) - 1.0f;
					float rx = 2.0f * joy_state.lZ / float(0xffff) - 1.0f;
					float ry = 2.0f * joy_state.lRz / float(0xffff) - 1.0f;
					float lt = joy_state.lRx / float(0xffff);
					float rt = joy_state.lRy / float(0xffff);

					tracker->joyaxis_state(i, JoypadAxis::LeftX, lx);
					tracker->joyaxis_state(i, JoypadAxis::LeftY, ly);
					tracker->joyaxis_state(i, JoypadAxis::RightX, rx);
					tracker->joyaxis_state(i, JoypadAxis::RightY, ry);
					tracker->joyaxis_state(i, JoypadAxis::LeftTrigger, lt);
					tracker->joyaxis_state(i, JoypadAxis::RightTrigger, rt);

					int pov = joy_state.rgdwPOV[0];

					bool left = false, right = false, up = false, down = false;
					if (pov >= 0)
					{
						pov /= 100;
						up = pov > 270 || pov < 90;
						right = pov > 0 && pov < 180;
						down = pov > 90 && pov < 270;
						left = pov > 180;
					}

					tracker->joypad_key_state(i, JoypadKey::Right, right ? JoypadKeyState::Pressed : JoypadKeyState::Released);
					tracker->joypad_key_state(i, JoypadKey::Down, down ? JoypadKeyState::Pressed : JoypadKeyState::Released);
					tracker->joypad_key_state(i, JoypadKey::Up, up ? JoypadKeyState::Pressed : JoypadKeyState::Released);
					tracker->joypad_key_state(i, JoypadKey::Left, left ? JoypadKeyState::Pressed : JoypadKeyState::Released);
				}
			}
		}
		else
		{
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
