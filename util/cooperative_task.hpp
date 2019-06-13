/* Copyright (c) 2017-2019 Hans-Kristian Arntzen
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

#include <thread>
#include <mutex>
#include <condition_variable>
#include <memory>

// Should really just be a stack-based co-routine/fiber,
// but I cannot find a portable alternative which fits all my requirements, sadly.
// The context swap frequency and number of routines should be low enough that OS threads are viable for now.
// When need arise, figure out a more appropriate solution.

namespace Util
{
struct CooperativeTaskKilledException
{
};

class CooperativeTaskRunnable
{
public:
	virtual ~CooperativeTaskRunnable() = default;

	bool wait_for_yield()
	{
		std::unique_lock<std::mutex> holder{lock};
		cond.wait(holder, [&]() -> bool
		{
			return !task_is_active;
		});
		return complete;
	}

	void wait_for_resume()
	{
		std::unique_lock<std::mutex> holder{lock};
		cond.wait(holder, [&]() -> bool {
			return task_is_active || dead;
		});

		if (dead)
			throw CooperativeTaskKilledException();
	}

	void kill()
	{
		std::lock_guard<std::mutex> holder{lock};
		dead = true;
		cond.notify_one();
	}

	void resume()
	{
		std::lock_guard<std::mutex> holder{lock};
		task_is_active = true;
		cond.notify_one();
	}

	bool is_runnable(double time) const
	{
		return time >= sleep_until;
	}

	void set_current_time(double time)
	{
		current_time = time;
	}

	void yield_complete()
	{
		std::lock_guard<std::mutex> holder{lock};
		task_is_active = false;
		complete = true;
		cond.notify_one();
	}

	virtual void run() = 0;

protected:
	double get_current_time() const
	{
		return current_time;
	}

	void yield()
	{
		{
			std::lock_guard<std::mutex> holder{lock};
			task_is_active = false;
			cond.notify_one();
		}

		wait_for_resume();
	}

	void yield_and_delay(double time)
	{
		sleep_until = current_time + time;
		yield();
	}

private:
	double current_time = 0.0;
	double sleep_until = 0.0;
	std::mutex lock;
	std::condition_variable cond;
	bool task_is_active = false;
	bool dead = false;
	bool complete = false;
};

// If the task can be run concurrently with other tasks, or if it should run serially with main thread.
enum class CooperativeTaskType
{
	Concurrent,
	Serial
};

class CooperativeTask
{
public:
	CooperativeTask(std::unique_ptr<CooperativeTaskRunnable> task_,
	                CooperativeTaskType task_type_)
		: task(std::move(task_)), task_type(task_type_)
	{
		thr = std::thread(&CooperativeTask::run_thread, this);
	}

	~CooperativeTask()
	{
		task->kill();
		if (thr.joinable())
			thr.join();
	}

	CooperativeTask(const CooperativeTask &) = delete;
	void operator=(const CooperativeTask &) = delete;

	void resume(double current_time)
	{
		task->set_current_time(current_time);
		task->resume();
	}

	bool wait_yield()
	{
		if (task->wait_for_yield())
		{
			if (thr.joinable())
				thr.join();
			return true;
		}
		else
			return false;
	}

	bool task_is_runnable(double current_time) const
	{
		return task->is_runnable(current_time);
	}

	CooperativeTaskType get_task_type() const
	{
		return task_type;
	}

private:
	std::thread thr;
	std::unique_ptr<CooperativeTaskRunnable> task;
	CooperativeTaskType task_type;

	void run_thread()
	{
		try
		{
			task->wait_for_resume();
			task->run();
		}
		catch (...)
		{
		}
		task->yield_complete();
	}
};
}

