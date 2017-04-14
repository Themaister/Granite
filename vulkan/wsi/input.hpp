#pragma once

#include "enum_cast.hpp"
#include <stdint.h>

namespace Vulkan
{
enum class Key
{
	A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
	Return,
	LeftCtrl,
	LeftAlt,
	LeftShift,
	Space,
	Count
};

enum class KeyState
{
	Pressed,
	Released,
	Repeat
};
static_assert(Util::ecast(Key::Count) <= 64, "Cannot have more than 64 keys for bit-packing.");

class InputTracker
{
public:
	void key_event(Key key, KeyState state);

private:
	uint64_t key_state = 0;
};

}