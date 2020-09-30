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

#include "thread_group.hpp"
#include "logging.hpp"

using namespace Granite;

int main()
{
	ThreadGroup group;
	group.start(4);

	auto task1 = group.create_task([]() {
		LOGI("Ohai!\n");
	});
	auto task2 = group.create_task([]() {
		LOGI("Ohai 2!\n");
	});
	auto task3 = group.create_task([]() {
		LOGI("Ohai 3!\n");
	});
	group.enqueue_task(*task3, []() {
		LOGI("Brrr :3\n");
	});
	task1->id = 1;
	task2->id = 2;
	task3->id = 3;
	group.add_dependency(*task1, *task3);
	group.add_dependency(*task2, *task3);
	group.add_dependency(*task1, *task2);
	group.submit(task1);
	group.submit(task2);
	group.submit(task3);

	group.wait_idle();
}
