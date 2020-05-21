/* Copyright (c) 2017-2020 Hans-Kristian Arntzen
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

#include "intrusive.hpp"
#include "intrusive_hash_map.hpp"
#include "logging.hpp"
#include <vector>

using namespace Util;

struct A : IntrusivePtrEnabled<A>
{
	virtual ~A() = default;
	int a = 5;
};

struct B : A
{
	~B()
	{
		LOGE("Destroying B.\n");
	}
	int b = 10;
};

static unsigned destructor_count = 0;

struct NonPOD : IntrusiveHashMapEnabled<NonPOD>
{
	NonPOD(int a) { v = a; }
	~NonPOD()
	{
		destructor_count++;
	}
	int get() { return v; }
	int v;
};

static Hash get_key(int v)
{
	return ((v & 7) << 24) | (v >> 3);
}

int main()
{
	ThreadSafeIntrusiveHashMap<NonPOD> hash_map;

	for (int i = 0; i < 100000; i++)
	{
		hash_map.emplace_yield(get_key(i), i + 2000000);
		hash_map.emplace_replace(get_key(i), i + 3000000);
	}

	assert(destructor_count == 100000);

	for (int i = 0; i < 100000; i += 2)
	{
		hash_map.erase(hash_map.find(get_key(i)));
	}

	assert(destructor_count == 150000);

	for (int i = 1; i < 100000; i += 2)
	{
		auto *v = hash_map.find(get_key(i));
		assert(v && v->get() == i + 3000000);
	}

	hash_map.clear();
	assert(destructor_count == 200000);

	std::vector<IntrusivePtr<A>> as;

	{
		auto b = make_handle<B>();
		IntrusivePtr<A> a;
		a = b;
		IntrusivePtr<A> c;
		c = a;
		as.push_back(a);
	}

	LOGI("a->a = %d\n", as[0]->a);
}