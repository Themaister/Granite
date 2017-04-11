#pragma once

namespace Granite
{
template <typename T>
constexpr typename std::underlying_type<T>::type ecast(T x)
{
	return static_cast<typename std::underlying_type<T>::type>(x);
}
}