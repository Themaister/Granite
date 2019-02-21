#include "dsp/tone_filter.hpp"
#include "timer.hpp"
#include "util.hpp"
#include <random>

using namespace Granite::Audio;

int main()
{
	DSP::ToneFilter filter;
	filter.init(44100.0f);

	float buffer[1000] = {};
	float out_buffer[1000] = {};

	std::mt19937 rnd;
	std::uniform_real_distribution<float> range(-1.0f, 1.0f);
	for (auto &b : buffer)
		b = range(rnd);

	auto start = Util::get_current_time_nsecs();
	for (unsigned i = 0; i < 20000; i++)
		filter.filter(out_buffer, buffer, 1000);
	auto end = Util::get_current_time_nsecs();
	LOGI("Perf: %.6f M samples / s\n", 20.0 / (1e-9 * double(end - start)));
}
