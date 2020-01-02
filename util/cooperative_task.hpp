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

#pragma once

#include <memory>

namespace Util
{
class CooperativeTaskRunnable
{
public:
	virtual ~CooperativeTaskRunnable() = default;

	bool is_runnable(double time) const;
	void set_current_time(double time);
	void yield_complete();
	bool is_complete() const;

	virtual void run() noexcept = 0;

protected:
	double get_current_time() const;
	void yield();
	void yield_and_delay(double time);

private:
	double current_time = 0.0;
	double sleep_until = 0.0;
	bool complete = false;
};

class CooperativeTask
{
public:
	explicit CooperativeTask(std::unique_ptr<CooperativeTaskRunnable> task_);
	~CooperativeTask();

	CooperativeTask(const CooperativeTask &) = delete;
	void operator=(const CooperativeTask &) = delete;

	void resume(double current_time);
	bool task_is_runnable(double current_time) const;
	bool task_is_complete() const;

private:
	std::unique_ptr<CooperativeTaskRunnable> task;
	void *cothread = nullptr;
};
}

