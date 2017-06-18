#pragma once

#include <memory>
#include <vector>

namespace Util
{
template <typename T>
class ObjectPool
{
public:
	template <typename... P>
	T *allocate(P &&... p)
	{
		if (vacants.empty())
		{
			unsigned num_objects = 64u << memory.size();
			T *ptr = static_cast<T *>(malloc(num_objects * sizeof(T)));
			if (!ptr)
				return nullptr;

			for (unsigned i = 0; i < num_objects; i++)
				vacants.push_back(&ptr[i]);

			memory.emplace_back(ptr);
		}

		T *ptr = vacants.back();
		vacants.pop_back();
		new (ptr) T(std::forward<P>(p)...);
		return ptr;
	}

	void free(T *ptr)
	{
		ptr->~T();
		vacants.push_back(ptr);
	}

	void clear()
	{
		vacants.clear();
		memory.clear();
	}

private:
	std::vector<T *> vacants;
	struct MallocDeleter
	{
		void operator()(T *ptr)
		{
			::free(ptr);
		}
	};
	std::vector<std::unique_ptr<T, MallocDeleter>> memory;
};
}
