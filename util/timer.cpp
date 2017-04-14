#include "timer.hpp"
#include <chrono>

using namespace std;

namespace Granite
{
FrameTimer::FrameTimer()
{
	start = get_time();
	last = start;
	last_period = 0;
}

double FrameTimer::get_frame_time() const
{
	return double(last_period) * 1e-9;
}

double FrameTimer::frame()
{
	auto new_time = get_time();
	last_period = new_time - last;
	last = new_time;
	return double(last_period) * 1e-9;
}

double FrameTimer::get_elapsed() const
{
	return double(last - start) * 1e-9;
}

int64_t FrameTimer::get_time()
{
	auto current = chrono::steady_clock::now().time_since_epoch();
	auto nsecs = chrono::duration_cast<chrono::nanoseconds>(current);
	return nsecs.count();
}
}