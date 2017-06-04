#pragma once
#include <memory>
#include <stdint.h>
#include <unordered_map>

namespace Util
{
using Hash = uint64_t;

struct UnityHasher
{
	inline size_t operator()(uint64_t hash) const
	{
		return hash;
	}
};

template <typename T>
using HashMap = std::unordered_map<Hash, T, UnityHasher>;

class Hasher
{
public:
	template <typename T>
	inline void data(const T *data, size_t size)
	{
		using arith_type = decltype(data[int()]);
		size /= sizeof(arith_type);
		for (size_t i = 0; i < size; i++)
			h = (h * 0x100000001b3ull) ^ data[i];
	}

	inline void u32(uint32_t value)
	{
		h = (h * 0x100000001b3ull) ^ value;
	}

	inline void s32(int32_t value)
	{
		u32(uint32_t(value));
	}

	inline void f32(float value)
	{
		union
		{
			float f32;
			uint32_t u32;
		} u;
		u.f32 = value;
		u32(u.u32);
	}

	inline void u64(uint64_t value)
	{
		u32(value & 0xffffffffu);
		u32(value >> 32);
	}

	inline void pointer(const void *ptr)
	{
		u64(reinterpret_cast<uintptr_t>(ptr));
	}

	inline void string(const char *str)
	{
		char c;
		while ((c = *str++) != '\0')
			u32(uint8_t(c));
	}

	inline Hash get() const
	{
		return h;
	}

private:
	Hash h = 0xcbf29ce484222325ull;
};
}
