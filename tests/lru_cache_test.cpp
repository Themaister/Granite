#include "lru_cache.hpp"
#include "logging.hpp"

using namespace Util;

static int active_foos;

struct Foo
{
	Foo()
	{
		active_foos++;
		LOGI("Construct (%d alive)!\n", active_foos);
	}

	~Foo()
	{
		active_foos--;
		LOGI("Destruct (%d alive)!\n", active_foos);
	}
	unsigned value = 0;
};

int main()
{
	LRUCache<Foo> cache;
	cache.set_total_cost(20);

	cache.allocate(1, 10)->value = 1;
	cache.allocate(2, 10)->value = 2;
	cache.allocate(3, 10)->value = 3;
	cache.allocate(4, 10)->value = 4;
	cache.allocate(3, 10);
	cache.evict(2);
	cache.erase(1);

	LOGI("=== Values ===\n");
	for (auto &entry : cache)
		LOGI("Value: %u\n", entry.t.value);

	cache.prune();

	LOGI("=== Pruned ===\n");
	for (auto &entry : cache)
		LOGI("Value: %u\n", entry.t.value);
}