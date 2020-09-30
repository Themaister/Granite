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

#include "task_composer.hpp"

namespace Granite
{
TaskComposer::TaskComposer(ThreadGroup &group_)
	: group(group_)
{
}

void TaskComposer::set_incoming_task(TaskGroupHandle group_)
{
	incoming = std::move(group_);
}

TaskGroup &TaskComposer::begin_pipeline_stage()
{
	auto new_group = group.create_task();
	if (incoming)
		group.add_dependency(*new_group, *incoming);
	incoming = new_group;
	return *incoming;
}

TaskGroup &TaskComposer::get_group()
{
	return *incoming;
}

TaskGroupHandle TaskComposer::get_outgoing_task()
{
	if (incoming)
	{
		auto new_group = group.create_task();
		group.add_dependency(*new_group, *incoming);
		incoming = new_group;
	}
	return incoming;
}

ThreadGroup &TaskComposer::get_thread_group()
{
	return group;
}
}
