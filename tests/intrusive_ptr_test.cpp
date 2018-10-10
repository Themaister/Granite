/* Copyright (c) 2017-2018 Hans-Kristian Arntzen
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
#include "util.hpp"
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

struct Value : IntrusiveHashMapEnabled<Value>
{
	Value(int v)
		: value(v)
	{
	}

	int value;
};

int main()
{
	IntrusiveHashMap<Value> hash_map;

	const auto emplace_integer = [&](int key, int value) {
		Hasher h;
		h.s32(key);
		auto hash = h.get();
		hash_map.emplace_yield(hash, value);
	};

	const auto emplace_replace = [&](int key, int value) {
		Hasher h;
		h.s32(key);
		auto hash = h.get();
		hash_map.emplace_replace(hash, value);
	};

	const auto find = [&](int key) -> Value * {
		Hasher h;
		h.s32(key);
		auto hash = h.get();
		return hash_map.find(hash);
	};

	emplace_integer(1, 10);
	emplace_integer(1, 20);
	emplace_integer(2, 100);
	emplace_integer(3, 1000);
	emplace_integer(4, 9999);
	emplace_replace(4, 10000);

	for (auto &v : hash_map)
		LOGI("Value: %d\n", v.value);

	Value *v;
	v = find(1);
	assert(v && v->value == 10);
	v = find(2);
	assert(v && v->value == 100);
	v = find(3);
	assert(v && v->value == 1000);
	v = find(4);
	assert(v && v->value == 10000);

	hash_map.erase(find(2));

	for (auto &v : hash_map)
		LOGI("Value: %d\n", v.value);

	v = find(1);
	assert(v && v->value == 10);
	v = find(2);
	assert(!v);
	v = find(3);
	assert(v && v->value == 1000);
	v = find(4);
	assert(v && v->value == 10000);

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