#pragma once

#include <algorithm>

namespace Util
{
template <typename T, size_t N>
class StackAllocator
{
public:
	T *allocate(size_t count)
	{
		if (count == 0)
			return nullptr;
		if (offset + count > N)
			return nullptr;

		T *ret = buffer + offset;
		offset += count;
		return ret;
	}

	T *allocate_cleared(size_t count)
	{
		T *ret = allocate(count);
		if (ret)
			std::fill(ret, ret + count, T());
		return ret;
	}

	void reset()
	{
		offset = 0;
	}

private:
	T buffer[N];
	size_t offset = 0;
};
}