#include "unordered_array.hpp"
#include "object_pool.hpp"
#include <memory>

using namespace Util;

class Foo : public IntrusiveUnorderedArrayEnabled
{
public:
	explicit Foo(int v) : value(std::make_unique<int>(v)) {}

	int get_value() const
	{
		return *value;
	}

private:
	// Test destructors.
	std::unique_ptr<int> value;
};

int main()
{
	ObjectPool<Foo> foo_pool;
	IntrusiveUnorderedArray<Foo> foo;

	const auto sum_values = [&]() -> int {
		int v = 0;
		for (auto *f : foo)
			v += f->get_value();
		return v;
	};
	int expected_sum = 0;

	std::vector<Foo *> ptrs;

	for (int i = 0; i < 1024; i++)
	{
		auto *ptr = foo_pool.allocate(i);
		foo.add(ptr);
		ptrs.push_back(ptr);
		expected_sum += i;
	}

	if (expected_sum != sum_values())
		return EXIT_FAILURE;

	if (foo.size() != 1024)
		return EXIT_FAILURE;

	static const int removal_indices[] = {
		1023, 10, 192, 1000, 14, 15, 0, 1, 80,
	};

	for (auto remove_index : removal_indices)
	{
		foo.erase(ptrs[remove_index]);
		foo_pool.free(ptrs[remove_index]);
		expected_sum -= remove_index;
		if (expected_sum != sum_values())
			return EXIT_FAILURE;
	}

	if (foo.size() != 1024 - 9)
		return EXIT_FAILURE;

	foo.garbage_collect_if([&](Foo *value) -> bool {
		if (value->get_value() == 20 || value->get_value() == 1022 || value->get_value() == 40)
		{
			expected_sum -= value->get_value();
			foo_pool.free(value);
			return true;
		}
		else
			return false;
	});

	if (foo.size() != 1024 - 9 - 3)
		return EXIT_FAILURE;

	if (expected_sum != sum_values())
		return EXIT_FAILURE;

	for (auto *f : foo)
		foo_pool.free(f);
}