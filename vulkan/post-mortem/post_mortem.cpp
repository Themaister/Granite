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

#include "post_mortem.hpp"

#ifdef HAVE_AFTERMATH_SDK
#include "NsightAftermathGpuCrashTracker.h"
#endif

namespace Vulkan
{
namespace PostMortem
{
class CrashTracker
{
public:
	virtual ~CrashTracker() = default;
	virtual void register_shader(const void *data, size_t size) = 0;
};

static std::unique_ptr<CrashTracker> global_tracker;

#ifdef HAVE_AFTERMATH_SDK
struct NsightCrashTracker : CrashTracker
{
	GpuCrashTracker::MarkerMap marker;
	GpuCrashTracker tracker;
	NsightCrashTracker() : tracker(marker) {}
	void register_shader(const void *data, size_t size) override { tracker.RegisterShader(data, size); }
};
#endif

void init_nv_aftermath()
{
	if (global_tracker)
		return;

#ifdef HAVE_AFTERMATH_SDK
	auto tracker = std::make_unique<NsightCrashTracker>();
	tracker->tracker.Initialize();
	global_tracker = std::move(tracker);
#endif
}

void register_shader(const void *data, size_t size)
{
	if (global_tracker)
		global_tracker->register_shader(data, size);
}

void deinit()
{
	global_tracker.reset();
}
}
}