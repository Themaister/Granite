/* Copyright (c) 2017-2024 Hans-Kristian Arntzen
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

#include "thread_priority.hpp"
#include "logging.hpp"

#if defined(__linux__)
#include <pthread.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace Util
{
void set_current_thread_priority(ThreadPriority priority)
{
#if defined(__linux__)
	if (priority == ThreadPriority::Low)
	{
		struct sched_param param = {};
		int policy = 0;
		param.sched_priority = sched_get_priority_min(SCHED_BATCH);
		policy = SCHED_BATCH;
		if (pthread_setschedparam(pthread_self(), policy, &param) != 0)
			LOGE("Failed to set thread priority.\n");
	}
#elif defined(_WIN32)
	if (priority == ThreadPriority::Low)
	{
		if (!SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN))
			LOGE("Failed to set background thread priority.\n");
	}
	else if (priority == ThreadPriority::Default)
	{
		if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL))
			LOGE("Failed to set normal thread priority.\n");
	}
	else if (priority == ThreadPriority::High)
	{
		if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST))
			LOGE("Failed to set high thread priority.\n");
	}
#else
#warning "Unimplemented set_current_thread_priority."
	(void)priority;
#endif
}
}
