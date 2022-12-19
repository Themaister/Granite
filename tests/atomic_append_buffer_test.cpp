#include "atomic_append_buffer.hpp"
#include "logging.hpp"
#include "thread_group.hpp"
#include <vector>
#include <stdlib.h>

using namespace Util;
using namespace Granite;

static void test_iterations(ThreadGroup &group, unsigned iterations)
{
	AtomicAppendBuffer<unsigned, 2> Buf;

	for (unsigned i = 0; i < iterations; i++)
		group.create_task([i, &Buf]() { Buf.push(i); });
	group.wait_idle();

	std::vector<unsigned> output;

	Buf.for_each_ranged([&](const unsigned *values, size_t count) {
		output.insert(output.end(), values, values + count);
	});

	std::sort(output.begin(), output.end());

	if (output.size() != iterations)
		exit(EXIT_FAILURE);

	for (unsigned i = 0; i < iterations; i++)
		if (output[i] != i)
			exit(EXIT_FAILURE);
}

int main()
{
	ThreadGroup group;
	group.start(4, 0, {});
	test_iterations(group, 0);
	test_iterations(group, 3);
	test_iterations(group, 9);
	test_iterations(group, 8);
	test_iterations(group, 16);
	test_iterations(group, 32);
	test_iterations(group, 34);
	test_iterations(group, 63);
	test_iterations(group, 94);
	test_iterations(group, 195);
}