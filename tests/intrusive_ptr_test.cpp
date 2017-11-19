/* Copyright (c) 2017 Hans-Kristian Arntzen
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

int main()
{
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