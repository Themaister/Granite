#include "dsp/tone_filter.hpp"
#include "timer.hpp"
#include "logging.hpp"
#include <random>
#include <cmath>

using namespace Granite::Audio;

#if 0
#include "simd_headers.hpp"

static float recp(float v)
{
	float32x2_t vs = vdup_n_f32(v);
	vs = vrecpe_f32(vs);
	return vget_lane_f32(vs, 0);
}

static float rsqrt(float v)
{
	float32x2_t vs = vdup_n_f32(v);
	vs = vmul_f32(vs, vrsqrte_f32(vmax_f32(vs, vdup_n_f32(1e-30f))));
	return vget_lane_f32(vs, 0);
}

static void test_div_sqrt()
{
	float inputs[41];
	for (int i = 0; i <= 40; i++)
		inputs[i] = 3.0f * std::pow(10.0f, float(i) - 20.0f);

	for (auto i : inputs)
		LOGI("recp(%g) = %g\n", i, recp(i));
	for (auto i : inputs)
		LOGI("sqrt(%g) = %g\n", i, rsqrt(i));
}
#endif

int main()
{
#if 0
	test_div_sqrt();
#endif

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
