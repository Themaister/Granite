#pragma once
#include <memory>
#include <stdint.h>
#include <unordered_map>

namespace Vulkan
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

	inline void u64(uint64_t value)
	{
		u32(value & 0xffffffffu);
		u32(value >> 32);
	}

	inline Hash get() const
	{
		return h;
	}

private:
	Hash h = 0xcbf29ce484222325ull;
};
}
